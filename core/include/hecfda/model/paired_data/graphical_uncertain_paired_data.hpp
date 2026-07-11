// ported from: HEC.FDA.Model/paireddata/GraphicalUncertainPairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_GRAPHICAL_UNCERTAIN_PAIRED_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_GRAPHICAL_UNCERTAIN_PAIRED_DATA_HPP
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/model/extensions/graphical_distribution.hpp"
#include "hecfda/model/interfaces/i_can_be_null.hpp"
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/i_meta_data.hpp"
#include "hecfda/model/paired_data/i_paired_data_producer.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
namespace hecfda {
namespace model {
namespace paired_data {

// ported from: GraphicalUncertainPairedData.cs. The non-parametric ("graphical") counterpart to
// UncertainPairedData: instead of taking a caller-supplied IDistribution per X point, it builds a
// GraphicalDistribution (Task P2T4b's extensions::GraphicalDistribution, wrapping Beth Faber's
// "Less Simple Method" -- Task P2T4a) from the input exceedance-probability/stage-or-flow curve
// and samples THAT curve's per-point distributions.
//
// Implements IPairedDataProducer, ICanBeNull (hecfda::model::interfaces), and IMetaData -- same
// three C# interfaces (`GraphicalUncertainPairedData : ValidationErrorLogger, IPairedDataProducer,
// ICanBeNull, IMetaData`), minus the ValidationErrorLogger base (severed, see class comment below).
//
// Move-only: holds a GraphicalDistribution member, which itself holds
// vector<unique_ptr<IDistribution>> (see graphical_distribution.hpp's class comment) and is
// therefore move-only. Construct a fresh instance per use rather than copying one (same
// fixture-dispatch convention as UncertainPairedData/GraphicalDistribution).
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//   - `: ValidationErrorLogger` base: no rules/validation-on-construct infrastructure anywhere in
//     this Model-layer port (same severance already established for UncertainPairedData and
//     GraphicalDistribution).
//   - The private 3-arg ctor `GraphicalUncertainPairedData(combinedExceedanceProbabilities,
//     graphicalDistributionWithLessSimple, curveMetaData)`: used ONLY by ReadFromXML -- no analog
//     once XML is severed (see below).
//   - WriteToXML() / ReadFromXML(XElement): XML (de)serialization -- severed, no XML layer in
//     this port (same rationale as CurveMetaData/PairedData/UncertainPairedData/
//     GraphicalDistribution). GraphicalShouldReadTheSameStuffItWrites (the one C# test that
//     exercises WriteToXML/ReadFromXML/Equals together) has no analog for the same reason.
//   - GUI/[StoredProperty] attributes: no reflection-driven serialization layer in this port.
class GraphicalUncertainPairedData : public IPairedDataProducer,
                                     public hecfda::model::interfaces::ICanBeNull,
                                     public IMetaData {
   public:
    // ported from: GraphicalUncertainPairedData.cs's parameterless ctor
    // (CurveMetaData=new(), GraphicalDistributionWithLessSimple=new(),
    // CombinedExceedanceProbabilities={0}).
    GraphicalUncertainPairedData()
        : graphical_distribution_with_less_simple_(),
          curve_meta_data_(),
          combined_exceedance_probabilities_{0.0} {}

    // ported from: GraphicalUncertainPairedData.cs GraphicalUncertainPairedData(double[]
    // exceedanceProbabilities, double[] flowOrStageValues, int equivalentRecordLength,
    // CurveMetaData curveMetaData, bool usingStagesNotFlows). Builds the GraphicalDistribution
    // (the C#'s "GraphicalDistributionWithLessSimple"), then echoes its (expanded) exceedance
    // probabilities back as CombinedExceedanceProbabilities.
    GraphicalUncertainPairedData(const std::vector<double>& exceedance_probabilities,
                                 const std::vector<double>& flow_or_stage_values,
                                 int equivalent_record_length, CurveMetaData curve_meta_data,
                                 bool using_stages_not_flows)
        : graphical_distribution_with_less_simple_(exceedance_probabilities, flow_or_stage_values,
                                                    equivalent_record_length, using_stages_not_flows),
          curve_meta_data_(std::move(curve_meta_data)) {
        combined_exceedance_probabilities_ = graphical_distribution_with_less_simple_.exceedance_probabilities();
    }

    // Move-only: graphical_distribution_with_less_simple_ is move-only (see class comment).
    GraphicalUncertainPairedData(GraphicalUncertainPairedData&&) = default;
    GraphicalUncertainPairedData& operator=(GraphicalUncertainPairedData&&) = default;
    GraphicalUncertainPairedData(const GraphicalUncertainPairedData&) = delete;
    GraphicalUncertainPairedData& operator=(const GraphicalUncertainPairedData&) = delete;

    const extensions::GraphicalDistribution& graphical_distribution_with_less_simple() const {
        return graphical_distribution_with_less_simple_;
    }
    const std::vector<double>& combined_exceedance_probabilities() const {
        return combined_exceedance_probabilities_;
    }

    // ported from: GraphicalUncertainPairedData.cs's IsNull getter (`CurveMetaData.IsNull`).
    bool is_null() const override { return curve_meta_data_.is_null(); }

    // ported from: GraphicalUncertainPairedData.cs's IMetaData.CurveMetaData getter.
    const CurveMetaData& curve_meta_data() const override { return curve_meta_data_; }

    // ported from: GraphicalUncertainPairedData.cs GenerateRandomNumbers(int seed, int size).
    // Delegated to RandomProvider, same as UncertainPairedData::generate_random_numbers (see its
    // comment for the seeded-stream fidelity note).
    void generate_random_numbers(int seed, long size) {
        random_numbers_ = hecfda::model::compute::RandomProvider(seed).next_random_sequence(size);
    }

    // ported from: GraphicalUncertainPairedData.cs SamplePairedData(double probability).
    // "Returns the relationship in Non-Exceedance Probabilities." Samples every per-point
    // distribution at `probability` via InverseCDF, builds a PairedData over the NON-exceedance
    // probabilities (1 - exceedance) carrying this curve's metadata, then forces STRICT
    // monotonicity -- bottom-up for probability < 0.5, top-down otherwise (matching the C#'s
    // `probability < 0.5` branch verbatim).
    PairedData sample_paired_data(double probability) const override {
        const auto& dists = graphical_distribution_with_less_simple_.stage_or_log_flow_distributions();
        std::size_t num_coords = dists.size();

        std::vector<double> y(num_coords);
        for (std::size_t i = 0; i < num_coords; ++i) {
            y[i] = dists[i]->inverse_cdf(probability);
        }

        PairedData paired_data(
            exceedance_to_non_exceedance(graphical_distribution_with_less_simple_.exceedance_probabilities()),
            std::move(y), curve_meta_data_);
        if (probability < 0.5) {
            paired_data.force_strict_monotonicity_bottom_up();
        } else {
            paired_data.force_strict_monotonicity_top_down();
        }
        return paired_data;
    }

    // ported from: GraphicalUncertainPairedData.cs SamplePairedData(long iterationNumber, bool
    // computeIsDeterministic = false). computeIsDeterministic samples at probability 0.5 (the
    // median); otherwise samples at the previously-generated random number for iterationNumber,
    // throwing the same "no random numbers"/"bad index" errors as UncertainPairedData's
    // iteration-overload (see uncertain_paired_data.hpp's sample_paired_data(long, bool) comment
    // -- identical error text and empty-vector-covers-both-cases rationale).
    PairedData sample_paired_data(long iteration_number, bool compute_is_deterministic = false) const override {
        double probability;
        if (compute_is_deterministic) {
            probability = 0.5;
        } else {
            if (random_numbers_.empty()) {
                throw std::runtime_error("Random numbers have not been created for UPD sampling");
            }
            if (iteration_number < 0 || iteration_number >= static_cast<long>(random_numbers_.size())) {
                throw std::out_of_range(
                    "Iteration number cannot be less than 0 or greater than the size of the random "
                    "number array");
            }
            probability = random_numbers_[static_cast<std::size_t>(iteration_number)];
        }
        return sample_paired_data(probability);
    }

    // ported from: GraphicalUncertainPairedData.cs Equals(GraphicalUncertainPairedData
    // incomingGraphicalUncertainPairedData). Short-circuits true if both curves' CurveMetaData are
    // null; otherwise compares equivalent record length, every per-point distribution (via
    // IDistribution::equals), and every combined exceedance probability, in that order --
    // verbatim, including the C#'s implicit assumption that both curves have the same number of
    // distributions/probabilities (an index out-of-range here mirrors the C#'s own unguarded
    // `incoming...[i]` indexing).
    bool equals(const GraphicalUncertainPairedData& incoming) const {
        bool null_matches = curve_meta_data_.is_null() == incoming.curve_meta_data_.is_null();
        if (null_matches && is_null()) {
            return true;
        }
        bool erl_is_the_same = graphical_distribution_with_less_simple_.equivalent_record_length() ==
                                incoming.graphical_distribution_with_less_simple_.equivalent_record_length();
        if (!erl_is_the_same) {
            return false;
        }
        const auto& dists = graphical_distribution_with_less_simple_.stage_or_log_flow_distributions();
        const auto& incoming_dists = incoming.graphical_distribution_with_less_simple_.stage_or_log_flow_distributions();
        for (std::size_t i = 0; i < dists.size(); ++i) {
            if (!dists[i]->equals(*incoming_dists[i])) {
                return false;
            }
        }
        for (std::size_t i = 0; i < combined_exceedance_probabilities_.size(); ++i) {
            if (combined_exceedance_probabilities_[i] != incoming.combined_exceedance_probabilities_[i]) {
                return false;
            }
        }
        return true;
    }

   private:
    // ported from: GraphicalUncertainPairedData.cs's private static ExceedanceToNonExceedance(
    // double[] exceedanceProbabilities) -- `1 - p` per element.
    static std::vector<double> exceedance_to_non_exceedance(const std::vector<double>& exceedance_probabilities) {
        std::vector<double> non_exceedance_probabilities(exceedance_probabilities.size());
        for (std::size_t i = 0; i < exceedance_probabilities.size(); ++i) {
            non_exceedance_probabilities[i] = 1.0 - exceedance_probabilities[i];
        }
        return non_exceedance_probabilities;
    }

    extensions::GraphicalDistribution graphical_distribution_with_less_simple_;
    CurveMetaData curve_meta_data_;
    std::vector<double> combined_exceedance_probabilities_;
    std::vector<double> random_numbers_;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_GRAPHICAL_UNCERTAIN_PAIRED_DATA_HPP
