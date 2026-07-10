#include <cpp11.hpp>
#include <vector>
#include <string>
#include "hecfda_core/include/hecfda/model/compute/random_provider.hpp"
#include "hecfda_core/include/hecfda/statistics/distributions/normal.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/paired_data.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/uncertain_paired_data.hpp"
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;

[[cpp11::register]] cpp11::doubles hecfda_rng_sequence(int seed, int n) {
    hecfda::model::compute::RandomProvider rp(seed);
    auto v = rp.next_random_sequence(n);
    return cpp11::doubles(cpp11::writable::doubles(v.begin(), v.end()));
}
[[cpp11::register]] double hecfda_normal_eval(double mean, double sd, std::string method, double x) {
    nd::Normal dist(mean, sd, 1);
    if (method == "pdf") return dist.pdf(x);
    if (method == "cdf") return dist.cdf(x);
    return dist.inverse_cdf(x);
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
