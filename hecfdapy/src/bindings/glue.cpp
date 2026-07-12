#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/i_distribution_factory.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/validation.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/model/structures/value_uncertainty.hpp"
#include "hecfda/model/structures/value_ratio_with_uncertainty.hpp"
#include "hecfda/model/structures/first_floor_elevation_uncertainty.hpp"
#include "hecfda/model/structures/occupancy_type.hpp"
#include "hecfda/model/structures/structure.hpp"
#include "hecfda/statistics/distributions/deterministic.hpp"
#include "hecfda/model/metrics/consequence_result.hpp"
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/graphical_uncertain_paired_data.hpp"
#include "hecfda/model/stage_damage/hydraulic_profiles.hpp"
#include "hecfda/model/stage_damage/impact_area_stage_damage.hpp"
namespace py = pybind11;
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;
namespace ms = hecfda::model::structures;
namespace mm = hecfda::model::metrics;
namespace sd = hecfda::model::stage_damage;

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
    std::vector<std::unique_ptr<nd::IDistribution>> ys;
    for (size_t i = 0; i < means.size(); ++i) ys.push_back(std::make_unique<nd::Normal>(means[i], sds[i], 1));
    pd::UncertainPairedData upd(std::move(xs), std::move(ys));
    return upd.sample_and_integrate(seed);
}

// Bespoke dispatch for ValueUncertainty (Phase 3 Task 7 Python binding): mirrors
// core/tests/test_fixtures.cpp's run_value_uncertainty exactly (construct-then-dispatch, same two
// methods). `args` is [probability] for "sample" or [iteration, computeIsDeterministic] for
// "sample_iteration" (both plain doubles; the flag is interpreted via != 0, matching the C++
// dispatch and fixtures/structures/value_uncertainty.json's schema).
static double value_uncertainty(const std::string& dist, double std_or_min, double max,
                                 const std::string& method, std::vector<double> args) {
    ms::ValueUncertainty vu(nd::distribution_type_from_name(dist), std_or_min, max);
    if (method == "sample") return vu.sample(args[0]);
    if (method == "sample_iteration") return vu.sample(static_cast<long>(args[0]), args[1] != 0.0);
    throw std::invalid_argument("value_uncertainty: unknown method: " + method);
}

// Builds one UncertainPairedData curve from parallel depths/types/params, mirroring
// test_fixtures.cpp's make_occupancy_type_upd(). `params` holds one numeric vector per depth point
// (variable length per distribution type, e.g. Normal = 3, Triangular = 4).
static pd::UncertainPairedData py_make_curve(std::vector<double> depths, const std::vector<std::string>& types,
                                              const std::vector<std::vector<double>>& params) {
    std::vector<std::unique_ptr<nd::IDistribution>> ys;
    for (std::size_t i = 0; i < types.size(); ++i) {
        ys.push_back(nd::IDistributionFactory::create(nd::distribution_type_from_name(types[i]), params[i]));
    }
    return pd::UncertainPairedData(std::move(depths), std::move(ys));
}

// Builds the OccupancyType for the "structure" fixture target, mirroring test_fixtures.cpp's
// make_structure_occupancy_type() (struct_depths/content_depths are separate arrays, unlike the
// occupancy_type fixture target which shares one "depths" array -- see structure.json's note).
static ms::OccupancyType py_make_structure_occupancy_type(
    const std::string& name, const std::string& damage_category, std::vector<double> struct_depths,
    const std::vector<std::string>& struct_types, const std::vector<std::vector<double>>& struct_params,
    std::vector<double> content_depths, const std::vector<std::string>& content_types,
    const std::vector<std::vector<double>>& content_params, const std::string& ffe_dist, double ffe_std_or_min,
    double ffe_max, const std::string& sv_dist, double sv_std_or_min, double sv_max, const std::string& csvr_dist,
    double csvr_std_or_min, double csvr_central, double csvr_max) {
    auto struct_upd = py_make_curve(std::move(struct_depths), struct_types, struct_params);
    auto content_upd = py_make_curve(std::move(content_depths), content_types, content_params);
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

// Bespoke dispatch for Structure (Phase 3 Task 7 Python binding): mirrors
// core/tests/test_fixtures.cpp's make_structure_occupancy_type() + make_structure() +
// run_structure() -- builds one fresh OccupancyType + Structure per call, samples once via
// occ.sample(sample_iteration, sample_compute_is_deterministic), and dispatches
// compute_damage_{struct,content,vehicle,other} = the four tuple items of
// Structure::compute_damage(wse, sampled). ffe_max/sv_max/csvr_max take the C# default-max
// sentinel (sys.float_info.max for ffe/csvr, 100 for sv, from the Python fixture runner) when the
// fixture's "max" field is absent, matching `ctor.contains("max")` in the C++ dispatch.
static double structure(const std::string& oc_name, const std::string& oc_damage_category,
                         std::vector<double> struct_depths, std::vector<std::string> struct_types,
                         std::vector<std::vector<double>> struct_params, std::vector<double> content_depths,
                         std::vector<std::string> content_types, std::vector<std::vector<double>> content_params,
                         const std::string& ffe_dist, double ffe_std_or_min, double ffe_max,
                         const std::string& sv_dist, double sv_std_or_min, double sv_max,
                         const std::string& csvr_dist, double csvr_std_or_min, double csvr_central,
                         double csvr_max, long sample_iteration, bool sample_compute_is_deterministic,
                         const std::string& fid, double first_floor_elevation, double val_struct,
                         const std::string& st_damcat, const std::string& occtype, int impact_area_id,
                         double val_cont, double val_vehic, double val_other, double ground_elevation,
                         const std::string& method, double wse) {
    auto occ = py_make_structure_occupancy_type(oc_name, oc_damage_category, std::move(struct_depths),
                                                 struct_types, struct_params, std::move(content_depths),
                                                 content_types, content_params, ffe_dist, ffe_std_or_min, ffe_max,
                                                 sv_dist, sv_std_or_min, sv_max, csvr_dist, csvr_std_or_min,
                                                 csvr_central, csvr_max);
    auto sampled = occ.sample(sample_iteration, sample_compute_is_deterministic);
    ms::Structure struct_obj(fid, first_floor_elevation, val_struct, st_damcat, occtype, impact_area_id,
                              val_cont, val_vehic, val_other, "unassigned", ms::Structure::kDefaultMissingValue,
                              ground_elevation);
    auto [struct_damage, cont_damage, vehicle_damage, other_damage] =
        struct_obj.compute_damage(static_cast<float>(wse), sampled);
    if (method == "compute_damage_struct") return struct_damage;
    if (method == "compute_damage_content") return cont_damage;
    if (method == "compute_damage_vehicle") return vehicle_damage;
    if (method == "compute_damage_other") return other_damage;
    throw std::invalid_argument("structure: unknown method: " + method);
}

// Bespoke dispatch for ConsequenceResult (Phase 4 Task 9 Python binding): mirrors
// core/tests/test_fixtures.cpp's run_consequence_result exactly -- builds one fresh
// ConsequenceResult from `damage_category`, replays every row of `increments` (each a 4-element
// vector [structureDamage, contentDamage, vehicleDamage, otherDamage]) via increment_consequence,
// then dispatches an accessor. `equals` builds a second ConsequenceResult from
// compare_damage_category/compare_increments (empty/unused for every other method) and returns
// 1.0/0.0 for the primary object's equals(second), matching fixtures/metrics/consequence_result.json.
static mm::ConsequenceResult py_build_consequence_result(const std::string& damage_category,
                                                           const std::vector<std::vector<double>>& increments) {
    mm::ConsequenceResult cr(damage_category);
    for (const auto& inc : increments) {
        cr.increment_consequence(inc[0], inc[1], inc[2], inc[3]);
    }
    return cr;
}

static double consequence_result(const std::string& damage_category,
                                  const std::vector<std::vector<double>>& increments, const std::string& method,
                                  const std::string& compare_damage_category,
                                  const std::vector<std::vector<double>>& compare_increments) {
    auto cr = py_build_consequence_result(damage_category, increments);
    if (method == "structure_damage") return cr.structure_damage();
    if (method == "content_damage") return cr.content_damage();
    if (method == "vehicle_damage") return cr.vehicle_damage();
    if (method == "other_damage") return cr.other_damage();
    if (method == "damaged_structures_quantity") return static_cast<double>(cr.damaged_structures_quantity());
    if (method == "damaged_contents_quantity") return static_cast<double>(cr.damaged_contents_quantity());
    if (method == "damaged_vehicles_quantity") return static_cast<double>(cr.damaged_vehicles_quantity());
    if (method == "damaged_others_quantity") return static_cast<double>(cr.damaged_others_quantity());
    if (method == "equals") {
        auto cr2 = py_build_consequence_result(compare_damage_category, compare_increments);
        return cr.equals(cr2) ? 1.0 : 0.0;
    }
    throw std::invalid_argument("consequence_result: unknown method: " + method);
}

// Bespoke construction for ImpactAreaStageDamage (Phase 4 Task 9 Python binding): reproduces
// test_fixtures.cpp's make_tractable_residential_inventory/make_tractable_commercial_inventory/
// make_mock_wses_by_profile literals verbatim (see fixtures/stage_damage/impact_area_stage_damage.json's
// note for why these are fixed test literals, not fixture-carried fields).
static ms::Inventory py_make_tractable_residential_inventory(int impact_area_id) {
    std::vector<double> depths = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<double> struct_damage_vals = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    std::vector<double> content_damage_vals = {0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95};
    auto make_deterministic_upd = [](const std::vector<double>& xs, const std::vector<double>& vals) {
        std::vector<std::unique_ptr<nd::IDistribution>> ys;
        ys.reserve(vals.size());
        for (double v : vals) ys.push_back(std::make_unique<nd::Deterministic>(v));
        return pd::UncertainPairedData(xs, std::move(ys));
    };
    ms::OccupancyType residential =
        ms::OccupancyType::builder()
            .with_name("Residential")
            .with_damage_category("Residential")
            .with_structure_depth_percent_damage(make_deterministic_upd(depths, struct_damage_vals))
            .with_content_depth_percent_damage(make_deterministic_upd(depths, content_damage_vals))
            .with_content_to_structure_value_ratio(ms::ValueRatioWithUncertainty(50))
            .build();
    std::map<std::string, ms::OccupancyType> occ_types;
    occ_types.emplace("Residential", std::move(residential));
    std::vector<ms::Structure> structures;
    structures.push_back(ms::Structure("1", /*first_floor_elevation=*/14, /*val_struct=*/100, "Residential",
                                        "Residential", impact_area_id, 0, 0, 0, "unassigned",
                                        ms::Structure::kDefaultMissingValue, /*ground_elevation=*/12));
    structures.push_back(ms::Structure("2", /*first_floor_elevation=*/15, /*val_struct=*/200, "Residential",
                                        "Residential", impact_area_id, 0, 0, 0, "unassigned",
                                        ms::Structure::kDefaultMissingValue, /*ground_elevation=*/12));
    return ms::Inventory(std::move(occ_types), std::move(structures));
}

static ms::Inventory py_make_tractable_commercial_inventory(int impact_area_id) {
    std::vector<double> depths = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<double> struct_damage_vals = {0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95};
    std::vector<double> content_damage_vals = {0, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90};
    auto make_deterministic_upd = [](const std::vector<double>& xs, const std::vector<double>& vals) {
        std::vector<std::unique_ptr<nd::IDistribution>> ys;
        ys.reserve(vals.size());
        for (double v : vals) ys.push_back(std::make_unique<nd::Deterministic>(v));
        return pd::UncertainPairedData(xs, std::move(ys));
    };
    ms::OccupancyType commercial =
        ms::OccupancyType::builder()
            .with_name("Commercial")
            .with_damage_category("Commercial")
            .with_structure_depth_percent_damage(make_deterministic_upd(depths, struct_damage_vals))
            .with_content_depth_percent_damage(make_deterministic_upd(depths, content_damage_vals))
            .with_content_to_structure_value_ratio(ms::ValueRatioWithUncertainty(120))
            .build();
    std::map<std::string, ms::OccupancyType> occ_types;
    occ_types.emplace("Commercial", std::move(commercial));
    std::vector<ms::Structure> structures;
    structures.push_back(ms::Structure("3", /*first_floor_elevation=*/17, /*val_struct=*/300, "Commercial",
                                        "Commercial", impact_area_id, 0, 0, 0, "unassigned",
                                        ms::Structure::kDefaultMissingValue, /*ground_elevation=*/12));
    structures.push_back(ms::Structure("4", /*first_floor_elevation=*/18, /*val_struct=*/400, "Commercial",
                                        "Commercial", impact_area_id, 0, 0, 0, "unassigned",
                                        ms::Structure::kDefaultMissingValue, /*ground_elevation=*/12));
    return ms::Inventory(std::move(occ_types), std::move(structures));
}

static std::vector<std::vector<float>> py_make_mock_wses_by_profile(float stage1, float stage2,
                                                                      std::size_t profile_count) {
    std::vector<std::vector<float>> wses;
    wses.reserve(profile_count);
    wses.push_back({stage1, stage2});
    for (std::size_t i = 1; i < profile_count; ++i) {
        const auto& previous = wses.back();
        wses.push_back({previous[0] + 1.0f, previous[1] + 1.0f});
    }
    return wses;
}

// Bespoke dispatch for ImpactAreaStageDamage (Phase 4 Task 9 Python binding, the headline
// end-to-end stage-damage compute): mirrors core/tests/test_fixtures.cpp's
// "impact_area_stage_damage fixture" TEST_CASE body exactly -- builds a fresh
// damage-category-scoped tractable Inventory + mock HydraulicProfiles + graphical stage/flow
// frequency curves (all fixed literals, matching the fixture's construct-only-carries-scalars
// shape), constructs one ImpactAreaStageDamage, runs compute(compute_is_deterministic=true),
// selects the UncertainPairedData whose metadata matches (damage_category, asset_category),
// samples it deterministically, and evaluates at `stage`.
static double impact_area_stage_damage(int impact_area_id, const std::string& damage_category,
                                        const std::string& asset_category, double hydraulic_stage1,
                                        double hydraulic_stage2, bool use_reg_unreg, double stage) {
    const std::vector<double> probabilities = {.5, .2, .1, .04, .02, .01, .004, .002};

    ms::Inventory inventory = damage_category == "Residential"
                                   ? py_make_tractable_residential_inventory(impact_area_id)
                                   : py_make_tractable_commercial_inventory(impact_area_id);
    auto wses_by_profile = py_make_mock_wses_by_profile(static_cast<float>(hydraulic_stage1),
                                                          static_cast<float>(hydraulic_stage2), probabilities.size());
    sd::HydraulicProfiles hydraulics(probabilities, wses_by_profile);

    std::vector<double> graphical_stages = {12, 13, 14, 15, 16, 17, 18, 19};
    pd::CurveMetaData stage_frequency_md("probability", "stages", "graphical stage frequency");
    pd::GraphicalUncertainPairedData stage_frequency(probabilities, graphical_stages, /*erl=*/50, stage_frequency_md,
                                                       /*using_stages_not_flows=*/true);

    std::vector<double> inflows = {1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900};
    pd::CurveMetaData flow_frequency_md("probability", "discharge", "graphical flow frequency");
    pd::GraphicalUncertainPairedData flow_frequency(probabilities, inflows, /*erl=*/50, flow_frequency_md,
                                                      /*using_stages_not_flows=*/false);

    std::vector<double> outflow_vals = {120, 130, 140, 150, 160, 170, 180, 190};
    std::vector<std::unique_ptr<nd::IDistribution>> outflow_ys;
    for (double v : outflow_vals) outflow_ys.push_back(std::make_unique<nd::Deterministic>(v));
    pd::UncertainPairedData unreg_reg(inflows, std::move(outflow_ys));

    std::vector<double> flows = {120, 130, 140, 150, 160, 170, 180, 190};
    std::vector<double> stage_vals = {12, 13, 14, 15, 16, 17, 18, 19};
    std::vector<std::unique_ptr<nd::IDistribution>> stage_ys;
    for (double v : stage_vals) stage_ys.push_back(std::make_unique<nd::Deterministic>(v));
    pd::UncertainPairedData discharge_stage(flows, std::move(stage_ys));

    sd::ImpactAreaStageDamage impact_area_stage_damage_obj(
        impact_area_id, std::move(inventory), std::move(hydraulics), /*analysis_year=*/9999,
        /*analytical_flow_frequency=*/nullptr, use_reg_unreg ? &flow_frequency : &stage_frequency,
        use_reg_unreg ? &discharge_stage : nullptr, use_reg_unreg ? &unreg_reg : nullptr,
        /*using_mock_data=*/true);

    auto compute_result = impact_area_stage_damage_obj.compute(/*compute_is_deterministic=*/true);
    const pd::UncertainPairedData* target = nullptr;
    for (const auto& upd : compute_result.first) {
        if (upd.metadata().damage_category() == damage_category && upd.metadata().asset_category() == asset_category) {
            target = &upd;
            break;
        }
    }
    if (target == nullptr) {
        throw std::runtime_error("impact_area_stage_damage: no matching UncertainPairedData");
    }
    auto sampled = target->sample_paired_data(1, true);
    return sampled.f(stage);
}

PYBIND11_MODULE(_core, mod) {
    mod.def("rng_sequence", &rng_sequence);
    mod.def("dist_eval", &dist_eval);
    mod.def("paired_f", &paired_f);
    mod.def("upd_sample_integrate", &upd_sample_integrate);
    mod.def("value_uncertainty", &value_uncertainty);
    mod.def("structure", &structure, py::arg("oc_name"), py::arg("oc_damage_category"),
             py::arg("struct_depths"), py::arg("struct_types"), py::arg("struct_params"),
             py::arg("content_depths"), py::arg("content_types"), py::arg("content_params"),
             py::arg("ffe_dist"), py::arg("ffe_std_or_min"), py::arg("ffe_max"), py::arg("sv_dist"),
             py::arg("sv_std_or_min"), py::arg("sv_max"), py::arg("csvr_dist"), py::arg("csvr_std_or_min"),
             py::arg("csvr_central"), py::arg("csvr_max"), py::arg("sample_iteration"),
             py::arg("sample_compute_is_deterministic"), py::arg("fid"), py::arg("first_floor_elevation"),
             py::arg("val_struct"), py::arg("st_damcat"), py::arg("occtype"), py::arg("impact_area_id"),
             py::arg("val_cont"), py::arg("val_vehic"), py::arg("val_other"), py::arg("ground_elevation"),
             py::arg("method"), py::arg("wse"));
    mod.def("consequence_result", &consequence_result, py::arg("damage_category"), py::arg("increments"),
             py::arg("method"), py::arg("compare_damage_category"), py::arg("compare_increments"));
    mod.def("impact_area_stage_damage", &impact_area_stage_damage, py::arg("impact_area_id"),
             py::arg("damage_category"), py::arg("asset_category"), py::arg("hydraulic_stage1"),
             py::arg("hydraulic_stage2"), py::arg("use_reg_unreg"), py::arg("stage"));
}
