#include <cpp11.hpp>
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>
#include "hecfda_core/include/hecfda/model/compute/random_provider.hpp"
#include "hecfda_core/include/hecfda/statistics/distributions/i_distribution_factory.hpp"
#include "hecfda_core/include/hecfda/statistics/distributions/normal.hpp"
#include "hecfda_core/include/hecfda/statistics/validation.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/paired_data.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda_core/include/hecfda/model/structures/value_uncertainty.hpp"
#include "hecfda_core/include/hecfda/model/structures/value_ratio_with_uncertainty.hpp"
#include "hecfda_core/include/hecfda/model/structures/first_floor_elevation_uncertainty.hpp"
#include "hecfda_core/include/hecfda/model/structures/occupancy_type.hpp"
#include "hecfda_core/include/hecfda/model/structures/structure.hpp"
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;
namespace ms = hecfda::model::structures;

[[cpp11::register]] cpp11::doubles hecfda_rng_sequence(int seed, int n) {
    hecfda::model::compute::RandomProvider rp(seed);
    auto v = rp.next_random_sequence(n);
    return cpp11::doubles(cpp11::writable::doubles(v.begin(), v.end()));
}
// Uses the shared distribution_type_from_name() from i_distribution_enum.hpp;
// removed local duplicate triplication.
// Generic factory-based distribution dispatch (Task A4): pdf/cdf/inverse_cdf evaluate directly;
// has_errors/error_level validate() first via a dynamic_cast to hecfda::statistics::Validation
// (every ContinuousDistribution derives from it). `error_level` returns the raw ErrorLevel byte
// value (e.g. Minor == 2), matching the fixture convention documented in
// fixtures/distributions/uniform.json. `x` is unused for has_errors/error_level.
[[cpp11::register]] double hecfda_dist_eval(std::string type, cpp11::doubles params, std::string method, double x) {
    auto dist = nd::IDistributionFactory::create(nd::distribution_type_from_name(type),
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
    std::vector<std::unique_ptr<nd::IDistribution>> ys;
    for (R_xlen_t i = 0; i < means.size(); ++i) ys.push_back(std::make_unique<nd::Normal>(means[i], sds[i], 1));
    pd::UncertainPairedData upd(std::vector<double>(xs.begin(), xs.end()), std::move(ys));
    return upd.sample_and_integrate(seed);
}

// Bespoke dispatch for ValueUncertainty (Phase 3 Task 7 R binding): mirrors
// core/tests/test_fixtures.cpp's run_value_uncertainty exactly (construct-then-dispatch, same two
// methods). `args` is [probability] for "sample" or [iteration, computeIsDeterministic] for
// "sample_iteration" (both plain doubles; the flag is interpreted via != 0, matching the C++
// dispatch and fixtures/structures/value_uncertainty.json's schema).
[[cpp11::register]] double hecfda_value_uncertainty(std::string dist, double std_or_min, double max,
                                                      std::string method, cpp11::doubles args) {
    ms::ValueUncertainty vu(nd::distribution_type_from_name(dist), std_or_min, max);
    if (method == "sample") return vu.sample(args[0]);
    if (method == "sample_iteration") return vu.sample(static_cast<long>(args[0]), args[1] != 0.0);
    throw std::invalid_argument("hecfda_value_uncertainty: unknown method: " + method);
}

// Builds one UncertainPairedData curve from parallel depths/types/params, mirroring
// test_fixtures.cpp's make_occupancy_type_upd(). `params` is an R list of numeric vectors, one per
// depth point (variable length per distribution type, e.g. Normal = 3, Triangular = 4).
static pd::UncertainPairedData r_make_curve(cpp11::doubles depths, cpp11::strings types, cpp11::list params) {
    std::vector<double> xs(depths.begin(), depths.end());
    std::vector<std::unique_ptr<nd::IDistribution>> ys;
    for (R_xlen_t i = 0; i < types.size(); ++i) {
        cpp11::doubles p(params[i]);
        ys.push_back(nd::IDistributionFactory::create(nd::distribution_type_from_name(std::string(types[i])),
                                                        std::vector<double>(p.begin(), p.end())));
    }
    return pd::UncertainPairedData(std::move(xs), std::move(ys));
}

// Builds the OccupancyType for the "structure" fixture target, mirroring test_fixtures.cpp's
// make_structure_occupancy_type() (struct_depths/content_depths are separate arrays, unlike the
// occupancy_type fixture target which shares one "depths" array -- see structure.json's note).
static ms::OccupancyType r_make_structure_occupancy_type(
    const std::string& name, const std::string& damage_category, cpp11::doubles struct_depths,
    cpp11::strings struct_types, cpp11::list struct_params, cpp11::doubles content_depths,
    cpp11::strings content_types, cpp11::list content_params, const std::string& ffe_dist,
    double ffe_std_or_min, double ffe_max, const std::string& sv_dist, double sv_std_or_min, double sv_max,
    const std::string& csvr_dist, double csvr_std_or_min, double csvr_central, double csvr_max) {
    auto struct_upd = r_make_curve(struct_depths, struct_types, struct_params);
    auto content_upd = r_make_curve(content_depths, content_types, content_params);
    ms::FirstFloorElevationUncertainty ffe(nd::distribution_type_from_name(ffe_dist), ffe_std_or_min, ffe_max);
    ms::ValueUncertainty sv(nd::distribution_type_from_name(sv_dist), sv_std_or_min, sv_max);
    ms::ValueRatioWithUncertainty csvr(nd::distribution_type_from_name(csvr_dist), csvr_std_or_min,
                                        csvr_central, csvr_max);
    return ms::OccupancyType::builder()
        .with_name(name)
        .with_damage_category(damage_category)
        .with_structure_depth_percent_damage(std::move(struct_upd))
        .with_content_depth_percent_damage(std::move(content_upd))
        .with_first_floor_elevation_uncertainty(ffe)
        .with_structure_value_uncertainty(sv)
        .with_content_to_structure_value_ratio(csvr)
        .build();
}

// Bespoke dispatch for Structure (Phase 3 Task 7 R binding): mirrors core/tests/test_fixtures.cpp's
// make_structure_occupancy_type() + make_structure() + run_structure() -- builds one fresh
// OccupancyType + Structure per call, samples once via
// occ.sample(sample_iteration, sample_compute_is_deterministic), and dispatches
// compute_damage_{struct,content,vehicle,other} = the four tuple items of
// Structure::compute_damage(wse, sampled). ffe_max/sv_max/csvr_max take the C# default-max sentinel
// (.Machine$double.xmax for ffe/csvr, 100 for sv, from the R fixture runner) when the fixture's
// "max" field is absent, matching `ctor.contains("max")` in the C++ dispatch.
[[cpp11::register]] double hecfda_structure(
    std::string oc_name, std::string oc_damage_category, cpp11::doubles struct_depths,
    cpp11::strings struct_types, cpp11::list struct_params, cpp11::doubles content_depths,
    cpp11::strings content_types, cpp11::list content_params, std::string ffe_dist, double ffe_std_or_min,
    double ffe_max, std::string sv_dist, double sv_std_or_min, double sv_max, std::string csvr_dist,
    double csvr_std_or_min, double csvr_central, double csvr_max, int sample_iteration,
    bool sample_compute_is_deterministic, std::string fid, double first_floor_elevation, double val_struct,
    std::string st_damcat, std::string occtype, int impact_area_id, double val_cont, double val_vehic,
    double val_other, double ground_elevation, std::string method, double wse) {
    auto occ = r_make_structure_occupancy_type(oc_name, oc_damage_category, struct_depths, struct_types,
                                                struct_params, content_depths, content_types, content_params,
                                                ffe_dist, ffe_std_or_min, ffe_max, sv_dist, sv_std_or_min,
                                                sv_max, csvr_dist, csvr_std_or_min, csvr_central, csvr_max);
    auto sampled = occ.sample(static_cast<long>(sample_iteration), sample_compute_is_deterministic);
    ms::Structure structure(fid, first_floor_elevation, val_struct, st_damcat, occtype, impact_area_id,
                             val_cont, val_vehic, val_other, "unassigned", ms::Structure::kDefaultMissingValue,
                             ground_elevation);
    auto [struct_damage, cont_damage, vehicle_damage, other_damage] =
        structure.compute_damage(static_cast<float>(wse), sampled);
    if (method == "compute_damage_struct") return struct_damage;
    if (method == "compute_damage_content") return cont_damage;
    if (method == "compute_damage_vehicle") return vehicle_damage;
    if (method == "compute_damage_other") return other_damage;
    throw std::invalid_argument("hecfda_structure: unknown method: " + method);
}
