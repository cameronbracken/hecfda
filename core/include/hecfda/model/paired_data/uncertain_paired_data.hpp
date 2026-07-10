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
    PairedData sample_paired_data(const std::vector<double>& packet) const {
        std::vector<double> y(ys_.size());
        for (std::size_t i = 0; i < ys_.size(); ++i) y[i] = ys_[i].inverse_cdf(packet[i]);
        return PairedData(xs_, y);
    }
    double sample_and_integrate(int seed) const {
        hecfda::model::compute::RandomProvider rp(seed);
        std::vector<double> packet = rp.next_random_sequence(static_cast<long>(xs_.size()));
        return sample_paired_data(packet).integrate();
    }
   private:
    std::vector<double> xs_;
    std::vector<hecfda::statistics::distributions::Normal> ys_;
};
}}}  // namespace
#endif
