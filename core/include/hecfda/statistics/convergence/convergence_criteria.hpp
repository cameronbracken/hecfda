// ported from: HEC.FDA.Statistics/Convergence/ConvergeCriteria.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_CONVERGENCE_CONVERGENCE_CRITERIA_HPP
#define HECFDA_STATISTICS_CONVERGENCE_CONVERGENCE_CRITERIA_HPP
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace statistics {

// ported from: ConvergeCriteria.cs `public class ConvergenceCriteria : Validation`. Monte Carlo
// stopping-rule parameters (min/max iterations, the normal-quantile ZAlpha used for the
// convergence half-width test, and the relative Tolerance). Small parameter-holder class; the
// histogram (a later task) consumes it to decide when to stop simulating.
//
// IterationCount quirk, transcribed verbatim: the C# field initializer is `= 100`, but the ctor
// body then overwrites it to 1 whenever MaxIterations < 100 (e.g. tiny/test configurations).
// This is NOT `std::min(MaxIterations, 100)` -- it is a binary 100-or-1 switch, exactly as C#
// does it, however small MaxIterations is (0 still yields 1, not 0).
//
// Default ErrorLevel, verified against upstream rather than assumed: HEC.MVVMFramework.Base/
// Implementations/Rule.cs's two-arg ctor `Rule(Func<bool> expr, string msg)` delegates to
// `this(expr, msg, ErrorLevel.Info)` -- so all three AddRules() calls below, which use the C#
// two-arg `new Rule(...)` form, get ErrorLevel.Info (not Fatal/Error/Minor). Transcribed
// faithfully even though Info is a curious choice for "your Monte Carlo config is malformed".
//
// DONE_WITH_CONCERNS (scoped out, not ported; XML severance, documented repo-wide):
//  - WriteToXML()/ReadFromXML(XElement): XML (de)serialization, no equivalent surface in this
//    port. Callers reconstruct ConvergenceCriteria via the ctor with explicit parameters instead.
class ConvergenceCriteria : public Validation {
   public:
    // ported from: ConvergeCriteria.cs ctor default parameters (verbatim, including the
    // full-precision ZAlpha default).
    explicit ConvergenceCriteria(int min_iterations = 50000, int max_iterations = 500000,
                                  double z_alpha = 1.96039491692543, double tolerance = .01)
        : min_iterations_(min_iterations),
          max_iterations_(max_iterations),
          z_alpha_(z_alpha),
          tolerance_(tolerance) {
        if (max_iterations_ < 100) {
            iteration_count_ = 1;
        }
        add_rules();
    }

    int min_iterations() const { return min_iterations_; }
    int max_iterations() const { return max_iterations_; }
    double z_alpha() const { return z_alpha_; }
    double tolerance() const { return tolerance_; }
    int iteration_count() const { return iteration_count_; }

    // ported from: ConvergeCriteria.cs Equals(ConvergenceCriteria convergenceCriteria), field by
    // field with early-return-on-mismatch, transcribed in the same order.
    bool equals(const ConvergenceCriteria& other) const {
        if (min_iterations_ != other.min_iterations_) return false;
        if (max_iterations_ != other.max_iterations_) return false;
        if (z_alpha_ != other.z_alpha_) return false;
        if (tolerance_ != other.tolerance_) return false;
        return true;
    }

   private:
    // ported from: ConvergeCriteria.cs AddRules().
    void add_rules() {
        add_single_property_rule(
            "MaxIterations", [this]() { return max_iterations_ >= min_iterations_; },
            "Max iterations must be at least equal to min iterations but they are not.",
            ErrorLevel::Info);
        add_single_property_rule(
            "ZAlpha", [this]() { return -4 < z_alpha_ && z_alpha_ < 4; },
            "Z Alpha must be between -4 and 4 but is not.", ErrorLevel::Info);
        add_single_property_rule(
            "Tolerance", [this]() { return 0 < tolerance_ && tolerance_ < 1; },
            "Tolerance must be between 0 and 1 but is not.", ErrorLevel::Info);
    }

    int min_iterations_;
    int max_iterations_;
    double z_alpha_;
    double tolerance_;
    int iteration_count_ = 100;
};

}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_CONVERGENCE_CONVERGENCE_CRITERIA_HPP
