#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
namespace py = pybind11;
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;

static std::vector<double> rng_sequence(int seed, int n) {
    hecfda::model::compute::RandomProvider rp(seed);
    return rp.next_random_sequence(n);
}
static double normal_eval(double mean, double sd, const std::string& m, double x) {
    nd::Normal d(mean, sd, 1);
    if (m == "pdf") return d.pdf(x);
    if (m == "cdf") return d.cdf(x);
    return d.inverse_cdf(x);
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
    mod.def("normal_eval", &normal_eval);
    mod.def("paired_f", &paired_f);
    mod.def("upd_sample_integrate", &upd_sample_integrate);
}
