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
#include "hecfda_core/include/hecfda/statistics/distributions/deterministic.hpp"
#include "hecfda_core/include/hecfda/model/metrics/consequence_result.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/graphical_uncertain_paired_data.hpp"
#include "hecfda_core/include/hecfda/model/stage_damage/hydraulic_profiles.hpp"
#include "hecfda_core/include/hecfda/model/stage_damage/impact_area_stage_damage.hpp"
#include "hecfda_core/include/hecfda/model/compute/impact_area_scenario_simulation.hpp"
#include "hecfda_core/include/hecfda/model/scenarios/scenario.hpp"
#include "hecfda_core/include/hecfda/model/alternatives/alternative.hpp"
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;
namespace ms = hecfda::model::structures;
namespace mm = hecfda::model::metrics;
namespace sd = hecfda::model::stage_damage;

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

// Bespoke dispatch for ConsequenceResult (Phase 4 Task 9 R binding): mirrors
// core/tests/test_fixtures.cpp's run_consequence_result exactly -- builds one fresh
// ConsequenceResult from `damage_category`, replays every row of `increments` (each a numeric
// vector [structureDamage, contentDamage, vehicleDamage, otherDamage]) via increment_consequence,
// then dispatches an accessor. `equals` builds a second ConsequenceResult from
// compare_damage_category/compare_increments (empty/unused for every other method) and returns
// 1.0/0.0 for the primary object's equals(second), matching fixtures/metrics/consequence_result.json.
static mm::ConsequenceResult r_build_consequence_result(const std::string& damage_category,
                                                          cpp11::list increments) {
    mm::ConsequenceResult cr(damage_category);
    for (R_xlen_t i = 0; i < increments.size(); ++i) {
        cpp11::doubles inc(increments[i]);
        cr.increment_consequence(inc[0], inc[1], inc[2], inc[3]);
    }
    return cr;
}

[[cpp11::register]] double hecfda_consequence_result(std::string damage_category, cpp11::list increments,
                                                       std::string method, std::string compare_damage_category,
                                                       cpp11::list compare_increments) {
    auto cr = r_build_consequence_result(damage_category, increments);
    if (method == "structure_damage") return cr.structure_damage();
    if (method == "content_damage") return cr.content_damage();
    if (method == "vehicle_damage") return cr.vehicle_damage();
    if (method == "other_damage") return cr.other_damage();
    if (method == "damaged_structures_quantity") return static_cast<double>(cr.damaged_structures_quantity());
    if (method == "damaged_contents_quantity") return static_cast<double>(cr.damaged_contents_quantity());
    if (method == "damaged_vehicles_quantity") return static_cast<double>(cr.damaged_vehicles_quantity());
    if (method == "damaged_others_quantity") return static_cast<double>(cr.damaged_others_quantity());
    if (method == "equals") {
        auto cr2 = r_build_consequence_result(compare_damage_category, compare_increments);
        return cr.equals(cr2) ? 1.0 : 0.0;
    }
    throw std::invalid_argument("hecfda_consequence_result: unknown method: " + method);
}

// Bespoke construction for ImpactAreaStageDamage (Phase 4 Task 9 R binding): reproduces
// test_fixtures.cpp's make_tractable_residential_inventory/make_tractable_commercial_inventory/
// make_mock_wses_by_profile literals verbatim (see fixtures/stage_damage/impact_area_stage_damage.json's
// note for why these are fixed test literals, not fixture-carried fields).
static ms::Inventory r_make_tractable_residential_inventory(int impact_area_id) {
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

static ms::Inventory r_make_tractable_commercial_inventory(int impact_area_id) {
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

static std::vector<std::vector<float>> r_make_mock_wses_by_profile(float stage1, float stage2,
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

// Bespoke dispatch for ImpactAreaStageDamage (Phase 4 Task 9 R binding, the headline end-to-end
// stage-damage compute): mirrors core/tests/test_fixtures.cpp's "impact_area_stage_damage
// fixture" TEST_CASE body exactly -- builds a fresh damage-category-scoped tractable Inventory +
// mock HydraulicProfiles + graphical stage/flow frequency curves (all fixed literals, matching
// the fixture's construct-only-carries-scalars shape), constructs one ImpactAreaStageDamage, runs
// compute(compute_is_deterministic=true), selects the UncertainPairedData whose metadata matches
// (damage_category, asset_category), samples it deterministically, and evaluates at `stage`.
[[cpp11::register]] double hecfda_impact_area_stage_damage(int impact_area_id, std::string damage_category,
                                                             std::string asset_category, double hydraulic_stage1,
                                                             double hydraulic_stage2, bool use_reg_unreg,
                                                             double stage) {
    const std::vector<double> probabilities = {.5, .2, .1, .04, .02, .01, .004, .002};

    ms::Inventory inventory = damage_category == "Residential" ? r_make_tractable_residential_inventory(impact_area_id)
                                                                 : r_make_tractable_commercial_inventory(impact_area_id);
    auto wses_by_profile = r_make_mock_wses_by_profile(static_cast<float>(hydraulic_stage1),
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

    sd::ImpactAreaStageDamage impact_area_stage_damage(
        impact_area_id, std::move(inventory), std::move(hydraulics), /*analysis_year=*/9999,
        /*analytical_flow_frequency=*/nullptr, use_reg_unreg ? &flow_frequency : &stage_frequency,
        use_reg_unreg ? &discharge_stage : nullptr, use_reg_unreg ? &unreg_reg : nullptr,
        /*using_mock_data=*/true);

    auto compute_result = impact_area_stage_damage.compute(/*compute_is_deterministic=*/true);
    const pd::UncertainPairedData* target = nullptr;
    for (const auto& upd : compute_result.first) {
        if (upd.metadata().damage_category() == damage_category && upd.metadata().asset_category() == asset_category) {
            target = &upd;
            break;
        }
    }
    if (target == nullptr) {
        throw std::runtime_error("hecfda_impact_area_stage_damage: no matching UncertainPairedData");
    }
    auto sampled = target->sample_paired_data(1, true);
    return sampled.f(stage);
}

// Bespoke dispatch for SystemPerformanceResults (Phase 5 Task 12 R binding): reproduces
// test_fixtures.cpp's run_system_performance_results "rng_conformance" case_kind exactly -- the
// PerformanceTest.AssuranceResultStorageShould RNG-port-conformance pin, the case that proves the
// seeded DotNetRandom(1234) -> RandomProvider -> Normal InverseCDF chain reproduces the real C#
// through this metrics leaf. The "aep" and "levee" case_kinds traverse the identical binding +
// compiled core and stay validated in C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate
// only -- see .claude/CLAUDE.md's "R/Python distribution coverage scope" convention.
[[cpp11::register]] double hecfda_system_performance_results(int min_iterations, int max_iterations,
                                                                double standard_probability, int master_seed,
                                                                double threshold_value, int compute_chunks,
                                                                std::string method) {
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
    throw std::invalid_argument("hecfda_system_performance_results: unknown method: " + method);
}

// Bespoke dispatch for ImpactAreaScenarioSimulation (Phase 5 Task 12 R binding, the phase's
// headline end-to-end EAD compute): reproduces test_fixtures.cpp's build_simulation/run_simulation
// for the "compute_ead" case of
// fixtures/compute/impact_area_scenario_simulation_deterministic.json -- one flow_frequency
// ContinuousDistribution (via with_flow_frequency), a two-point flow_stage UncertainPairedData (via
// with_flow_stage, reusing r_make_curve), one CurveMetaData-tagged stage_damage
// UncertainPairedData (via with_stage_damages), and an additional_threshold (always
// ThresholdEnum::DefaultExteriorStage/ConvergenceCriteria(1,1), matching that case). Only mean_eac
// (mean_expected_annual_consequences's own ConsequenceType::Damage/RiskType::Total defaults) is
// exposed -- the phase's headline oracle; the remaining simulation methods (frequency-stage
// assembly, default-threshold, mean_aep, assurance_of_event, the seeded benchmark) traverse the
// identical binding + compiled core and stay validated in C++ (core/tests/test_fixtures.cpp) + the
// dotnet oracle gate only.
// Shared construction (Phase 6 Task 12 extracted this out of
// hecfda_impact_area_scenario_simulation's body, unchanged, so hecfda_scenario below can reuse it
// to fan out N of these -- one per Scenario Task 12 R binding's impact_area_ids entry): one
// flow_frequency ContinuousDistribution (via with_flow_frequency), a two-point flow_stage
// UncertainPairedData (via with_flow_stage, reusing r_make_curve), one CurveMetaData-tagged
// stage_damage UncertainPairedData (via with_stage_damages), and an additional_threshold (always
// ThresholdEnum::DefaultExteriorStage/ConvergenceCriteria(1,1), matching every fixture case seen
// so far).
static hecfda::model::compute::ImpactAreaScenarioSimulation r_build_impact_area_simulation(
    int impact_area_id, const std::string& flow_freq_type, cpp11::doubles flow_freq_params,
    cpp11::doubles flow_stage_xs, cpp11::strings flow_stage_types, cpp11::list flow_stage_params,
    cpp11::doubles stage_damage_xs, cpp11::strings stage_damage_types, cpp11::list stage_damage_params,
    const std::string& damage_category, const std::string& asset_category, int threshold_id,
    double threshold_value) {
    using hecfda::model::compute::ImpactAreaScenarioSimulation;
    using hecfda::model::metrics::Threshold;
    using hecfda::model::metrics::ThresholdEnum;
    using hecfda::statistics::ConvergenceCriteria;

    auto builder = ImpactAreaScenarioSimulation::builder(impact_area_id);

    auto ff_dist = nd::IDistributionFactory::create(
        nd::distribution_type_from_name(flow_freq_type),
        std::vector<double>(flow_freq_params.begin(), flow_freq_params.end()));
    auto* ff_continuous = dynamic_cast<nd::ContinuousDistribution*>(ff_dist.get());
    if (ff_continuous == nullptr) {
        throw std::runtime_error("r_build_impact_area_simulation: flow_frequency is not continuous");
    }
    ff_dist.release();
    builder.with_flow_frequency(std::unique_ptr<nd::ContinuousDistribution>(ff_continuous));

    builder.with_flow_stage(r_make_curve(flow_stage_xs, flow_stage_types, flow_stage_params));

    pd::CurveMetaData stage_damage_metadata(damage_category, asset_category);
    std::vector<std::unique_ptr<nd::IDistribution>> stage_damage_ys;
    for (R_xlen_t i = 0; i < stage_damage_types.size(); ++i) {
        cpp11::doubles p(stage_damage_params[i]);
        stage_damage_ys.push_back(nd::IDistributionFactory::create(
            nd::distribution_type_from_name(std::string(stage_damage_types[i])),
            std::vector<double>(p.begin(), p.end())));
    }
    std::vector<pd::UncertainPairedData> stage_damage;
    stage_damage.push_back(pd::UncertainPairedData(std::vector<double>(stage_damage_xs.begin(), stage_damage_xs.end()),
                                                     std::move(stage_damage_ys), std::move(stage_damage_metadata)));
    builder.with_stage_damages(std::move(stage_damage));

    ConvergenceCriteria threshold_cc(1, 1);
    builder.with_additional_threshold(
        Threshold(threshold_id, threshold_cc, ThresholdEnum::DefaultExteriorStage, threshold_value));

    return builder.build();
}

[[cpp11::register]] double hecfda_impact_area_scenario_simulation(
    int impact_area_id, std::string flow_freq_type, cpp11::doubles flow_freq_params,
    cpp11::doubles flow_stage_xs, cpp11::strings flow_stage_types, cpp11::list flow_stage_params,
    cpp11::doubles stage_damage_xs, cpp11::strings stage_damage_types, cpp11::list stage_damage_params,
    std::string damage_category, std::string asset_category, int threshold_id, double threshold_value,
    int min_iterations, int max_iterations, bool compute_is_deterministic) {
    using hecfda::statistics::ConvergenceCriteria;

    auto simulation = r_build_impact_area_simulation(
        impact_area_id, flow_freq_type, flow_freq_params, flow_stage_xs, flow_stage_types, flow_stage_params,
        stage_damage_xs, stage_damage_types, stage_damage_params, damage_category, asset_category, threshold_id,
        threshold_value);
    ConvergenceCriteria cc(min_iterations, max_iterations);
    auto results = simulation.compute(cc, compute_is_deterministic);
    return results.mean_expected_annual_consequences(impact_area_id, damage_category, asset_category);
}

// Bespoke dispatch for Alternative::compute_eqad (Phase 6 Task 12 R binding, the phase's headline
// scalar math -- the 8-row ComputeEqad oracle table in fixtures/alternatives/alternative.json).
// Mirrors test_fixtures.cpp's run_alternative "compute_eqad" kind exactly: args are (base_value,
// base_year, future_value, future_year, period_of_analysis, discount_rate), matching
// Alternative::compute_eqad's own parameter order. The fixture's other kind ("annualization",
// AlternativeResults-producing) and the rest of the Alternative/AlternativeComparisonReport surface
// traverse the identical binding + compiled core and stay validated in C++
// (core/tests/test_fixtures.cpp) + the dotnet oracle gate only.
[[cpp11::register]] double hecfda_alternative_compute_eqad(double base_value, int base_year, double future_value,
                                                              int future_year, int period_of_analysis,
                                                              double discount_rate) {
    return hecfda::model::alternatives::Alternative::compute_eqad(base_value, base_year, future_value, future_year,
                                                                     period_of_analysis, discount_rate);
}

// Bespoke dispatch for Scenario (Phase 6 Task 12 R binding, the impact-area fan-out representative
// for Phase 6): reproduces test_fixtures.cpp's run_scenario_compute/run_scenario for the
// "two_impact_area_fan_out" case of fixtures/scenarios/scenario.json -- N
// ImpactAreaScenarioSimulation objects (one per `impact_area_ids` entry, each built via
// r_build_impact_area_simulation with the SAME flow/stage/damage/threshold params, since that
// fixture's impact_areas entries are byte-identical except impact_area_id), moved into a fresh
// Scenario and computed once via Scenario::compute. Only mean_eac
// (ScenarioResults::sample_mean_expected_annual_consequences) is exposed -- consequence_type is
// never passed, relying on that method's own ConsequenceType::Damage default (matching the
// fixture's args, which are always "Damage") and RiskType::Fail default (never passed, same as
// run_scenario's own dispatch). impact_area_id may be the DEFAULT_MISSING_VALUE wildcard (-999),
// matching the fixture's third assertion.
[[cpp11::register]] double hecfda_scenario(
    cpp11::integers impact_area_ids, std::string flow_freq_type, cpp11::doubles flow_freq_params,
    cpp11::doubles flow_stage_xs, cpp11::strings flow_stage_types, cpp11::list flow_stage_params,
    cpp11::doubles stage_damage_xs, cpp11::strings stage_damage_types, cpp11::list stage_damage_params,
    std::string damage_category, std::string asset_category, int threshold_id, double threshold_value,
    int min_iterations, int max_iterations, bool compute_is_deterministic, int query_impact_area_id) {
    using hecfda::model::compute::ImpactAreaScenarioSimulation;
    using hecfda::model::scenarios::Scenario;
    using hecfda::statistics::ConvergenceCriteria;

    std::vector<ImpactAreaScenarioSimulation> simulations;
    for (R_xlen_t i = 0; i < impact_area_ids.size(); ++i) {
        simulations.push_back(r_build_impact_area_simulation(
            impact_area_ids[i], flow_freq_type, flow_freq_params, flow_stage_xs, flow_stage_types,
            flow_stage_params, stage_damage_xs, stage_damage_types, stage_damage_params, damage_category,
            asset_category, threshold_id, threshold_value));
    }
    Scenario scenario(std::move(simulations));
    ConvergenceCriteria cc(min_iterations, max_iterations);
    auto results = scenario.compute(cc, compute_is_deterministic);
    return results.sample_mean_expected_annual_consequences(query_impact_area_id, damage_category, asset_category);
}

// Public-API seeded sampling (0.1.0): quantile sampling for any factory distribution.
// sample = inverse_cdf(next_random()) over a fresh RandomProvider(seed) -- the identical chain
// every ported sampler uses (UncertainPairedData::sample_paired_data, OccupancyType::sample), so
// seeded draws are cross-language and C#-consistent by construction.
[[cpp11::register]] cpp11::doubles hecfda_dist_sample(std::string type, cpp11::doubles params,
                                                        int n, int seed) {
    auto dist = nd::IDistributionFactory::create(nd::distribution_type_from_name(type),
                                                  std::vector<double>(params.begin(), params.end()));
    hecfda::model::compute::RandomProvider rp(seed);
    cpp11::writable::doubles out(n);
    for (int i = 0; i < n; ++i) out[i] = dist->inverse_cdf(rp.next_random());
    return out;
}
