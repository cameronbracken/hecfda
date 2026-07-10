// ported from: HEC.FDA.Model/paireddata/UncertainPairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_UNCERTAIN_PAIRED_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_UNCERTAIN_PAIRED_DATA_HPP
#include <vector>
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
namespace hecfda { namespace model { namespace paired_data {
class UncertainPairedData {
   public:
    UncertainPairedData(std::vector<double> xs,
                        std::vector<hecfda::statistics::distributions::Normal> ys)
        : xs_(std::move(xs)), ys_(std::move(ys)) {}
    // ported from: UncertainPairedData.cs SamplePairedDataRaw(double probability)
    // A single scalar probability is applied to every point's inverse CDF. The non-raw
    // SamplePairedData additionally calls ForceWeakMonotonicityBottomUp() after this; that
    // helper is not yet ported, so this is a documented Phase-0 severance.
    PairedData sample_paired_data_raw(double probability) const {
        std::vector<double> y(ys_.size());
        for (std::size_t i = 0; i < ys_.size(); ++i) y[i] = ys_[i].inverse_cdf(probability);
        return PairedData(xs_, y);
    }
    double sample_and_integrate(int seed) const {
        double p = hecfda::model::compute::RandomProvider(seed).next_random();
        return sample_paired_data_raw(p).integrate();
    }
   private:
    std::vector<double> xs_;
    std::vector<hecfda::statistics::distributions::Normal> ys_;
};
}}}  // namespace
#endif
