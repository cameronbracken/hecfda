// ported from: HEC.FDA.Statistics/Distributions/UncertainToDeterministicDistributionConverter.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_UNCERTAIN_TO_DETERMINISTIC_CONVERTER_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_UNCERTAIN_TO_DETERMINISTIC_CONVERTER_HPP
#include <cmath>
#include <stdexcept>
#include "hecfda/statistics/distributions/deterministic.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/distributions/i_distribution_enum.hpp"
#include "hecfda/statistics/distributions/lognormal.hpp"
#include "hecfda/statistics/distributions/logpearson3.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/triangular.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: UncertainToDeterministicDistributionConverter.cs's static
// ConvertDistributionToDeterministic(IDistribution). Maps any distribution to a Deterministic at
// its "central value". Transcribed verbatim, INCLUDING two genuine upstream quirks that are
// reproduced rather than fixed:
//   - LogPearsonIII: `Math.Pow(logLP3Mean, 10)` computes logMean^10, NOT 10^logMean (the
//     presumably-intended un-log10 operation). This IS an upstream bug -- see the case below.
//   - LogNormal: `Math.Exp(logNormalMean)` -- natural exp of the mean, even though LogNormal's own
//     Fit() logs data with log10 (see lognormal.hpp's class comment for that separate, pre-existing
//     quirk). Reproduced as-is.
//
// SCOPE (Task D1, Phase 1 = Statistics layer): handles the 7 IDistribution cases reachable through
// `const IDistribution&` in this port -- Normal, Uniform, Triangular, LogPearsonIII, LogNormal,
// Empirical, Deterministic/NotSupported. Two upstream cases are DEFERRED to Phase 2, not
// implemented here:
//   - `IDistributionEnum.IHistogram`: DynamicHistogram (Task C2) does NOT derive from IDistribution
//     in this port (i_histogram.hpp is a separate interface) -- so this case is unreachable through
//     `const IDistribution&` and is not bridgeable without a Model-layer change. If ever reached
//     (it should not be, in Phase 1), throws rather than silently returning an incorrect value.
//   - UncertainPairedData's deterministic-sample-path wiring (the Model-layer caller that would
//     invoke this converter against a paired-data uncertainty distribution): requires generalizing
//     UncertainPairedData from concrete Normal to IDistribution, a Phase-2 concern. This header is
//     the converter only.
//
// Any DistributionType not matched by a case (TruncatedNormal, and the port-internal
// TruncatedLogNormal/TruncatedLogPearson3 factory keys -- none of which appear in the upstream
// switch either) falls through to C#'s pre-switch-initialized `new Deterministic()` (Value ==
// default(double) == 0.0), matching the C# method's `Deterministic returnedDistribution = new
// Deterministic();` declared before the switch and never reassigned for an unmatched case.
inline Deterministic convert_distribution_to_deterministic(const IDistribution& dist) {
    switch (dist.type()) {
        // ported from: `case IDistributionEnum.NotSupported: case IDistributionEnum.Deterministic:
        // returnedDistribution = (Deterministic)iDistribution;`. NotSupported has no
        // IDistributionFactory case in this port (see i_distribution_enum.hpp) and is not
        // constructible, so in practice only real Deterministic instances reach this branch.
        case DistributionType::NotSupported:
        case DistributionType::Deterministic: {
            const auto* det = dynamic_cast<const Deterministic*>(&dist);
            if (det == nullptr) {
                throw std::runtime_error(
                    "convert_distribution_to_deterministic: type() is NotSupported/Deterministic "
                    "but the instance is not a Deterministic (mirrors C#'s InvalidCastException on "
                    "`(Deterministic)iDistribution`)");
            }
            return Deterministic(det->value());
        }

        // ported from: `case IDistributionEnum.Normal: double normalMean =
        // ((Normal)iDistribution).Mean; returnedDistribution = new Deterministic(normalMean);`
        case DistributionType::Normal: {
            const auto* normal = dynamic_cast<const Normal*>(&dist);
            if (normal == nullptr) {
                throw std::runtime_error(
                    "convert_distribution_to_deterministic: type() is Normal but the instance is "
                    "not a Normal (mirrors C#'s InvalidCastException on `(Normal)iDistribution`)");
            }
            return Deterministic(normal->mean());
        }

        // ported from: `case IDistributionEnum.Uniform: double median = iDistribution.
        // InverseCDF(0.5); returnedDistribution = new Deterministic(median);`. Dispatches
        // polymorphically through IDistribution::inverse_cdf, matching the C# call site exactly (no
        // downcast to Uniform).
        case DistributionType::Uniform:
            return Deterministic(dist.inverse_cdf(0.5));

        // ported from: `case IDistributionEnum.Triangular: double mostLikely =
        // ((Triangular)iDistribution).MostLikely; returnedDistribution = new
        // Deterministic(mostLikely);`
        case DistributionType::Triangular: {
            const auto* tri = dynamic_cast<const Triangular*>(&dist);
            if (tri == nullptr) {
                throw std::runtime_error(
                    "convert_distribution_to_deterministic: type() is Triangular but the instance "
                    "is not a Triangular (mirrors C#'s InvalidCastException on "
                    "`(Triangular)iDistribution`)");
            }
            return Deterministic(tri->most_likely());
        }

        // ported from: `case IDistributionEnum.LogPearsonIII: double logLP3Mean =
        // ((LogPearson3)iDistribution).Mean; double unloggedLP3Mean = Math.Pow(logLP3Mean, 10);
        // returnedDistribution = new Deterministic(unloggedLP3Mean);`
        //
        // UPSTREAM BUG, reproduced verbatim (NOT fixed): `Math.Pow(logLP3Mean, 10)` computes
        // logLP3Mean^10 (mean raised to the 10th power), NOT 10^logLP3Mean (the un-log10 operation
        // that would actually undo LogPearson3's log-base-10 moment scale). std::pow(mean, 10.0)
        // below reproduces this exactly, including the surprising numeric result.
        case DistributionType::LogPearsonIII: {
            const auto* lp3 = dynamic_cast<const LogPearson3*>(&dist);
            if (lp3 == nullptr) {
                throw std::runtime_error(
                    "convert_distribution_to_deterministic: type() is LogPearsonIII but the "
                    "instance is not a LogPearson3 (mirrors C#'s InvalidCastException on "
                    "`(LogPearson3)iDistribution`)");
            }
            double log_lp3_mean = lp3->mean();
            double unlogged_lp3_mean = std::pow(log_lp3_mean, 10.0);  // BUG: mean^10, not 10^mean.
            return Deterministic(unlogged_lp3_mean);
        }

        // ported from: `case IDistributionEnum.LogNormal: double logNormalMean =
        // ((LogNormal)iDistribution).Mean; double unloggedNormalMean = Math.Exp(logNormalMean);
        // returnedDistribution = new Deterministic(unloggedNormalMean);`. Natural exp (Math.Exp),
        // not 10^ -- reproduced as-is per the upstream quirk documented in lognormal.hpp (Fit()
        // logs with log10, but InverseCDF/this converter use natural log/exp).
        case DistributionType::LogNormal: {
            const auto* ln = dynamic_cast<const LogNormal*>(&dist);
            if (ln == nullptr) {
                throw std::runtime_error(
                    "convert_distribution_to_deterministic: type() is LogNormal but the instance is "
                    "not a LogNormal (mirrors C#'s InvalidCastException on "
                    "`(LogNormal)iDistribution`)");
            }
            double log_normal_mean = ln->mean();
            double unlogged_normal_mean = std::exp(log_normal_mean);
            return Deterministic(unlogged_normal_mean);
        }

        // ported from: `case IDistributionEnum.Empirical: double empiricalMean =
        // ((Empirical)iDistribution).SampleMean; returnedDistribution = new
        // Deterministic(empiricalMean);`
        case DistributionType::Empirical: {
            const auto* emp = dynamic_cast<const Empirical*>(&dist);
            if (emp == nullptr) {
                throw std::runtime_error(
                    "convert_distribution_to_deterministic: type() is Empirical but the instance is "
                    "not an Empirical (mirrors C#'s InvalidCastException on "
                    "`(Empirical)iDistribution`)");
            }
            return Deterministic(emp->sample_mean());
        }

        // ported from: `case IDistributionEnum.IHistogram: double mean =
        // ((Histograms.IHistogram)iDistribution).SampleMean; returnedDistribution = new
        // Deterministic(mean);`. DEFERRED to Phase 2 -- see the class-level comment above. Not
        // reachable through `const IDistribution&` in this port; throws rather than silently
        // falling through to the wrong (default-0.0) branch below if it is ever reached.
        case DistributionType::IHistogram:
            throw std::logic_error(
                "convert_distribution_to_deterministic: IHistogram case is deferred to Phase 2 "
                "(DynamicHistogram does not derive from IDistribution in this port) and is not "
                "implemented");

        // ported from: the C# switch's implicit fall-through for any unmatched Type -- the
        // pre-switch `Deterministic returnedDistribution = new Deterministic();` (Value ==
        // default(double) == 0.0) is returned unchanged. TruncatedNormal (real Type ==
        // TruncatedNormal) and the port-internal TruncatedLogNormal/TruncatedLogPearson3 factory
        // keys (never returned by any instance's type(), per i_distribution_enum.hpp) all land
        // here, matching upstream: none of them has a `case` in the real C# switch either.
        case DistributionType::TruncatedNormal:
        case DistributionType::TruncatedLogNormal:
        case DistributionType::TruncatedLogPearson3:
        default:
            return Deterministic(0.0);
    }
}

}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_UNCERTAIN_TO_DETERMINISTIC_CONVERTER_HPP
