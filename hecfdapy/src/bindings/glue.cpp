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
#include "hecfda/model/compute/impact_area_scenario_simulation.hpp"
#include "hecfda/model/scenarios/scenario.hpp"
#include "hecfda/model/alternatives/alternative.hpp"
#include "hecfda/model/metrics/scenario_results.hpp"
#include "hecfda/model/metrics/alternative_results.hpp"
#include "hecfda/model/alternative_comparison_report/alternative_comparison_report.hpp"
namespace py = pybind11;
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;
namespace ms = hecfda::model::structures;
namespace mm = hecfda::model::metrics;
namespace sd = hecfda::model::stage_damage;

// Opaque handles (0.1.0, Task 3): the move-only metrics results types
// (ScenarioResults/AlternativeResults) can't cross the pybind11 boundary by value, so each handle
// owns one via unique_ptr and is registered as a class with no methods -- pure single-use tokens
// passed from scenario_compute() into Task 4's alternative_ead(). Reused (not re-declared) by
// Task 4.
struct ScenarioResultsHandle {
    std::unique_ptr<mm::ScenarioResults> results;
};
struct AlternativeResultsHandle {
    std::unique_ptr<mm::AlternativeResults> results;
};

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

// Bespoke dispatch for SystemPerformanceResults (Phase 5 Task 12 Python binding): reproduces
// test_fixtures.cpp's run_system_performance_results "rng_conformance" case_kind exactly -- the
// PerformanceTest.AssuranceResultStorageShould RNG-port-conformance pin, the case that proves the
// seeded DotNetRandom(1234) -> RandomProvider -> Normal InverseCDF chain reproduces the real C#
// through this metrics leaf. The "aep" and "levee" case_kinds traverse the identical binding +
// compiled core and stay validated in C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate
// only -- see .claude/CLAUDE.md's "R/Python distribution coverage scope" convention.
static double system_performance_results(int min_iterations, int max_iterations, double standard_probability,
                                          int master_seed, double threshold_value, int compute_chunks,
                                          const std::string& method) {
    using namespace hecfda::model::metrics;
    using hecfda::statistics::ConvergenceCriteria;

    ConvergenceCriteria cc(min_iterations, max_iterations);
    SystemPerformanceResults spr(cc);
    spr.add_stage_assurance_histogram(standard_probability);

    int iteration_count = cc.iteration_count();
    hecfda::sampling::DotNetRandom master_seed_list(master_seed);
    std::vector<int> seeds(static_cast<std::size_t>(iteration_count));
    for (int i = 0; i < iteration_count; ++i) {
        seeds[static_cast<std::size_t>(i)] = master_seed_list.internal_sample();
    }

    nd::Normal standard_normal(0.0, 1.0);
    for (int j = 0; j < compute_chunks; ++j) {
        for (int i = 0; i < iteration_count; ++i) {
            hecfda::model::compute::RandomProvider provider(seeds[static_cast<std::size_t>(i)]);
            double inv_cdf = standard_normal.inverse_cdf(provider.next_random());
            spr.add_stage_for_assurance(standard_probability, inv_cdf, i);
        }
        spr.put_data_into_histograms();
    }
    if (method == "assurance_of_event") return spr.assurance_of_event(standard_probability, threshold_value);
    if (method == "normal_cdf_reference") return standard_normal.cdf(threshold_value);
    throw std::invalid_argument("system_performance_results: unknown method: " + method);
}

// Bespoke dispatch for ImpactAreaScenarioSimulation (Phase 5 Task 12 Python binding, the phase's
// headline end-to-end EAD compute): reproduces test_fixtures.cpp's build_simulation/run_simulation
// for the "compute_ead" case of
// fixtures/compute/impact_area_scenario_simulation_deterministic.json -- one flow_frequency
// ContinuousDistribution (via with_flow_frequency), a two-point flow_stage UncertainPairedData (via
// with_flow_stage, reusing py_make_curve), one CurveMetaData-tagged stage_damage
// UncertainPairedData (via with_stage_damages), and an additional_threshold (always
// ThresholdEnum::DefaultExteriorStage/ConvergenceCriteria(1,1), matching that case). Only mean_eac
// (mean_expected_annual_consequences's own ConsequenceType::Damage/RiskType::Total defaults) is
// exposed -- the phase's headline oracle; the remaining simulation methods (frequency-stage
// assembly, default-threshold, mean_aep, assurance_of_event, the seeded benchmark) traverse the
// identical binding + compiled core and stay validated in C++ (core/tests/test_fixtures.cpp) + the
// dotnet oracle gate only.
// Shared construction (Phase 6 Task 12 extracted this out of impact_area_scenario_simulation's
// body, unchanged, so the scenario binding below can reuse it to fan out N of these -- one per the
// scenario binding's impact_area_ids entry).
static hecfda::model::compute::ImpactAreaScenarioSimulation py_build_impact_area_simulation(
    int impact_area_id, const std::string& flow_freq_type, std::vector<double> flow_freq_params,
    std::vector<double> flow_stage_xs, const std::vector<std::string>& flow_stage_types,
    const std::vector<std::vector<double>>& flow_stage_params, std::vector<double> stage_damage_xs,
    const std::vector<std::string>& stage_damage_types, const std::vector<std::vector<double>>& stage_damage_params,
    const std::string& damage_category, const std::string& asset_category, int threshold_id, double threshold_value) {
    using hecfda::model::compute::ImpactAreaScenarioSimulation;
    using hecfda::model::metrics::Threshold;
    using hecfda::model::metrics::ThresholdEnum;
    using hecfda::statistics::ConvergenceCriteria;

    auto builder = ImpactAreaScenarioSimulation::builder(impact_area_id);

    auto ff_dist = nd::IDistributionFactory::create(nd::distribution_type_from_name(flow_freq_type), flow_freq_params);
    auto* ff_continuous = dynamic_cast<nd::ContinuousDistribution*>(ff_dist.get());
    if (ff_continuous == nullptr) {
        throw std::runtime_error("py_build_impact_area_simulation: flow_frequency is not continuous");
    }
    ff_dist.release();
    builder.with_flow_frequency(std::unique_ptr<nd::ContinuousDistribution>(ff_continuous));

    builder.with_flow_stage(py_make_curve(std::move(flow_stage_xs), flow_stage_types, flow_stage_params));

    pd::CurveMetaData stage_damage_metadata(damage_category, asset_category);
    std::vector<std::unique_ptr<nd::IDistribution>> stage_damage_ys;
    for (std::size_t i = 0; i < stage_damage_types.size(); ++i) {
        stage_damage_ys.push_back(
            nd::IDistributionFactory::create(nd::distribution_type_from_name(stage_damage_types[i]), stage_damage_params[i]));
    }
    std::vector<pd::UncertainPairedData> stage_damage;
    stage_damage.push_back(
        pd::UncertainPairedData(std::move(stage_damage_xs), std::move(stage_damage_ys), std::move(stage_damage_metadata)));
    builder.with_stage_damages(std::move(stage_damage));

    ConvergenceCriteria threshold_cc(1, 1);
    builder.with_additional_threshold(
        Threshold(threshold_id, threshold_cc, ThresholdEnum::DefaultExteriorStage, threshold_value));

    return builder.build();
}

static double impact_area_scenario_simulation(
    int impact_area_id, const std::string& flow_freq_type, std::vector<double> flow_freq_params,
    std::vector<double> flow_stage_xs, const std::vector<std::string>& flow_stage_types,
    const std::vector<std::vector<double>>& flow_stage_params, std::vector<double> stage_damage_xs,
    const std::vector<std::string>& stage_damage_types, const std::vector<std::vector<double>>& stage_damage_params,
    const std::string& damage_category, const std::string& asset_category, int threshold_id, double threshold_value,
    int min_iterations, int max_iterations, bool compute_is_deterministic) {
    using hecfda::statistics::ConvergenceCriteria;

    auto simulation = py_build_impact_area_simulation(
        impact_area_id, flow_freq_type, std::move(flow_freq_params), std::move(flow_stage_xs), flow_stage_types,
        flow_stage_params, std::move(stage_damage_xs), stage_damage_types, stage_damage_params, damage_category,
        asset_category, threshold_id, threshold_value);
    ConvergenceCriteria cc(min_iterations, max_iterations);
    auto results = simulation.compute(cc, compute_is_deterministic);
    return results.mean_expected_annual_consequences(impact_area_id, damage_category, asset_category);
}

// Bespoke dispatch for Alternative::compute_eqad (Phase 6 Task 12 Python binding, the phase's
// headline scalar math -- the 8-row ComputeEqad oracle table in
// fixtures/alternatives/alternative.json). Mirrors test_fixtures.cpp's run_alternative
// "compute_eqad" kind exactly: args are (base_value, base_year, future_value, future_year,
// period_of_analysis, discount_rate), matching Alternative::compute_eqad's own parameter order.
// The fixture's other kind ("annualization", AlternativeResults-producing) and the rest of the
// Alternative/AlternativeComparisonReport surface traverse the identical binding + compiled core
// and stay validated in C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate only.
static double alternative_compute_eqad(double base_value, int base_year, double future_value, int future_year,
                                        int period_of_analysis, double discount_rate) {
    return hecfda::model::alternatives::Alternative::compute_eqad(base_value, base_year, future_value, future_year,
                                                                    period_of_analysis, discount_rate);
}

// Bespoke dispatch for Scenario (Phase 6 Task 12 Python binding, the impact-area fan-out
// representative for Phase 6): reproduces test_fixtures.cpp's run_scenario_compute/run_scenario
// for the "two_impact_area_fan_out" case of fixtures/scenarios/scenario.json -- N
// ImpactAreaScenarioSimulation objects (one per impact_area_ids entry, each built via
// py_build_impact_area_simulation with the SAME flow/stage/damage/threshold params, since that
// fixture's impact_areas entries are byte-identical except impact_area_id), moved into a fresh
// Scenario and computed once via Scenario::compute. Only mean_eac
// (ScenarioResults::sample_mean_expected_annual_consequences) is exposed -- consequence_type is
// never passed, relying on that method's own ConsequenceType::Damage default (matching the
// fixture's args, which are always "Damage") and RiskType::Fail default (never passed, same as
// run_scenario's own dispatch). impact_area_id may be the DEFAULT_MISSING_VALUE wildcard (-999),
// matching the fixture's third assertion.
static double scenario(std::vector<int> impact_area_ids, const std::string& flow_freq_type,
                        std::vector<double> flow_freq_params, std::vector<double> flow_stage_xs,
                        const std::vector<std::string>& flow_stage_types,
                        const std::vector<std::vector<double>>& flow_stage_params, std::vector<double> stage_damage_xs,
                        const std::vector<std::string>& stage_damage_types,
                        const std::vector<std::vector<double>>& stage_damage_params, const std::string& damage_category,
                        const std::string& asset_category, int threshold_id, double threshold_value,
                        int min_iterations, int max_iterations, bool compute_is_deterministic,
                        int query_impact_area_id) {
    using hecfda::model::compute::ImpactAreaScenarioSimulation;
    using hecfda::model::scenarios::Scenario;
    using hecfda::statistics::ConvergenceCriteria;

    std::vector<ImpactAreaScenarioSimulation> simulations;
    for (int impact_area_id : impact_area_ids) {
        simulations.push_back(py_build_impact_area_simulation(
            impact_area_id, flow_freq_type, flow_freq_params, flow_stage_xs, flow_stage_types, flow_stage_params,
            stage_damage_xs, stage_damage_types, stage_damage_params, damage_category, asset_category, threshold_id,
            threshold_value));
    }
    Scenario sc(std::move(simulations));
    ConvergenceCriteria cc(min_iterations, max_iterations);
    auto results = sc.compute(cc, compute_is_deterministic);
    return results.sample_mean_expected_annual_consequences(query_impact_area_id, damage_category, asset_category);
}

// ---- Public-API marshaling (0.1.0) ----
// Spec shapes (documented in hecfdapy/src/hecfdapy/workflow.py): dist spec = {"dist", "params"};
// curve spec = {"x", "dist", "params"[, "damage_category", "asset_category"]} with "params" a
// list of numeric vectors, one per point. These mirror the fixture `construct` schema (see
// core/tests/test_fixtures.cpp's build_simulation, the authoritative reference) so the API tests
// can drive the public functions straight from fixtures/compute/*.json.
static std::unique_ptr<nd::IDistribution> py_spec_to_dist(py::dict spec) {
    std::string type = spec["dist"].cast<std::string>();
    std::vector<double> params = spec["params"].cast<std::vector<double>>();
    return nd::IDistributionFactory::create(nd::distribution_type_from_name(type), params);
}

static pd::UncertainPairedData py_spec_to_curve(py::dict spec) {
    std::vector<double> xs = spec["x"].cast<std::vector<double>>();
    std::vector<std::string> types = spec["dist"].cast<std::vector<std::string>>();
    py::list params = spec["params"].cast<py::list>();
    std::vector<std::unique_ptr<nd::IDistribution>> ys;
    for (std::size_t i = 0; i < types.size(); ++i) {
        std::vector<double> p = params[i].cast<std::vector<double>>();
        ys.push_back(nd::IDistributionFactory::create(nd::distribution_type_from_name(types[i]), p));
    }
    if (spec.contains("damage_category")) {
        pd::CurveMetaData md(spec["damage_category"].cast<std::string>(),
                              spec["asset_category"].cast<std::string>());
        return pd::UncertainPairedData(std::move(xs), std::move(ys), std::move(md));
    }
    return pd::UncertainPairedData(std::move(xs), std::move(ys));
}

// Builds one ImpactAreaScenarioSimulation from a full simulation spec. Mirrors
// core/tests/test_fixtures.cpp's build_simulation field-for-field (that function is the
// authoritative reference for the construct schema); additional thresholds use the caller's
// convergence criteria.
static hecfda::model::compute::ImpactAreaScenarioSimulation py_spec_to_simulation(
    py::dict spec, int min_iterations, int max_iterations) {
    using hecfda::model::compute::ImpactAreaScenarioSimulation;
    using hecfda::model::metrics::Threshold;
    using hecfda::model::metrics::ThresholdEnum;
    using hecfda::statistics::ConvergenceCriteria;

    int impact_area_id = spec["impact_area_id"].cast<int>();
    auto builder = ImpactAreaScenarioSimulation::builder(impact_area_id);

    if (spec.contains("flow_frequency")) {
        auto ff = py_spec_to_dist(spec["flow_frequency"].cast<py::dict>());
        auto* cont = dynamic_cast<nd::ContinuousDistribution*>(ff.get());
        if (cont == nullptr) {
            throw std::runtime_error("ead_simulation: flow_frequency must be a continuous distribution");
        }
        ff.release();
        builder.with_flow_frequency(std::unique_ptr<nd::ContinuousDistribution>(cont));
        builder.with_flow_stage(py_spec_to_curve(spec["flow_stage"].cast<py::dict>()));
    }
    if (spec.contains("frequency_stage")) {
        py::dict fs = spec["frequency_stage"].cast<py::dict>();
        std::vector<double> probs = fs["probabilities"].cast<std::vector<double>>();
        std::vector<double> stages = fs["stages"].cast<std::vector<double>>();
        pd::CurveMetaData md(fs["damage_category"].cast<std::string>(),
                              fs["asset_category"].cast<std::string>());
        builder.with_frequency_stage(pd::GraphicalUncertainPairedData(
            probs, stages, static_cast<int>(fs["erl"].cast<double>()), md,
            /*using_stages_not_flows=*/true));
    }
    py::list sd_list = spec["stage_damage"].cast<py::list>();
    std::vector<pd::UncertainPairedData> stage_damage;
    for (auto item : sd_list) {
        stage_damage.push_back(py_spec_to_curve(item.cast<py::dict>()));
    }
    builder.with_stage_damages(std::move(stage_damage));
    if (spec.contains("levee")) {
        py::dict levee = spec["levee"].cast<py::dict>();
        builder.with_levee(py_spec_to_curve(levee), levee["top_of_levee_elevation"].cast<double>());
    }
    if (spec.contains("threshold")) {
        py::dict th = spec["threshold"].cast<py::dict>();
        ConvergenceCriteria cc(min_iterations, max_iterations);
        builder.with_additional_threshold(Threshold(th["id"].cast<int>(), cc,
                                                     ThresholdEnum::DefaultExteriorStage,
                                                     th["value"].cast<double>()));
    }
    return builder.build();
}

// Public-API EAD compute (0.1.0): build one simulation from `spec`, run the seeded Monte Carlo
// (or the deterministic path), and report mean EAD per stage-damage category pair, the
// all-category total, and the default-threshold (id 0) mean AEP.
static py::dict ead_simulation(py::dict spec, int min_iterations, int max_iterations,
                                bool compute_is_deterministic) {
    using hecfda::statistics::ConvergenceCriteria;
    auto simulation = py_spec_to_simulation(spec, min_iterations, max_iterations);
    ConvergenceCriteria cc(min_iterations, max_iterations);
    auto results = simulation.compute(cc, compute_is_deterministic);

    int impact_area_id = spec["impact_area_id"].cast<int>();
    py::list sd_list = spec["stage_damage"].cast<py::list>();
    py::list rows;
    for (auto item : sd_list) {
        py::dict curve = item.cast<py::dict>();
        std::string dc = curve["damage_category"].cast<std::string>();
        std::string ac = curve["asset_category"].cast<std::string>();
        py::dict row;
        row["damage_category"] = dc;
        row["asset_category"] = ac;
        row["mean_ead"] = results.mean_expected_annual_consequences(impact_area_id, dc, ac);
        rows.append(row);
    }
    py::dict out;
    out["ead"] = rows;
    out["total_ead"] = results.mean_expected_annual_consequences(impact_area_id);
    out["mean_aep"] = results.mean_aep(0);
    return out;
}

// Public-API scenario compute (0.1.0): N impact-area simulation specs -> one Scenario ->
// ScenarioResults. The results object is move-only and is consumed later by annualization
// (Task 4), so it is heap-allocated into a ScenarioResultsHandle and handed back as an opaque
// object (single-use: annualization moves out of the pointee; see alternative_ead's docs).
// Mirrors hecfdar/src/glue.cpp's hecfda_scenario_compute loop-for-loop.
static py::dict scenario_compute(py::list specs, int min_iterations, int max_iterations,
                                  bool compute_is_deterministic) {
    using hecfda::model::compute::ImpactAreaScenarioSimulation;
    using hecfda::model::metrics::ScenarioResults;
    using hecfda::model::scenarios::Scenario;
    using hecfda::statistics::ConvergenceCriteria;

    std::vector<ImpactAreaScenarioSimulation> sims;
    for (auto item : specs) {
        sims.push_back(py_spec_to_simulation(item.cast<py::dict>(), min_iterations, max_iterations));
    }
    Scenario scenario(std::move(sims));
    ConvergenceCriteria cc(min_iterations, max_iterations);
    std::unique_ptr<ScenarioResultsHandle> handle(new ScenarioResultsHandle{
        std::make_unique<mm::ScenarioResults>(scenario.compute(cc, compute_is_deterministic))});

    using hecfda::model::metrics::ConsequenceType;
    py::list rows;
    for (int id : handle->results->get_impact_area_ids(ConsequenceType::Damage)) {
        for (const auto& dc : handle->results->get_damage_categories()) {
            for (const auto& ac : handle->results->get_asset_categories()) {
                py::dict row;
                row["impact_area_id"] = id;
                row["damage_category"] = dc;
                row["asset_category"] = ac;
                row["mean_ead"] = handle->results->sample_mean_expected_annual_consequences(id, dc, ac);
                rows.append(row);
            }
        }
    }
    py::dict out;
    out["summary"] = rows;
    out["total_ead"] = handle->results->sample_mean_expected_annual_consequences();
    out["handle"] = py::cast(handle.release(), py::return_value_policy::take_ownership);
    return out;
}

// Public-API stage-damage (0.1.0): marshaled generalization of the fixture-literal
// impact_area_stage_damage above (same core call chain; see that function's comment).
// occupancy_types spec fields: name, damage_category, structure_curve, content_curve,
// content_to_structure_value_ratio (dict with "central" -> deterministic ctor, or
// dict with dist/std_or_min/central/max), optional first_floor_elevation / structure_value
// (dict with dist/std_or_min/max). Mirrors hecfdar/src/glue.cpp's r_spec_to_occupancy_type +
// hecfda_stage_damage.
static ms::OccupancyType py_spec_to_occupancy_type(py::dict spec) {
    auto builder = ms::OccupancyType::builder()
                       .with_name(spec["name"].cast<std::string>())
                       .with_damage_category(spec["damage_category"].cast<std::string>())
                       .with_structure_depth_percent_damage(
                           py_spec_to_curve(spec["structure_curve"].cast<py::dict>()))
                       .with_content_depth_percent_damage(
                           py_spec_to_curve(spec["content_curve"].cast<py::dict>()));
    py::dict csvr = spec["content_to_structure_value_ratio"].cast<py::dict>();
    if (!csvr.contains("dist")) {
        builder.with_content_to_structure_value_ratio(
            ms::ValueRatioWithUncertainty(csvr["central"].cast<double>()));
    } else {
        builder.with_content_to_structure_value_ratio(ms::ValueRatioWithUncertainty(
            nd::distribution_type_from_name(csvr["dist"].cast<std::string>()),
            csvr["std_or_min"].cast<double>(), csvr["central"].cast<double>(),
            csvr["max"].cast<double>()));
    }
    if (spec.contains("first_floor_elevation")) {
        py::dict ffe = spec["first_floor_elevation"].cast<py::dict>();
        builder.with_first_floor_elevation_uncertainty(ms::FirstFloorElevationUncertainty(
            nd::distribution_type_from_name(ffe["dist"].cast<std::string>()),
            ffe["std_or_min"].cast<double>(), ffe["max"].cast<double>()));
    }
    if (spec.contains("structure_value")) {
        py::dict sv = spec["structure_value"].cast<py::dict>();
        builder.with_structure_value_uncertainty(ms::ValueUncertainty(
            nd::distribution_type_from_name(sv["dist"].cast<std::string>()),
            sv["std_or_min"].cast<double>(), sv["max"].cast<double>()));
    }
    return builder.build();
}

static py::list stage_damage(py::dict structures, py::list occupancy_types, py::dict hydraulics,
                              py::dict stage_frequency, std::vector<double> stages,
                              int impact_area_id, bool compute_is_deterministic) {
    std::map<std::string, ms::OccupancyType> occ_types;
    for (auto item : occupancy_types) {
        auto occ = py_spec_to_occupancy_type(item.cast<py::dict>());
        std::string name = occ.name();
        occ_types.emplace(std::move(name), std::move(occ));
    }
    std::vector<std::string> fid = structures["fid"].cast<std::vector<std::string>>();
    std::vector<double> ffe = structures["first_floor_elevation"].cast<std::vector<double>>();
    std::vector<double> val_struct = structures["value_structure"].cast<std::vector<double>>();
    std::vector<std::string> damcat = structures["damage_category"].cast<std::vector<std::string>>();
    std::vector<std::string> occtype = structures["occupancy_type"].cast<std::vector<std::string>>();
    std::vector<double> ground = structures["ground_elevation"].cast<std::vector<double>>();
    std::size_t n = fid.size();
    auto optional_col = [&](const char* key) {
        if (structures.contains(key)) return structures[key].cast<std::vector<double>>();
        return std::vector<double>(n, 0.0);
    };
    std::vector<double> val_cont = optional_col("value_content");
    std::vector<double> val_vehic = optional_col("value_vehicle");
    std::vector<double> val_other = optional_col("value_other");
    std::vector<ms::Structure> structs;
    for (std::size_t i = 0; i < n; ++i) {
        structs.push_back(ms::Structure(fid[i], ffe[i], val_struct[i], damcat[i], occtype[i],
                                         impact_area_id, val_cont[i], val_vehic[i], val_other[i],
                                         "unassigned", ms::Structure::kDefaultMissingValue,
                                         ground[i]));
    }
    ms::Inventory inventory(std::move(occ_types), std::move(structs));

    std::vector<double> hyd_probs = hydraulics["probabilities"].cast<std::vector<double>>();
    py::list wses_list = hydraulics["wses"].cast<py::list>();
    std::vector<std::vector<float>> wses;
    for (auto item : wses_list) {
        std::vector<double> profile = item.cast<std::vector<double>>();
        wses.push_back(std::vector<float>(profile.begin(), profile.end()));
    }
    sd::HydraulicProfiles hyd(hyd_probs, wses);

    std::vector<double> freq_probs = stage_frequency["probabilities"].cast<std::vector<double>>();
    std::vector<double> freq_stages = stage_frequency["stages"].cast<std::vector<double>>();
    pd::CurveMetaData freq_md("probability", "stages", "graphical stage frequency");
    pd::GraphicalUncertainPairedData frequency(freq_probs, freq_stages,
                                                stage_frequency["erl"].cast<double>(), freq_md,
                                                /*using_stages_not_flows=*/true);

    sd::ImpactAreaStageDamage iasd(impact_area_id, std::move(inventory), std::move(hyd),
                                    /*analysis_year=*/9999, /*analytical_flow_frequency=*/nullptr,
                                    &frequency, /*discharge_stage=*/nullptr,
                                    /*unregulated_regulated=*/nullptr, /*using_mock_data=*/true);
    auto compute_result = iasd.compute(compute_is_deterministic);

    py::list rows;
    for (const auto& upd : compute_result.first) {
        auto sampled = upd.sample_paired_data(1, true);
        for (double s : stages) {
            py::dict row;
            row["damage_category"] = upd.metadata().damage_category();
            row["asset_category"] = upd.metadata().asset_category();
            row["stage"] = s;
            row["damage"] = sampled.f(s);
            rows.append(row);
        }
    }
    return rows;
}

// Public-API annualization (0.1.0, Task 4): two computed ScenarioResultsHandle objects -> EqAD
// AlternativeResults, returned as an opaque AlternativeResultsHandle. MOVES OUT of the handles'
// `results` pointees (annualization_compute's documented ownership contract) -- the Python
// wrapper documents the handles as single-use. `future.is_none()` is the single-scenario case:
// the same pointer is passed for base and future, matching the C# null-coalesce
// (`computedResultsBaseYear ??= computedResultsFutureYear`) aliasing branch. Mirrors
// hecfdar/src/glue.cpp's hecfda_annualization.
static py::dict annualization(py::object base, py::object future, double discount_rate,
                               int period_of_analysis, int alternative_id, int base_year,
                               int future_year) {
    using hecfda::model::alternatives::Alternative;

    auto& base_handle = base.cast<ScenarioResultsHandle&>();
    mm::ScenarioResults* base_ptr = base_handle.results.get();
    mm::ScenarioResults* future_ptr = base_ptr;
    if (!future.is_none()) {
        auto& future_handle = future.cast<ScenarioResultsHandle&>();
        future_ptr = future_handle.results.get();
    }
    auto result = std::make_unique<mm::AlternativeResults>(Alternative::annualization_compute(
        discount_rate, period_of_analysis, alternative_id, base_ptr, future_ptr, base_year,
        future_year));
    if (result->is_null()) {
        throw std::runtime_error(
            "alternative_ead: invalid analysis years (future_year must fall inside the period "
            "of analysis starting at base_year)");
    }
    py::dict out;
    out["mean_eqad"] = result->sample_mean_eqad();
    out["base_year_ead"] = result->sample_mean_base_year_ead();
    out["future_year_ead"] = result->sample_mean_future_year_ead();
    std::unique_ptr<AlternativeResultsHandle> handle(new AlternativeResultsHandle{std::move(result)});
    out["handle"] = py::cast(handle.release(), py::return_value_policy::take_ownership);
    return out;
}

// Public-API with/without benefits (0.1.0, Task 4): consumes (moves) the without and with
// AlternativeResultsHandle objects into compute_alternative_comparison_report, then reports the
// wildcard reduced means per with-project alternative. Mirrors hecfdar/src/glue.cpp's
// hecfda_alt_comparison.
static py::dict alt_comparison(py::object without, py::list with_handles) {
    using hecfda::model::alternative_comparison_report::AlternativeComparisonReport;

    auto& without_handle = without.cast<AlternativeResultsHandle&>();
    std::vector<mm::AlternativeResults> withs;
    std::vector<int> with_ids;
    for (auto item : with_handles) {
        auto& w = item.cast<AlternativeResultsHandle&>();
        with_ids.push_back(w.results->alternative_id());
        withs.push_back(std::move(*w.results));
    }
    auto results = AlternativeComparisonReport::compute_alternative_comparison_report(
        std::move(*without_handle.results), std::move(withs));

    py::list rows;
    for (int id : with_ids) {
        py::dict row;
        row["alternative_id"] = id;
        row["eqad_reduced"] = results.sample_mean_eqad_reduced(id);
        row["base_year_ead_reduced"] = results.sample_mean_base_year_ead_reduced(id);
        row["future_year_ead_reduced"] = results.sample_mean_future_year_ead_reduced(id);
        row["with_project_eqad"] = results.sample_mean_with_project_eqad(id);
        rows.append(row);
    }
    py::dict out;
    out["reduced"] = rows;
    out["without_base_year_ead"] = results.sample_mean_without_project_base_year_ead();
    out["without_future_year_ead"] = results.sample_mean_without_project_future_year_ead();
    return out;
}

// Public-API seeded sampling (0.1.0): see hecfdar/src/glue.cpp's hecfda_dist_sample.
static std::vector<double> dist_sample(const std::string& type, const std::vector<double>& params,
                                        int n, int seed) {
    auto dist = nd::IDistributionFactory::create(nd::distribution_type_from_name(type), params);
    hecfda::model::compute::RandomProvider rp(seed);
    std::vector<double> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) out[static_cast<std::size_t>(i)] = dist->inverse_cdf(rp.next_random());
    return out;
}

PYBIND11_MODULE(_core, mod) {
    mod.def("rng_sequence", &rng_sequence, py::arg("seed"), py::arg("n"));
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
    mod.def("system_performance_results", &system_performance_results, py::arg("min_iterations"),
             py::arg("max_iterations"), py::arg("standard_probability"), py::arg("master_seed"),
             py::arg("threshold_value"), py::arg("compute_chunks"), py::arg("method"));
    mod.def("impact_area_scenario_simulation", &impact_area_scenario_simulation, py::arg("impact_area_id"),
             py::arg("flow_freq_type"), py::arg("flow_freq_params"), py::arg("flow_stage_xs"),
             py::arg("flow_stage_types"), py::arg("flow_stage_params"), py::arg("stage_damage_xs"),
             py::arg("stage_damage_types"), py::arg("stage_damage_params"), py::arg("damage_category"),
             py::arg("asset_category"), py::arg("threshold_id"), py::arg("threshold_value"),
             py::arg("min_iterations"), py::arg("max_iterations"), py::arg("compute_is_deterministic"));
    mod.def("alternative_compute_eqad", &alternative_compute_eqad, py::arg("base_value"), py::arg("base_year"),
             py::arg("future_value"), py::arg("future_year"), py::arg("period_of_analysis"),
             py::arg("discount_rate"));
    mod.def("scenario", &scenario, py::arg("impact_area_ids"), py::arg("flow_freq_type"),
             py::arg("flow_freq_params"), py::arg("flow_stage_xs"), py::arg("flow_stage_types"),
             py::arg("flow_stage_params"), py::arg("stage_damage_xs"), py::arg("stage_damage_types"),
             py::arg("stage_damage_params"), py::arg("damage_category"), py::arg("asset_category"),
             py::arg("threshold_id"), py::arg("threshold_value"), py::arg("min_iterations"),
             py::arg("max_iterations"), py::arg("compute_is_deterministic"), py::arg("query_impact_area_id"));
    mod.def("dist_sample", &dist_sample, py::arg("dist"), py::arg("params"), py::arg("n"),
             py::arg("seed"));
    mod.def("ead_simulation", &ead_simulation, py::arg("spec"), py::arg("min_iterations"),
             py::arg("max_iterations"), py::arg("compute_is_deterministic"));
    mod.def("stage_damage", &stage_damage, py::arg("structures"), py::arg("occupancy_types"),
             py::arg("hydraulics"), py::arg("stage_frequency"), py::arg("stages"),
             py::arg("impact_area_id"), py::arg("compute_is_deterministic"));

    py::class_<ScenarioResultsHandle>(mod, "ScenarioResultsHandle");
    py::class_<AlternativeResultsHandle>(mod, "AlternativeResultsHandle");
    mod.def("scenario_compute", &scenario_compute, py::arg("specs"), py::arg("min_iterations"),
             py::arg("max_iterations"), py::arg("compute_is_deterministic"));
    mod.def("annualization", &annualization, py::arg("base"), py::arg("future"),
             py::arg("discount_rate"), py::arg("period_of_analysis"), py::arg("alternative_id"),
             py::arg("base_year"), py::arg("future_year"));
    mod.def("alt_comparison", &alt_comparison, py::arg("without"), py::arg("with_handles"));
}
