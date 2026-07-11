#include <cpp11.hpp>
#include <stdexcept>
#include <vector>
#include <string>
#include "hecfda_core/include/hecfda/model/compute/random_provider.hpp"
#include "hecfda_core/include/hecfda/statistics/distributions/i_distribution_factory.hpp"
#include "hecfda_core/include/hecfda/statistics/distributions/normal.hpp"
#include "hecfda_core/include/hecfda/statistics/validation.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/paired_data.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/uncertain_paired_data.hpp"
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;

[[cpp11::register]] cpp11::doubles hecfda_rng_sequence(int seed, int n) {
    hecfda::model::compute::RandomProvider rp(seed);
    auto v = rp.next_random_sequence(n);
    return cpp11::doubles(cpp11::writable::doubles(v.begin(), v.end()));
}
static nd::DistributionType hecfda_dist_type_from_name(const std::string& type) {
    if (type == "Normal") return nd::DistributionType::Normal;
    if (type == "Uniform") return nd::DistributionType::Uniform;
    throw std::invalid_argument("hecfda_dist_type_from_name: unknown distribution type: " + type);
}
// Generic factory-based distribution dispatch (Task A4): pdf/cdf/inverse_cdf evaluate directly;
// has_errors/error_level validate() first via a dynamic_cast to hecfda::statistics::Validation
// (every ContinuousDistribution derives from it). `error_level` returns the raw ErrorLevel byte
// value (e.g. Minor == 2), matching the fixture convention documented in
// fixtures/distributions/uniform.json. `x` is unused for has_errors/error_level.
[[cpp11::register]] double hecfda_dist_eval(std::string type, cpp11::doubles params, std::string method, double x) {
    auto dist = nd::IDistributionFactory::create(hecfda_dist_type_from_name(type),
                                                  std::vector<double>(params.begin(), params.end()));
    if (method == "pdf") return dist->pdf(x);
    if (method == "cdf") return dist->cdf(x);
    if (method == "inverse_cdf") return dist->inverse_cdf(x);
    auto* validation = dynamic_cast<hecfda::statistics::Validation*>(dist.get());
    if (validation == nullptr) {
        throw std::runtime_error("hecfda_dist_eval: distribution does not support validation");
    }
    validation->validate();
    if (method == "has_errors") return validation->has_errors() ? 1.0 : 0.0;
    if (method == "error_level") return static_cast<double>(static_cast<unsigned char>(validation->error_level()));
    throw std::invalid_argument("hecfda_dist_eval: unknown method: " + method);
}
[[cpp11::register]] double hecfda_paired_f(cpp11::doubles xs, cpp11::doubles ys, std::string method, double x) {
    pd::PairedData p(std::vector<double>(xs.begin(), xs.end()), std::vector<double>(ys.begin(), ys.end()));
    if (method == "f_inverse") return p.f_inverse(x);
    if (method == "integrate") return p.integrate();
    return p.f(x);
}
[[cpp11::register]] double hecfda_upd_sample_integrate(cpp11::doubles xs, cpp11::doubles means, cpp11::doubles sds, int seed) {
    std::vector<nd::Normal> ys;
    for (R_xlen_t i = 0; i < means.size(); ++i) ys.emplace_back(means[i], sds[i], 1);
    pd::UncertainPairedData upd(std::vector<double>(xs.begin(), xs.end()), ys);
    return upd.sample_and_integrate(seed);
}
