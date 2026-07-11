#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include <string>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/i_distribution_factory.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/validation.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
namespace py = pybind11;
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;

static std::vector<double> rng_sequence(int seed, int n) {
    hecfda::model::compute::RandomProvider rp(seed);
    return rp.next_random_sequence(n);
}
// Uses the shared distribution_type_from_name() from i_distribution_enum.hpp;
// removed local duplicate triplication.
// Generic factory-based distribution dispatch (Task A4): pdf/cdf/inverse_cdf evaluate directly;
// has_errors/error_level validate() first via a dynamic_cast to hecfda::statistics::Validation.
// `error_level` returns the raw ErrorLevel byte value (e.g. Minor == 2), matching the fixture
// convention documented in fixtures/distributions/uniform.json. `x` is unused for
// has_errors/error_level.
static double dist_eval(const std::string& type, std::vector<double> params, const std::string& method,
                        double x) {
    auto dist = nd::IDistributionFactory::create(nd::distribution_type_from_name(type), params);
    if (method == "pdf") return dist->pdf(x);
    if (method == "cdf") return dist->cdf(x);
    if (method == "inverse_cdf") return dist->inverse_cdf(x);
    auto* validation = dynamic_cast<hecfda::statistics::Validation*>(dist.get());
    if (validation == nullptr) {
        throw std::runtime_error("dist_eval: distribution does not support validation");
    }
    validation->validate();
    if (method == "has_errors") return validation->has_errors() ? 1.0 : 0.0;
    if (method == "error_level") return static_cast<double>(static_cast<unsigned char>(validation->error_level()));
    throw std::invalid_argument("dist_eval: unknown method: " + method);
}
static double paired_f(std::vector<double> xs, std::vector<double> ys, const std::string& m, double x) {
    pd::PairedData p(std::move(xs), std::move(ys));
    if (m == "f_inverse") return p.f_inverse(x);
    if (m == "integrate") return p.integrate();
    return p.f(x);
}
static double upd_sample_integrate(std::vector<double> xs, std::vector<double> means,
                                   std::vector<double> sds, int seed) {
    std::vector<nd::Normal> ys;
    for (size_t i = 0; i < means.size(); ++i) ys.emplace_back(means[i], sds[i], 1);
    pd::UncertainPairedData upd(std::move(xs), ys);
    return upd.sample_and_integrate(seed);
}
PYBIND11_MODULE(_core, mod) {
    mod.def("rng_sequence", &rng_sequence);
    mod.def("dist_eval", &dist_eval);
    mod.def("paired_f", &paired_f);
    mod.def("upd_sample_integrate", &upd_sample_integrate);
}
