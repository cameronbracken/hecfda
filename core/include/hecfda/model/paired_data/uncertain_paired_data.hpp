// ported from: HEC.FDA.Model/paireddata/UncertainPairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_UNCERTAIN_PAIRED_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_UNCERTAIN_PAIRED_DATA_HPP
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/distributions/uncertain_to_deterministic_converter.hpp"
namespace hecfda {
namespace model {
namespace paired_data {

// Generalized from the Phase-0 thin slice (concrete std::vector<Normal>) to the real C# shape:
// Yvals is now IDistribution[] -> std::vector<std::unique_ptr<IDistribution>>, so a curve's
// per-point uncertainty can be any distribution (Normal, Uniform, Triangular, Deterministic, ...),
// not just Normal. Because unique_ptr is not copyable, this class is MOVE-ONLY -- construct a fresh
// instance per use (matching the fixture-dispatch "fresh construction per assertion" convention)
// rather than copying one. `metadata` mirrors the C# CurveMetaData field and is forwarded to every
// PairedData this produces; it is copied into each result (PairedData takes CurveMetaData by value).
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//   - AddRules() + the ValidationErrorLogger/Rule/ErrorLevel rule-registration side effect: there
//     is no rules/validation-on-construct infrastructure anywhere in this Model-layer port (none in
//     CurveMetaData/PairedData either), so the C# ctor's AddRules() call has no analog. The loop it
//     used, IsDistributionArrayValid, IS ported below as a plain standalone helper (available but
//     not wired to any construct-time validation), matching that C#-severance style.
//   - CombineWithWeights(IReadOnlyDictionary<UncertainPairedData,double>): SEVERED. Its body casts
//     each Yvals[i] to DynamicHistogram and calls DynamicHistogram.ConvertToEmpiricalDistribution
//     (deferred -- see dynamic_histogram.hpp) then Empirical.StackEmpiricalDistributionsWeighted
//     (severed -- see empirical.hpp). Both transitive dependencies are already severed elsewhere in
//     this port, and CombineWithWeights has NO test coverage in HEC.FDA.ModelTest (verified against
//     UncertainPairedDataShould.cs and the whole ModelTest tree). Port it later when a real caller
//     needs it.
//   - Equals(UncertainPairedData): not exercised by any fixture and not part of this task's
//     produced interface -- omitted (same severance treatment as above).
//   - ConvertDamagedElementCountToText / QuantilesToText: text/CSV formatting -- severed.
//   - WriteToXML / ReadFromXML: XML (de)serialization -- severed (no XML layer in this port, same as
//     CurveMetaData/PairedData).
class UncertainPairedData {
   public:
    using IDistribution = hecfda::statistics::distributions::IDistribution;

    // ported from: UncertainPairedData.cs UncertainPairedData(double[] xs, IDistribution[] ys,
    // CurveMetaData metadata). `metadata` defaults to a fresh (IsNull==true) CurveMetaData, standing
    // in for the parameterless C# ctor's `new CurveMetaData()` -- see curve_meta_data.hpp.
    UncertainPairedData(std::vector<double> xs, std::vector<std::unique_ptr<IDistribution>> ys,
                        CurveMetaData metadata = CurveMetaData())
        : xs_(std::move(xs)), ys_(std::move(ys)), metadata_(std::move(metadata)) {}

    // Move-only: ys_ holds unique_ptr, so copies are disabled (see class comment).
    UncertainPairedData(UncertainPairedData&&) = default;
    UncertainPairedData& operator=(UncertainPairedData&&) = default;
    UncertainPairedData(const UncertainPairedData&) = delete;
    UncertainPairedData& operator=(const UncertainPairedData&) = delete;

    const std::vector<double>& xvals() const { return xs_; }
    const CurveMetaData& metadata() const { return metadata_; }

    // Indexed Yvals accessor, added Phase 6 Task 5 (ScenarioResults::
    // get_accumulated_life_loss_fn_curve_data): C#'s `(DynamicHistogram)upd.Yvals[i]` reads one
    // per-ordinate distribution out of an already-built UncertainPairedData and hard-casts it.
    // ys_ has no other public accessor (every existing consumer either builds a NEW
    // UncertainPairedData from scratch or samples through inverse_cdf), so this is the minimal
    // read-only surface needed for that one caller: bounds-checked (`.at`, matching C#'s
    // array-index-out-of-range IndexOutOfRangeException on a bad index) rather than the
    // unchecked `operator[]` the private helpers below use internally.
    std::size_t size() const { return ys_.size(); }
    const IDistribution& y_at(std::size_t i) const { return *ys_.at(i); }

    // ported from: UncertainPairedData.cs GenerateRandomNumbers(int seed, long size).
    // C#: `Random random = new Random(seed); for i: randos[i] = random.NextDouble();`. Delegated to
    // RandomProvider(seed).next_random_sequence(size), which reproduces `new Random(seed).
    // NextDouble()`'s seeded stream call-for-call (the DotNetRandom port underneath -- verified
    // elsewhere in this port and pinned by fixtures/sampling/dotnet_random.json).
    void generate_random_numbers(int seed, long size) {
        random_numbers_ = hecfda::model::compute::RandomProvider(seed).next_random_sequence(size);
    }

    // ported from: UncertainPairedData.cs SamplePairedData(double probability). Samples every y at
    // `probability` via InverseCDF, builds a PairedData carrying this curve's metadata, then applies
    // ForceWeakMonotonicityBottomUp() (mutates in place) before returning.
    PairedData sample_paired_data(double probability) const {
        PairedData paired_data(xs_, sample_ys_at(probability), metadata_);
        paired_data.force_weak_monotonicity_bottom_up();
        return paired_data;
    }

    // ported from: UncertainPairedData.cs SamplePairedDataRaw(double probability). Same as
    // sample_paired_data but WITHOUT any monotonicity forcing.
    PairedData sample_paired_data_raw(double probability) const {
        return PairedData(xs_, sample_ys_at(probability), metadata_);
    }

    // ported from: UncertainPairedData.cs SamplePairedDataRawDeterministic(). Each y becomes its
    // central-tendency value via UncertainToDeterministicDistributionConverter
    // (convert_distribution_to_deterministic). No monotonicity forcing (matches C#).
    PairedData sample_paired_data_raw_deterministic() const {
        return PairedData(xs_, deterministic_ys(), metadata_);
    }

    // ported from: UncertainPairedData.cs SamplePairedData(long iterationNumber, bool
    // retrieveDeterministicRepresentation). If retrieveDeterministicRepresentation, each y is its
    // deterministic central value (via the converter); otherwise each y is sampled at the
    // previously-generated random number for iterationNumber. BOTH branches apply
    // ForceWeakMonotonicityBottomUp() before returning (matches C#).
    PairedData sample_paired_data(long iteration_number, bool retrieve_deterministic_representation) const {
        std::vector<double> y;
        if (retrieve_deterministic_representation) {
            y = deterministic_ys();
        } else {
            // C# throws Exception("Random numbers have not been created for UPD sampling") when
            // _RandomNumbers has length 0 (and NullReferenceExceptions when it was never generated);
            // an empty random_numbers_ covers both cases in this port.
            if (random_numbers_.empty()) {
                throw std::runtime_error("Random numbers have not been created for UPD sampling");
            }
            if (iteration_number < 0 ||
                iteration_number >= static_cast<long>(random_numbers_.size())) {
                throw std::out_of_range(
                    "Iteration number cannot be less than 0 or greater than the size of the random "
                    "number array");
            }
            double p = random_numbers_[static_cast<std::size_t>(iteration_number)];
            y = sample_ys_at(p);
        }
        PairedData paired_data(xs_, std::move(y), metadata_);
        paired_data.force_weak_monotonicity_bottom_up();
        return paired_data;
    }

    // ported from: UncertainPairedData.cs private IsDistributionArrayValid(IDistribution[], double
    // prob, Func<double,double,bool> comparison). Returns false if `comparison(ys[i].InverseCDF(prob),
    // ys[i+1].InverseCDF(prob))` holds for ANY adjacent pair, else true (the C# `arrayOfData == null`
    // guard has no analog -- ys_ is a concrete vector, never null). Ported as a plain standalone
    // helper: available, but NOT wired to any validation-on-construct behavior (see the AddRules
    // severance in the class comment).
    bool is_distribution_array_valid(double prob,
                                     const std::function<bool(double, double)>& comparison) const {
        for (std::size_t i = 0; i + 1 < ys_.size(); ++i) {
            if (comparison(ys_[i]->inverse_cdf(prob), ys_[i + 1]->inverse_cdf(prob))) {
                return false;
            }
        }
        return true;
    }

    // Phase-0 convenience retained across the generalization: draw ONE probability from
    // RandomProvider(seed).next_random(), sample_paired_data_raw(p) (no monotonicity forcing),
    // integrate. Pinned at fixtures/paired_data/uncertain_paired_data.json (24.425549382855987 for
    // 5 Normal(mean, 0.5) ys, seed 1234). Semantics unchanged from Phase 0 -- only ys_'s storage
    // type changed underneath.
    double sample_and_integrate(int seed) const {
        double p = hecfda::model::compute::RandomProvider(seed).next_random();
        return sample_paired_data_raw(p).integrate();
    }

   private:
    // Every y sampled at one scalar probability via InverseCDF (the SamplePairedData/Raw loop body).
    std::vector<double> sample_ys_at(double probability) const {
        std::vector<double> y(ys_.size());
        for (std::size_t i = 0; i < ys_.size(); ++i) {
            y[i] = ys_[i]->inverse_cdf(probability);
        }
        return y;
    }

    // Every y reduced to its deterministic central value (the RawDeterministic /
    // retrieveDeterministicRepresentation loop body).
    std::vector<double> deterministic_ys() const {
        std::vector<double> y(ys_.size());
        for (std::size_t i = 0; i < ys_.size(); ++i) {
            y[i] = hecfda::statistics::distributions::convert_distribution_to_deterministic(*ys_[i])
                       .value();
        }
        return y;
    }

    std::vector<double> xs_;
    std::vector<std::unique_ptr<IDistribution>> ys_;
    CurveMetaData metadata_;
    std::vector<double> random_numbers_;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_UNCERTAIN_PAIRED_DATA_HPP
