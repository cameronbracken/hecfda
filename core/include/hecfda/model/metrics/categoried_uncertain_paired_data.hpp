// ported from: HEC.FDA.Model/metrics/CategoriedUncertainPairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_CATEGORIED_UNCERTAIN_PAIRED_DATA_HPP
#define HECFDA_MODEL_METRICS_CATEGORIED_UNCERTAIN_PAIRED_DATA_HPP
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/categoried_paired_data.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: CategoriedUncertainPairedData.cs `public class CategoriedUncertainPairedData`. The
// per-(damageCategory, assetCategory, ConsequenceType, RiskType) damage/FN-frequency curve
// accumulator the EAD Monte Carlo loop feeds: each iteration's damage-frequency `PairedData`
// realization is staged into a per-ordinate temp array via `add_curve_realization`, batches are
// flushed into a lazily-constructed `DynamicHistogram` per ordinate via
// `put_data_into_histograms`, and `get_uncertain_paired_data` packages the resulting per-ordinate
// histograms into an `UncertainPairedData` FN/frequency curve. Curves MUST be added in batches
// (matching the C# doc comment) -- `_TempYValues[i]` is fixed-size at `ConvergenceCriteria.
// IterationCount`, so `add_curve_realization`'s `iteration_index` must stay within that batch
// window between `put_data_into_histograms()` flushes, exactly as the C# array-indexed staging
// requires.
//
// `DynamicHistogram` storage, a deliberate port simplification vs. `AggregatedConsequencesBinned`/
// `AssuranceResultStorage`'s `std::unique_ptr<DynamicHistogram>` members: those types model a C#
// property that starts `null` and is constructed exactly once. Here the C# field is
// `List<DynamicHistogram> YHistograms = []` -- never null, always a (possibly empty) list -- and
// `DynamicHistogram` has no `unique_ptr` members of its own (verified: its compiler-generated copy
// ctor is exactly what `StudyAreaConsequencesBinned::to_uncertain_paired_data` relies on to clone
// histograms), so `y_histograms_` is stored as a plain `std::vector<DynamicHistogram>` -- a
// value-semantics equivalent of `List<DynamicHistogram>`, no `unique_ptr`/nullability needed.
//
// Bin-width quirk, transcribed verbatim and DISTINCT from `AggregatedConsequencesBinned`'s own
// `PutDataIntoHistogram`: that sibling type's narrow-range branch uses
// `DynamicHistogram::DEFAULT_BIN_WIDTH` (.0001), but `InitializeHistograms` here hardcodes the
// literal `0.001` (one order of magnitude wider) -- both are real upstream constants, not a typo
// to reconcile; kept exactly as each source file wrote it.
//
// Staged-array quirk (shared with `AggregatedConsequencesBinned`, see that header's comment):
// `InitializeHistograms` computes each ordinate's initial bin width from the min/max of the
// ENTIRE `_TempYValues[i]` staging slice, not just the slots `add_curve_realization` actually
// wrote -- unwritten slots are `0.0` (C# `new double[n]` default), so they participate in the
// range/bin-width computation on the first `put_data_into_histograms()` call unless every one of
// the batch's `IterationCount` slots was staged.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - WriteToXML()/ReadFromXML(XElement): XML (de)serialization, no equivalent surface in this
//    port (repo-wide XML severance, matching ConvergenceCriteria/DynamicHistogram/CurveMetaData).
class CategoriedUncertainPairedData {
   public:
    // ported from: CategoriedUncertainPairedData.cs field constant.
    static constexpr int INITIAL_BIN_QUANTITY = 500;

    // ported from: CategoriedUncertainPairedData.cs `public CategoriedUncertainPairedData(
    // double[] xvals, string damageCategory, string assetCategory, ConsequenceType
    // consequenceType, RiskType riskType, ConvergenceCriteria convergenceCriteria)`. Defers
    // histogram construction (histograms_not_constructed_ = true, matching
    // _HistogramsNotConstructed's field initializer) and sizes every ordinate's staging array to
    // convergenceCriteria.IterationCount, matching C# exactly.
    CategoriedUncertainPairedData(std::vector<double> xvals, std::string damage_category,
                                   std::string asset_category, ConsequenceType consequence_type,
                                   RiskType risk_type,
                                   statistics::ConvergenceCriteria convergence_criteria)
        : xvals_(std::move(xvals)),
          damage_category_(std::move(damage_category)),
          asset_category_(std::move(asset_category)),
          consequence_type_(consequence_type),
          risk_type_(risk_type),
          convergence_criteria_(convergence_criteria),
          batch_size_(convergence_criteria.iteration_count()),
          temp_y_values_(xvals_.size(),
                         std::vector<double>(static_cast<std::size_t>(batch_size_), 0.0)) {}

    // ported from: CategoriedUncertainPairedData.cs `public CategoriedUncertainPairedData(
    // CategoriedPairedData initialCurve, ConvergenceCriteria convergenceCriteria)`. Delegates to
    // the ctor above, extracting Xvals and every category label from `initialCurve`.
    CategoriedUncertainPairedData(const CategoriedPairedData& initial_curve,
                                   statistics::ConvergenceCriteria convergence_criteria)
        : CategoriedUncertainPairedData(initial_curve.frequency_curve().xvals(),
                                         initial_curve.damage_category(),
                                         initial_curve.asset_category(),
                                         initial_curve.consequence_type(), initial_curve.risk_type(),
                                         std::move(convergence_criteria)) {}

    const std::vector<double>& xvals() const { return xvals_; }
    const std::vector<statistics::histograms::DynamicHistogram>& y_histograms() const {
        return y_histograms_;
    }
    ConsequenceType consequence_type() const { return consequence_type_; }
    RiskType risk_type() const { return risk_type_; }
    const std::string& damage_category() const { return damage_category_; }
    const std::string& asset_category() const { return asset_category_; }
    const statistics::ConvergenceCriteria& convergence_criteria() const {
        return convergence_criteria_;
    }

    // ported from: CategoriedUncertainPairedData.cs `public void AddCurveRealization(PairedData
    // frequencyCurve, long iterationIndex)`.
    void add_curve_realization(const paired_data::PairedData& frequency_curve,
                                std::int64_t iteration_index) {
        if (frequency_curve.xvals().size() != xvals_.size()) {
            throw std::invalid_argument(
                "frequency curves need to have the same x ordinates to be added.");
        }
        const std::vector<double>& yvals = frequency_curve.yvals();
        for (std::size_t i = 0; i < yvals.size(); ++i) {
            temp_y_values_[i][static_cast<std::size_t>(iteration_index)] = yvals[i];
        }
    }

    // ported from: CategoriedUncertainPairedData.cs `public void PutDataIntoHistograms()`.
    void put_data_into_histograms() {
        if (histograms_not_constructed_) {
            initialize_histograms();
            histograms_not_constructed_ = false;
        }
        for (std::size_t i = 0; i < temp_y_values_.size(); ++i) {
            y_histograms_[i].add_observations_to_histogram(temp_y_values_[i]);
            std::fill(temp_y_values_[i].begin(), temp_y_values_[i].end(), 0.0);
        }
    }

    // ported from: CategoriedUncertainPairedData.cs `public UncertainPairedData
    // GetUncertainPairedData()` -- `new(_Xvals, YHistograms.ToArray(), new())`. Each
    // `DynamicHistogram` is CLONED (compiler-generated copy ctor, no unique_ptr members) into a
    // fresh `unique_ptr<IDistribution>`, the same value-semantics equivalent
    // `StudyAreaConsequencesBinned::to_uncertain_paired_data` uses -- `DynamicHistogram` already
    // derives `IDistribution` (via `ContinuousDistribution`), so no new `UncertainPairedData` ctor
    // is needed. `new()` (a fresh default-constructed `CurveMetaData`, `IsNull() == true`) is
    // `paired_data::CurveMetaData()`'s default ctor.
    paired_data::UncertainPairedData get_uncertain_paired_data() const {
        std::vector<std::unique_ptr<statistics::distributions::IDistribution>> ys;
        ys.reserve(y_histograms_.size());
        for (const statistics::histograms::DynamicHistogram& histogram : y_histograms_) {
            ys.push_back(std::make_unique<statistics::histograms::DynamicHistogram>(histogram));
        }
        return paired_data::UncertainPairedData(xvals_, std::move(ys), paired_data::CurveMetaData());
    }

   private:
    // ported from: CategoriedUncertainPairedData.cs `private void InitializeHistograms()`. See the
    // class comment for the 0.001-literal bin-width quirk and the staged-array min/max quirk this
    // transcribes verbatim.
    void initialize_histograms() {
        y_histograms_.clear();
        y_histograms_.reserve(temp_y_values_.size());
        for (const std::vector<double>& temp_column : temp_y_values_) {
            double max = *std::max_element(temp_column.begin(), temp_column.end());
            double min = *std::min_element(temp_column.begin(), temp_column.end());
            double range = max - min;
            double bin_width = (range < INITIAL_BIN_QUANTITY) ? 0.001 : range / INITIAL_BIN_QUANTITY;
            y_histograms_.emplace_back(bin_width, convergence_criteria_);
        }
    }

    std::vector<double> xvals_;
    std::string damage_category_;
    std::string asset_category_;
    ConsequenceType consequence_type_;
    RiskType risk_type_;
    statistics::ConvergenceCriteria convergence_criteria_;
    int batch_size_;
    std::vector<std::vector<double>> temp_y_values_;
    bool histograms_not_constructed_ = true;
    std::vector<statistics::histograms::DynamicHistogram> y_histograms_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_CATEGORIED_UNCERTAIN_PAIRED_DATA_HPP
