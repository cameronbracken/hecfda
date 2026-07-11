// ported from: HEC.FDA.Model/extensions/GraphicalDistribution.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_EXTENSIONS_GRAPHICAL_DISTRIBUTION_HPP
#define HECFDA_MODEL_EXTENSIONS_GRAPHICAL_DISTRIBUTION_HPP
#include <memory>
#include <utility>
#include <vector>
#include "hecfda/model/utilities/graphical_frequency_uncertainty_calculators.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
namespace hecfda {
namespace model {
namespace extensions {

// ported from: GraphicalDistribution.cs. Wraps GraphicalFrequencyUncertaintyCalculators::
// less_simple_method() (Task P2T4a) into a small value type carrying the expanded exceedance
// probabilities, one distribution per probability (Normal for stages, LogNormal for flows -- see
// construct_distributions() in graphical_frequency_uncertainty_calculators.hpp), and each
// distribution's median (InverseCDF(0.5)) as the "point" stage/flow value. Beth Faber's "Less
// Simple Method" -- see the HEC-FDA Technical Reference (CPD-72a).
//
// Move-only: stage_or_log_flow_distributions_ holds unique_ptr<IDistribution> (same rationale as
// UncertainPairedData -- see uncertain_paired_data.hpp's class comment). Construct a fresh
// instance per use rather than copying one.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//   - `: ValidationErrorLogger` base + AddRules()/Validate()/ErrorLevel/ReportMessage(): there is
//     no rules/validation-on-construct infrastructure anywhere in this Model-layer port (same
//     severance already established for UncertainPairedData's AddRules() -- see
//     uncertain_paired_data.hpp). The C# ctor's behavior when ErrorLevel >= Major (leaving
//     StageOrLogFlowDistributions unset, i.e. null) has no analog: this port always finishes
//     construction with StageOrLogFlowDistributions populated. IsArrayValid (the private static
//     helper AddRules' Rules delegate to, checking `ExceedanceProbabilities` monotonically
//     decreasing / `StageOrLoggedFlowValues` monotonically increasing) is likewise not ported --
//     it has no caller once AddRules is severed, and no test exercises it directly (verified
//     against GraphicalTests.cs / GraphicalUncertaintyPairedDataTests.cs: neither constructs a
//     GraphicalDistribution expecting a validation error).
//   - The private 5-arg ctor `GraphicalDistribution(stageOrLoggedFlowValues,
//     usingStagesNotFlows, equivalentRecordLength, exceedanceProbabilities,
//     stageOrLogFlowDistributions)`: used ONLY by ReadFromXML to reconstruct a deserialized
//     instance -- no analog once XML is severed (see below).
//   - WriteToXML() / ReadFromXML(XElement): XML (de)serialization -- severed, no XML layer in
//     this port (same rationale as CurveMetaData/PairedData/UncertainPairedData).
//   - GUI/[StoredProperty] attributes: no reflection-driven serialization layer in this port.
class GraphicalDistribution {
   public:
    using IDistribution = hecfda::statistics::distributions::IDistribution;

    // ported from: GraphicalDistribution.cs's parameterless ctor. Matches the C# defaults
    // (EquivalentRecordLength=10, UsingStagesNotFlows=true, ExceedanceProbabilities={0},
    // StageOrLogFlowDistributions={Normal(0,1)}, StageOrLoggedFlowValues={0}) verbatim -- used by
    // GraphicalUncertainPairedData's own parameterless ctor.
    GraphicalDistribution()
        : equivalent_record_length_(10),
          using_stages_not_flows_(true),
          exceedance_probabilities_{0.0},
          stage_or_logged_flow_values_{0.0} {
        stage_or_log_flow_distributions_.push_back(
            std::make_unique<hecfda::statistics::distributions::Normal>(0.0, 1.0));
    }

    // ported from: GraphicalDistribution.cs GraphicalDistribution(double[]
    // userInputExceedanceProbabilities, double[] stageOrUnloggedFlowValues, int
    // equivalentRecordLength, bool usingStagesNotFlows = true). Transcribed verbatim: delegates
    // to GraphicalFrequencyUncertaintyCalculators::less_simple_method() (Task P2T4a) for the
    // expanded exceedance probabilities + per-point distributions, then reads each distribution's
    // median as the point stage/flow value. The C# AddRules()/Validate()/ErrorLevel branch that
    // would otherwise gate assignment of StageOrLogFlowDistributions is severed (see class
    // comment) -- this ctor unconditionally finishes with StageOrLogFlowDistributions populated,
    // matching the C#'s non-error (ErrorLevel < Major) path, which is the only path exercised by
    // any test.
    GraphicalDistribution(const std::vector<double>& user_input_exceedance_probabilities,
                          const std::vector<double>& stage_or_unlogged_flow_values,
                          int equivalent_record_length, bool using_stages_not_flows = true)
        : equivalent_record_length_(equivalent_record_length),
          using_stages_not_flows_(using_stages_not_flows) {
        auto less_simple_result =
            hecfda::model::utilities::GraphicalFrequencyUncertaintyCalculators::less_simple_method(
                user_input_exceedance_probabilities, stage_or_unlogged_flow_values,
                using_stages_not_flows_, equivalent_record_length_);
        exceedance_probabilities_ = std::move(less_simple_result.first);
        std::vector<std::unique_ptr<IDistribution>> stage_or_log_flow_dists =
            std::move(less_simple_result.second);

        stage_or_logged_flow_values_.reserve(stage_or_log_flow_dists.size());
        for (const auto& dist : stage_or_log_flow_dists) {
            stage_or_logged_flow_values_.push_back(dist->inverse_cdf(0.5));
        }

        // "then we compute uncertainty" (C# else branch -- always taken here, see class comment).
        stage_or_log_flow_distributions_ = std::move(stage_or_log_flow_dists);
    }

    // Move-only: stage_or_log_flow_distributions_ holds unique_ptr (see class comment).
    GraphicalDistribution(GraphicalDistribution&&) = default;
    GraphicalDistribution& operator=(GraphicalDistribution&&) = default;
    GraphicalDistribution(const GraphicalDistribution&) = delete;
    GraphicalDistribution& operator=(const GraphicalDistribution&) = delete;

    const std::vector<double>& stage_or_logged_flow_values() const { return stage_or_logged_flow_values_; }
    bool using_stages_not_flows() const { return using_stages_not_flows_; }
    int equivalent_record_length() const { return equivalent_record_length_; }
    const std::vector<double>& exceedance_probabilities() const { return exceedance_probabilities_; }
    const std::vector<std::unique_ptr<IDistribution>>& stage_or_log_flow_distributions() const {
        return stage_or_log_flow_distributions_;
    }

   private:
    int equivalent_record_length_;
    bool using_stages_not_flows_;
    std::vector<double> exceedance_probabilities_;
    std::vector<double> stage_or_logged_flow_values_;
    std::vector<std::unique_ptr<IDistribution>> stage_or_log_flow_distributions_;
};
}  // namespace extensions
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_EXTENSIONS_GRAPHICAL_DISTRIBUTION_HPP
