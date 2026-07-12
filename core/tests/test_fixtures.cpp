#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include "doctest.h"
#include "json.hpp"
#include "check.hpp"
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/model/extensions/graphical_distribution.hpp"
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/assurance_result_storage.hpp"
#include "hecfda/model/metrics/categoried_paired_data.hpp"
#include "hecfda/model/metrics/categoried_uncertain_paired_data.hpp"
#include "hecfda/model/metrics/consequence_extensions.hpp"
#include "hecfda/model/metrics/consequence_result.hpp"
#include "hecfda/model/metrics/impact_area_scenario_results.hpp"
#include "hecfda/model/metrics/performance_by_thresholds.hpp"
#include "hecfda/model/metrics/study_area_consequences_binned.hpp"
#include "hecfda/model/metrics/system_performance_results.hpp"
#include "hecfda/model/metrics/threshold_enum.hpp"
#include "hecfda/model/paired_data/graphical_uncertain_paired_data.hpp"
#include "hecfda/model/paired_data/interpolate_quantiles.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/sampling/dotnet_random.hpp"
#include "hecfda/model/stage_damage/hydraulic_profiles.hpp"
#include "hecfda/model/stage_damage/impact_area_stage_damage.hpp"
#include "hecfda/model/stage_damage/scenario_stage_damage.hpp"
#include "hecfda/model/structures/deterministic_occupancy_type.hpp"
#include "hecfda/model/structures/first_floor_elevation_uncertainty.hpp"
#include "hecfda/model/structures/inventory.hpp"
#include "hecfda/model/structures/occupancy_type.hpp"
#include "hecfda/model/structures/structure.hpp"
#include "hecfda/model/structures/value_ratio_with_uncertainty.hpp"
#include "hecfda/model/structures/value_uncertainty.hpp"
#include "hecfda/model/utilities/graphical_frequency_uncertainty_calculators.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
#include "hecfda/statistics/distributions/continuous_distribution_extensions.hpp"
#include "hecfda/statistics/distributions/deterministic.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
#include "hecfda/statistics/distributions/i_distribution_factory.hpp"
#include "hecfda/statistics/distributions/logpearson3.hpp"
#include "hecfda/statistics/distributions/lognormal.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/pearson3.hpp"
#include "hecfda/statistics/distributions/shifted_gamma.hpp"
#include "hecfda/statistics/distributions/triangular.hpp"
#include "hecfda/statistics/distributions/truncated_normal.hpp"
#include "hecfda/statistics/distributions/uncertain_to_deterministic_converter.hpp"
#include "hecfda/statistics/distributions/uniform.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
#include "hecfda/statistics/special_functions.hpp"
#include "hecfda/statistics/validation.hpp"

using json = nlohmann::json;

// Resolve the fixtures dir: env override or the in-repo default.
static std::string fixtures_dir() {
    if (const char* e = std::getenv("HECFDA_FIXTURES")) return e;
    return "../../fixtures";  // relative to core/build/<cfg>
}

static std::vector<double> run_rng(const json& c, const std::string& method, const json& args) {
    hecfda::model::compute::RandomProvider rp(c["construct"]["seed"].get<int>());
    if (method == "next_random_sequence") return rp.next_random_sequence(args[0].get<long>());
    auto msg = std::string("unknown rng method: ") + method;
    FAIL(msg.c_str());
    return {};
}

static double run_rng_digest(const json& c, const std::string& method, const json& args) {
    hecfda::model::compute::RandomProvider rp(c["construct"]["seed"].get<int>());
    if (method == "sum_random_sequence") {
        auto seq = rp.next_random_sequence(args[0].get<long>());
        double sum = 0.0;
        for (double v : seq) sum += v;
        return sum;
    }
    auto msg = std::string("unknown rng digest method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("dotnet_random fixture") {
    std::ifstream f(fixtures_dir() + "/sampling/dotnet_random.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "dotnet_random");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_rng(c, a["method"], a["args"]);
            std::vector<double> exp = a["expected"].get<std::vector<double>>();
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for mode: ") + mode;
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("rng_digest fixture") {
    std::ifstream f(fixtures_dir() + "/sampling/rng_digest.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "rng_digest");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_rng_digest(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for mode: ") + mode;
                FAIL(msg.c_str());
            }
        }
    }
}

// Generic distribution dispatch (Task A4): maps the fixture's `construct: {type, params}` to a
// DistributionType, constructs via IDistributionFactory::create, and dispatches
// pdf|cdf|inverse_cdf|has_errors|error_level|fit_<param>. This is the "enabling" dispatch every
// later distribution task's fixture reuses -- adding a distribution is "add a factory case + a
// fixture", not a new dispatch function.
// Uses the shared distribution_type_from_name() from i_distribution_enum.hpp;
// removed local duplicate triplication. For fixtures, the exception is converted to a test
// failure via TEST_EXCEPTION or caught separately as needed.

// `error_level` is returned as the raw ErrorLevel byte value (e.g. Minor == 2), not a string --
// see fixtures/distributions/uniform.json / README.md for the numeric convention. `fit_<param>`
// methods treat `args` as the raw data array to fit against (rather than args[0] == x, used by
// pdf/cdf/inverse_cdf), and dispatch the fitted parameter by the *original* type name since
// IDistribution::fit's return type varies by concrete distribution.
static double run_distribution(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    std::string type_name = ctor["type"].get<std::string>();
    std::vector<double> params = ctor["params"].get<std::vector<double>>();
    auto dist = hecfda::statistics::distributions::IDistributionFactory::create(
        hecfda::statistics::distributions::distribution_type_from_name(type_name), params);

    if (method == "pdf") return dist->pdf(args[0].get<double>());
    if (method == "cdf") return dist->cdf(args[0].get<double>());
    if (method == "inverse_cdf") return dist->inverse_cdf(args[0].get<double>());

    // Task D1: UncertainToDeterministicDistributionConverter. `args` is unused (matches the C#
    // method's single-argument `IDistribution` signature); returns the resulting Deterministic's
    // Value as the dispatched scalar result.
    if (method == "convert_to_deterministic") {
        auto det = hecfda::statistics::distributions::convert_distribution_to_deterministic(*dist);
        return det.value();
    }

    if (method == "has_errors" || method == "error_level") {
        auto* validation = dynamic_cast<hecfda::statistics::Validation*>(dist.get());
        REQUIRE(validation != nullptr);
        validation->validate();
        if (method == "has_errors") return validation->has_errors() ? 1.0 : 0.0;
        return static_cast<double>(static_cast<unsigned char>(validation->error_level()));
    }

    if (method.rfind("fit_", 0) == 0) {
        std::vector<double> data = args.get<std::vector<double>>();
        auto fitted = dist->fit(data);
        std::string param = method.substr(4);
        if (param == "sample_size") return static_cast<double>(fitted->sample_size());
        // Matches the emitter's fit_ demux (tools/oracle_emitter/Program.cs EvalDistribution): the
        // same six IDistribution concrete types (Normal/Uniform/Triangular/Deterministic/LogNormal/
        // TruncatedNormal) support fit_<param> against real C#, so the port dispatches the same set.
        // Unlike C#'s `TruncatedNormal : Normal`, the C++ TruncatedNormal does NOT derive from
        // Normal (see truncated_normal.hpp), so there is no dynamic_cast ambiguity/footgun ordering
        // requirement here -- each branch is independent.
        if (type_name == "Normal") {
            auto* n = dynamic_cast<hecfda::statistics::distributions::Normal*>(fitted.get());
            REQUIRE(n != nullptr);
            if (param == "mean") return n->mean();
            if (param == "standard_deviation") return n->standard_deviation();
        } else if (type_name == "Uniform") {
            auto* u = dynamic_cast<hecfda::statistics::distributions::Uniform*>(fitted.get());
            REQUIRE(u != nullptr);
            if (param == "min") return u->min();
            if (param == "max") return u->max();
        } else if (type_name == "Triangular") {
            auto* t = dynamic_cast<hecfda::statistics::distributions::Triangular*>(fitted.get());
            REQUIRE(t != nullptr);
            if (param == "min") return t->min();
            if (param == "max") return t->max();
            if (param == "most_likely") return t->most_likely();
        } else if (type_name == "Deterministic") {
            auto* d = dynamic_cast<hecfda::statistics::distributions::Deterministic*>(fitted.get());
            REQUIRE(d != nullptr);
            if (param == "value") return d->value();
        } else if (type_name == "LogNormal") {
            auto* ln = dynamic_cast<hecfda::statistics::distributions::LogNormal*>(fitted.get());
            REQUIRE(ln != nullptr);
            if (param == "mean") return ln->mean();
            if (param == "standard_deviation") return ln->standard_deviation();
        } else if (type_name == "TruncatedNormal") {
            auto* tn = dynamic_cast<hecfda::statistics::distributions::TruncatedNormal*>(fitted.get());
            REQUIRE(tn != nullptr);
            if (param == "mean") return tn->mean();
            if (param == "standard_deviation") return tn->standard_deviation();
            if (param == "min") return tn->min();
            if (param == "max") return tn->max();
        }
        auto msg = std::string("unknown fit param: ") + param + " for type " + type_name;
        FAIL(msg.c_str());
        return 0.0;
    }

    auto msg = std::string("unknown distribution method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("normal fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/normal.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for mode: ") + mode;
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("uniform fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/uniform.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("triangular fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/triangular.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("lognormal fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/lognormal.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("truncated_normal fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/truncated_normal.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("truncated_lognormal fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/truncated_lognormal.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("logpearson3 fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/logpearson3.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("truncated_logpearson3 fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/truncated_logpearson3.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

TEST_CASE("deterministic fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/deterministic.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Task D1: UncertainToDeterministicDistributionConverter. Reuses the generic `distribution`
// target/run_distribution dispatch (construct is `{type, params}`, same as normal.json/
// uniform.json/etc.) via the `convert_to_deterministic` method added above -- one case each for
// Normal/Uniform/Triangular/LogPearsonIII/LogNormal/Deterministic. Empirical's converter case is
// NOT here -- Empirical takes the bespoke two-array construct, which doesn't fit this generic
// `{type, params}` shape -- it is instead covered by run_empirical's own `convert_to_deterministic`
// branch + a case in fixtures/distributions/empirical.json.
TEST_CASE("converter fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/converter.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "distribution");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_distribution(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for ShiftedGamma (Task B9+B10): unlike run_distribution, ShiftedGamma is a
// plain helper class -- not an IDistribution -- so it is constructed directly rather than via
// IDistributionFactory::create. `construct.params` is [alpha, beta, shift] matching the
// ShiftedGamma(alpha, beta, shift) ctor. Follow this same bespoke-target pattern (dedicated
// run_* function + TEST_CASE, no factory/enum entry) for other internal, non-IDistribution helper
// classes such as PearsonIII's other building blocks.
static double run_shifted_gamma(const json& c, const std::string& method, const json& args) {
    std::vector<double> params = c["construct"]["params"].get<std::vector<double>>();
    hecfda::statistics::distributions::ShiftedGamma dist(params[0], params[1], params[2]);
    if (method == "pdf") return dist.pdf(args[0].get<double>());
    if (method == "cdf") return dist.cdf(args[0].get<double>());
    if (method == "inverse_cdf") return dist.inverse_cdf(args[0].get<double>());
    auto msg = std::string("unknown shifted_gamma method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("shifted_gamma fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/shifted_gamma.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "shifted_gamma");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_shifted_gamma(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for PearsonIII (Task B6): like ShiftedGamma, PearsonIII is a plain helper
// class -- not an IDistribution -- so it is constructed directly rather than via
// IDistributionFactory::create. `construct.params` is [mean, sd, skew, n] matching the
// PearsonIII(mean, sd, skew, n) ctor.
static double run_pearson3(const json& c, const std::string& method, const json& args) {
    std::vector<double> params = c["construct"]["params"].get<std::vector<double>>();
    hecfda::statistics::distributions::PearsonIII dist(params[0], params[1], params[2],
                                                         static_cast<long>(params[3]));
    if (method == "pdf") return dist.pdf(args[0].get<double>());
    if (method == "cdf") return dist.cdf(args[0].get<double>());
    if (method == "inverse_cdf") return dist.inverse_cdf(args[0].get<double>());
    auto msg = std::string("unknown pearson3 method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("pearson3 fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/pearson3.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "pearson3");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_pearson3(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for Empirical (Task B11): Empirical takes TWO parallel arrays (cumulative
// probabilities + values), which doesn't fit the scalar-`params` factory `create()` shape every
// other run_distribution fixture uses -- so, like ShiftedGamma/PearsonIII, it is constructed
// directly here. `construct` is `{"probabilities": [...], "values": [...]}` rather than a `params`
// array. `mean`/`median`/`min`/`max`/`standard_deviation` take no args (dispatch ignores `args`).
static double run_empirical(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    hecfda::statistics::distributions::Empirical dist(ctor["probabilities"].get<std::vector<double>>(),
                                                        ctor["values"].get<std::vector<double>>());
    if (method == "pdf") return dist.pdf(args[0].get<double>());
    if (method == "cdf") return dist.cdf(args[0].get<double>());
    if (method == "inverse_cdf") return dist.inverse_cdf(args[0].get<double>());
    if (method == "mean") return dist.mean();
    if (method == "median") return dist.median();
    if (method == "min") return dist.min();
    if (method == "max") return dist.max();
    if (method == "standard_deviation") return dist.standard_deviation();
    // Task D1: UncertainToDeterministicDistributionConverter's Empirical case (`Deterministic
    // (SampleMean)`). SampleMean is a settable property that both C# ctors leave at its default
    // (0.0) -- neither Empirical(probs, values) ctor overload assigns it -- so this always
    // reproduces Deterministic(0.0) for a freshly-constructed instance, matching real C# exactly.
    if (method == "convert_to_deterministic") {
        auto det = hecfda::statistics::distributions::convert_distribution_to_deterministic(dist);
        return det.value();
    }
    auto msg = std::string("unknown empirical method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("empirical fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/empirical.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "empirical");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_empirical(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for ConvergenceCriteria (Task C1): a plain Validation-derived parameter
// holder, not an IDistribution, so it is constructed directly here rather than via
// IDistributionFactory::create -- same bespoke-target pattern as ShiftedGamma/PearsonIII/
// Empirical. `construct.params` is [minIterations, maxIterations, zAlpha, tolerance] matching
// the ConvergenceCriteria(minIterations, maxIterations, zAlpha, tolerance) ctor; the first two
// are passed as doubles in the fixture (JSON has no int type) and truncated to int here. The
// scalar accessors take no args; has_errors/error_level call validate() first, same convention
// as run_distribution's has_errors/error_level branch.
static double run_convergence_criteria(const json& c, const std::string& method, const json& args) {
    (void)args;
    std::vector<double> params = c["construct"]["params"].get<std::vector<double>>();
    hecfda::statistics::ConvergenceCriteria cc(static_cast<int>(params[0]), static_cast<int>(params[1]),
                                                params[2], params[3]);
    if (method == "min_iterations") return cc.min_iterations();
    if (method == "max_iterations") return cc.max_iterations();
    if (method == "z_alpha") return cc.z_alpha();
    if (method == "tolerance") return cc.tolerance();
    if (method == "iteration_count") return cc.iteration_count();
    if (method == "has_errors" || method == "error_level") {
        cc.validate();
        if (method == "has_errors") return cc.has_errors() ? 1.0 : 0.0;
        return static_cast<double>(static_cast<unsigned char>(cc.error_level()));
    }
    auto msg = std::string("unknown convergence_criteria method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("convergence_criteria fixture") {
    std::ifstream f(fixtures_dir() + "/convergence/convergence_criteria.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "convergence_criteria");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_convergence_criteria(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for DynamicHistogram (Task C2): the Monte Carlo result accumulator. Like
// ShiftedGamma/PearsonIII/Empirical/ConvergenceCriteria, it is constructed directly here rather
// than via IDistributionFactory::create -- `construct` is `{"bin_width": w, "data": [...],
// "added": [...]}`, mirroring the C# HistogramTests.cs pattern: `new DynamicHistogram(binWidth,
// new ConvergenceCriteria())`, then `AddObservationsToHistogram(data)`, then one
// `AddObservationToHistogram(x)` per x in `added` (the "AddedData" test variants). Accessors take
// no args; pdf/cdf/inverse_cdf take one scalar from `args`. `mean` -> HistogramMean() (mean from
// the binned histogram); `sample_mean` -> SampleMean (raw running mean); `standard_deviation` ->
// StandardDeviation (raw running); `histogram_standard_deviation` -> HistogramStandardDeviation().
static double run_histogram(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    hecfda::statistics::histograms::DynamicHistogram hist(ctor["bin_width"].get<double>(),
                                                          hecfda::statistics::ConvergenceCriteria());
    hist.add_observations_to_histogram(ctor["data"].get<std::vector<double>>());
    if (ctor.contains("added")) {
        for (const auto& x : ctor["added"]) hist.add_observation_to_histogram(x.get<double>());
    }
    if (method == "min") return hist.min();
    if (method == "max") return hist.max();
    if (method == "sample_mean") return hist.sample_mean();
    if (method == "mean") return hist.histogram_mean();
    if (method == "histogram_standard_deviation") return hist.histogram_standard_deviation();
    if (method == "standard_deviation") return hist.standard_deviation();
    if (method == "pdf") return hist.pdf(args[0].get<double>());
    if (method == "cdf") return hist.cdf(args[0].get<double>());
    if (method == "inverse_cdf") return hist.inverse_cdf(args[0].get<double>());
    auto msg = std::string("unknown histogram method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("histogram fixture") {
    std::ifstream f(fixtures_dir() + "/histograms/dynamic_histogram.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "histogram");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_histogram(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

static double run_paired_data(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    hecfda::model::paired_data::PairedData pd(ctor["xs"].get<std::vector<double>>(),
                                               ctor["ys"].get<std::vector<double>>());
    if (method == "f") return pd.f(args[0].get<double>());
    if (method == "f_inverse") return pd.f_inverse(args[0].get<double>());
    if (method == "integrate") {
        bool with_padding = args.empty() ? true : (args[0].get<double>() != 0.0);
        return pd.integrate(with_padding);
    }
    auto msg = std::string("unknown paired_data method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("paired_data fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/paired_data.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "paired_data");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_paired_data(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for mode: ") + mode;
                FAIL(msg.c_str());
            }
        }
    }
}

// Regression coverage for the .NET Array.BinarySearch fidelity fix (dotnet_binary_search.hpp):
// exercises PairedData::f/f_inverse on curves with duplicate x (flat frequency) / duplicate y
// (flat damage) segments, where std::lower_bound (first equal element) and Array.BinarySearch
// (midpoint-driven match) can pick different indices. Values pinned from the real C# gate.
TEST_CASE("paired_data duplicate_values fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/duplicate_values.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "paired_data");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_paired_data(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Dispatch for the compose/SumYsForGivenX/multiply/monotonicity/sort/f(x,ref) surface added on
// top of the scalar-returning run_paired_data() above (paired_data_ops.json). Every case
// constructs a fresh PairedData from `construct`, and -- for the two-curve ops -- a fresh second
// PairedData from `input`; mutation methods (force_*/sort_to_increasing_x_vals) act on that fresh
// `pd` and read back xvals()/yvals() afterward, matching the fresh-construction-per-assertion
// pattern the emitter uses. Always returns a vector so compare_by_mode's "vector"/"abs" dispatch
// (driven by the fixture's own "mode" field) applies uniformly.
static std::vector<double> run_paired_data_ops(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    hecfda::model::paired_data::PairedData pd(ctor["xs"].get<std::vector<double>>(),
                                               ctor["ys"].get<std::vector<double>>());
    auto make_input = [&]() {
        const auto& in = c["input"];
        return hecfda::model::paired_data::PairedData(in["xs"].get<std::vector<double>>(),
                                                        in["ys"].get<std::vector<double>>());
    };
    if (method == "compose_xvals" || method == "compose_yvals") {
        auto result = pd.compose(make_input());
        return method == "compose_xvals" ? result.xvals() : result.yvals();
    }
    if (method == "sum_ys_for_given_x_xvals" || method == "sum_ys_for_given_x_yvals") {
        auto result = pd.sum_ys_for_given_x(make_input());
        return method == "sum_ys_for_given_x_xvals" ? result.xvals() : result.yvals();
    }
    if (method == "multiply_xvals" || method == "multiply_yvals") {
        auto result = pd.multiply(make_input());
        return method == "multiply_xvals" ? result.xvals() : result.yvals();
    }
    if (method == "force_weak_monotonicity_bottom_up_yvals") {
        pd.force_weak_monotonicity_bottom_up();
        return pd.yvals();
    }
    if (method == "force_strict_monotonicity_top_down_yvals") {
        pd.force_strict_monotonicity_top_down();
        return pd.yvals();
    }
    if (method == "force_strict_monotonicity_bottom_up_yvals") {
        pd.force_strict_monotonicity_bottom_up();
        return pd.yvals();
    }
    if (method == "sort_to_increasing_x_vals_xvals" || method == "sort_to_increasing_x_vals_yvals") {
        pd.sort_to_increasing_x_vals();
        return method == "sort_to_increasing_x_vals_xvals" ? pd.xvals() : pd.yvals();
    }
    if (method == "f_ref_index") {
        int index = 0;
        return {pd.f(args[0].get<double>(), index)};
    }
    auto msg = std::string("unknown paired_data_ops method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("paired_data_ops fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/paired_data_ops.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "paired_data");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_paired_data_ops(c, a["method"], a["args"]);
            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

static double run_special_functions(const std::string& method, const json& args) {
    using SF = hecfda::statistics::SpecialFunctions;
    if (method == "log_gamma") return SF::log_gamma(args[0].get<double>());
    if (method == "log_factorial") return SF::log_factorial(args[0].get<int>());
    if (method == "gamma") return SF::gamma(args[0].get<double>());
    if (method == "factorial") return SF::factorial(args[0].get<int>());
    if (method == "incomplete_gamma") return SF::incomplete_gamma(args[0].get<double>(), args[1].get<double>());
    if (method == "incomplete_gamma_range")
        return SF::incomplete_gamma(args[0].get<double>(), args[1].get<double>(), args[2].get<double>());
    if (method == "reg_incomplete_gamma") return SF::reg_incomplete_gamma(args[0].get<double>(), args[1].get<double>());
    if (method == "log_incomplete_gamma") return SF::log_incomplete_gamma(args[0].get<double>(), args[1].get<double>());
    if (method == "digamma") return SF::digamma(args[0].get<double>());
    if (method == "log_beta") return SF::log_beta(args[0].get<double>(), args[1].get<double>());
    if (method == "beta") return SF::beta(args[0].get<double>(), args[1].get<double>());
    if (method == "incomplete_beta")
        return SF::incomplete_beta(args[0].get<double>(), args[1].get<double>(), args[2].get<double>());
    if (method == "reg_incomplete_beta")
        return SF::reg_incomplete_beta(args[0].get<double>(), args[1].get<double>(), args[2].get<double>());
    if (method == "trigamma") return SF::trigamma(args[0].get<double>());
    if (method == "single_par_gamma_pdf")
        return SF::single_par_gamma_pdf(args[0].get<double>(), args[1].get<double>());
    if (method == "gamma_derivative") return SF::gamma_derivative(args[0].get<double>(), args[1].get<double>());
    auto msg = std::string("unknown special_functions method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("special_functions fixture") {
    std::ifstream f(fixtures_dir() + "/special_functions/special_functions.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "special_functions");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_special_functions(a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for method: ") + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

static double run_sample_statistics(const json& c, const std::string& method) {
    hecfda::statistics::SampleStatistics stats(c["construct"]["data"].get<std::vector<double>>());
    if (method == "mean") return stats.mean();
    if (method == "variance") return stats.variance();
    if (method == "standard_deviation") return stats.standard_deviation();
    if (method == "median") return stats.median();
    if (method == "skewness") return stats.skewness();
    if (method == "min") return stats.min();
    if (method == "max") return stats.max();
    if (method == "sample_size") return static_cast<double>(stats.sample_size());
    auto msg = std::string("unknown sample_statistics method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("sample_statistics fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/sample_statistics.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "sample_statistics");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_sample_statistics(c, a["method"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for method: ") + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Builds the ys array from the fixture's `{type, params}` distribution specs via the shared
// IDistributionFactory (same construct shape every distribution fixture uses), generalizing the
// old Normal-only `{mean, sd}` shape. Returns a fresh, move-only UncertainPairedData.
static hecfda::model::paired_data::UncertainPairedData make_upd(const json& ctor) {
    std::vector<double> xs = ctor["xs"].get<std::vector<double>>();
    std::vector<std::unique_ptr<hecfda::statistics::distributions::IDistribution>> ys;
    for (const auto& y : ctor["ys"]) {
        ys.push_back(hecfda::statistics::distributions::IDistributionFactory::create(
            hecfda::statistics::distributions::distribution_type_from_name(y["type"].get<std::string>()),
            y["params"].get<std::vector<double>>()));
    }
    return hecfda::model::paired_data::UncertainPairedData(std::move(xs), std::move(ys));
}

static double run_uncertain_paired_data(const json& c, const std::string& method) {
    auto upd = make_upd(c["construct"]);
    if (method == "sample_and_integrate") return upd.sample_and_integrate(c["seed"].get<int>());
    auto msg = std::string("unknown uncertain_paired_data method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("uncertain_paired_data fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/uncertain_paired_data.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "uncertain_paired_data");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_uncertain_paired_data(c, a["method"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for mode: ") + mode;
                FAIL(msg.c_str());
            }
        }
    }
}

// Dispatch for the generalized sample-path surface added on top of the scalar sample_and_integrate
// above (uncertain_paired_data_ops.json). Each case builds a fresh, move-only UncertainPairedData
// from `construct` (an `{xs, ys:[{type,params}], seed?, size?}` bag); the iteration-number overload
// needs generate_random_numbers(seed, size) first, encoded in the construct's `seed`/`size` fields.
// Always returns the produced curve's yvals as a vector so compare_by_mode's "vector" dispatch
// applies uniformly (mirrors run_paired_data_ops). Fresh construction per assertion.
static std::vector<double> run_uncertain_paired_data_ops(const json& c, const std::string& method,
                                                         const json& args) {
    const auto& ctor = c["construct"];
    auto upd = make_upd(ctor);
    if (ctor.contains("seed") && ctor.contains("size")) {
        upd.generate_random_numbers(ctor["seed"].get<int>(), ctor["size"].get<long>());
    }
    if (method == "sample_paired_data") return upd.sample_paired_data(args[0].get<double>()).yvals();
    if (method == "sample_paired_data_raw")
        return upd.sample_paired_data_raw(args[0].get<double>()).yvals();
    if (method == "sample_paired_data_raw_deterministic")
        return upd.sample_paired_data_raw_deterministic().yvals();
    if (method == "sample_paired_data_iteration")
        return upd.sample_paired_data(args[0].get<long>(), args[1].get<double>() != 0.0).yvals();
    auto msg = std::string("unknown uncertain_paired_data_ops method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("uncertain_paired_data_ops fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/uncertain_paired_data_ops.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "uncertain_paired_data");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_uncertain_paired_data_ops(c, a["method"], a["args"]);
            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for InterpolateQuantiles (Task P2T4a): a plain static-method helper class,
// called directly here, same bespoke-target pattern as ShiftedGamma/PearsonIII/Empirical/
// ConvergenceCriteria. `construct` is {"input_exceedance_probabilities": [...],
// "input_data_for_interpolation": [...]}; the single method `interpolate_on_x` takes the
// "required" exceedance probabilities as its (array-valued) `args`, mirroring the emitter's
// Program.cs EvalInterpolateQuantiles.
static std::vector<double> run_interpolate_quantiles(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    std::vector<double> input_exceedance_probabilities =
        ctor["input_exceedance_probabilities"].get<std::vector<double>>();
    std::vector<double> input_data_for_interpolation = ctor["input_data_for_interpolation"].get<std::vector<double>>();
    if (method == "interpolate_on_x") {
        std::vector<double> required = args.get<std::vector<double>>();
        return hecfda::model::paired_data::InterpolateQuantiles::interpolate_on_x(
            input_exceedance_probabilities, required, input_data_for_interpolation);
    }
    auto msg = std::string("unknown interpolate_quantiles method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("interpolate_quantiles fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/interpolate_quantiles.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "interpolate_quantiles");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_interpolate_quantiles(c, a["method"], a["args"]);
            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for GraphicalFrequencyUncertaintyCalculators (Task P2T4a): a plain static-method
// helper class, called directly here like InterpolateQuantiles above. `construct` is
// {"exceedance_probabilities": [...], "stages_or_flows": [...], "using_stages_not_flows": bool,
// "equivalent_record_length": int?} (ERL defaults to 10, matching the C# default parameter and the
// emitter's EvalGraphicalCalculators). Distribution mean/standard-deviation aren't part of the
// IDistribution interface, so `distribution_means`/`distribution_standard_deviations` dynamic_cast
// each returned IDistribution to Normal or LogNormal depending on `using_stages_not_flows`
// (mirroring the emitter's `is Normal`/`is LogNormal` pattern match). `distribution_pdf_at` takes
// [index, x] and evaluates PDF(x) on the distribution at that index -- this is what actually
// discriminates Normal vs LogNormal construction (both store mean/sd identically, but interpret an
// evaluation point `x` very differently), so it is exercised even though it isn't needed to recover
// mean/sd themselves.
static std::vector<double> run_graphical_calculators(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    std::vector<double> exceedance_probabilities = ctor["exceedance_probabilities"].get<std::vector<double>>();
    std::vector<double> stages_or_flows = ctor["stages_or_flows"].get<std::vector<double>>();
    bool using_stages_not_flows = ctor["using_stages_not_flows"].get<bool>();
    int erl = ctor.contains("equivalent_record_length") ? ctor["equivalent_record_length"].get<int>() : 10;

    auto result = hecfda::model::utilities::GraphicalFrequencyUncertaintyCalculators::less_simple_method(
        exceedance_probabilities, stages_or_flows, using_stages_not_flows, erl);
    const std::vector<double>& filled_probs = result.first;
    const auto& dists = result.second;

    if (method == "filled_exceedance_probabilities") return filled_probs;

    if (method == "distribution_means" || method == "distribution_standard_deviations") {
        std::vector<double> out;
        out.reserve(dists.size());
        for (const auto& d : dists) {
            if (using_stages_not_flows) {
                auto* n = dynamic_cast<hecfda::statistics::distributions::Normal*>(d.get());
                REQUIRE(n != nullptr);
                out.push_back(method == "distribution_means" ? n->mean() : n->standard_deviation());
            } else {
                auto* ln = dynamic_cast<hecfda::statistics::distributions::LogNormal*>(d.get());
                REQUIRE(ln != nullptr);
                out.push_back(method == "distribution_means" ? ln->mean() : ln->standard_deviation());
            }
        }
        return out;
    }

    if (method == "distribution_pdf_at") {
        std::size_t index = static_cast<std::size_t>(args[0].get<int>());
        double x = args[1].get<double>();
        return {dists.at(index)->pdf(x)};
    }

    auto msg = std::string("unknown graphical_calculators method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("graphical_calculators fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/graphical_calculators.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "graphical_calculators");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_graphical_calculators(c, a["method"], a["args"]);
            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Direct guard-clause coverage transcribed from GraphicalFrequencyUncertaintyCalculatorsTests.cs's
// three PORTABLE ArgumentException cases (mismatched lengths / insufficient points / invalid ERL).
// These are exception-message-text assertions, not numeric oracle values, so -- like every other
// exception guard already ported (e.g. PairedData's "X values must be in increasing order.") --
// they are plain doctest assertions here rather than JSON fixture cases; there is no
// expected-value-to-pin for "does this throw". The C# test file's other two cases
// (LessSimpleMethod_WithNullExceedanceProbabilities/StagesOrFlows_ThrowsArgumentNullException) have
// NO analog: `const std::vector<double>&` cannot be null, so those guards are not portable -- see
// the SEVERANCE note on less_simple_method() in graphical_frequency_uncertainty_calculators.hpp.
TEST_CASE("graphical_calculators guard clauses") {
    using hecfda::model::utilities::GraphicalFrequencyUncertaintyCalculators;
    std::vector<double> mismatched_probs = {0.5, 0.1, 0.02};
    std::vector<double> mismatched_flows = {1.0, 2.0};
    CHECK_THROWS_AS(
        GraphicalFrequencyUncertaintyCalculators::less_simple_method(mismatched_probs, mismatched_flows, true),
        std::invalid_argument);

    std::vector<double> single_prob = {0.5};
    std::vector<double> single_flow = {1.0};
    CHECK_THROWS_AS(GraphicalFrequencyUncertaintyCalculators::less_simple_method(single_prob, single_flow, true),
                     std::invalid_argument);

    std::vector<double> valid_probs = {0.5, 0.1};
    std::vector<double> valid_flows = {1.0, 2.0};
    CHECK_THROWS_AS(
        GraphicalFrequencyUncertaintyCalculators::less_simple_method(valid_probs, valid_flows, true, 0),
        std::invalid_argument);
}

// Direct property-based coverage transcribed from GraphicalTests.cs
// (unittests/extensions/GraphicalTests.cs) -- GraphicalDistribution's ExceedanceProbabilities/
// StageOrLogFlowDistributions surface, checked via CONTAINS-style assertions (not exact numeric
// pins, matching the C# source's own Assert.Contains usage) rather than JSON fixture cases.
TEST_CASE("graphical_distribution properties") {
    using hecfda::model::extensions::GraphicalDistribution;
    using hecfda::statistics::distributions::Normal;

    std::vector<double> probs = {.99, .5, .1, .02, .01, .002};
    std::vector<double> flows = {500, 2000, 34900, 66900, 86000, 146000};
    GraphicalDistribution graphical(probs, flows, 10);

    // ReturnDistributionsForInputProbabilities: every input probability survives into the
    // expanded ExceedanceProbabilities.
    const auto& output_probs = graphical.exceedance_probabilities();
    for (double p : probs) {
        CHECK(std::find(output_probs.begin(), output_probs.end(), p) != output_probs.end());
    }

    // ReturnDistributionsWithInputMeanValues: every input flow survives as some distribution's
    // mean (usingStagesNotFlows defaults to true, so these are Normal, not LogNormal).
    const auto& dists = graphical.stage_or_log_flow_distributions();
    std::vector<double> means;
    means.reserve(dists.size());
    for (const auto& d : dists) {
        auto* n = dynamic_cast<Normal*>(d.get());
        REQUIRE(n != nullptr);
        means.push_back(n->mean());
    }
    for (double flow : flows) {
        CHECK(std::find(means.begin(), means.end(), flow) != means.end());
    }

    // ReturnSameNumberOfProbabilitesAsDistributions.
    CHECK(dists.size() == output_probs.size());
}

// ported from: GraphicalUncertaintyPairedDataTests.cs
// ReturnsDistributionsWhereMeanAndConfidenceLimitsAreMonotonicallyIncreasing -- for every
// probability in the "required" exceedance-probability table (Task P2T4a's
// required_exceedance_probabilities(), reused directly here), SamplePairedData(probability)'s
// Yvals must be weakly increasing (every ForceStrictMonotonicity{TopDown,BottomUp} call this
// class makes is expected to guarantee this, regardless of which branch is taken).
TEST_CASE("graphical_uncertain_paired_data yvals monotonically increasing") {
    using hecfda::model::paired_data::CurveMetaData;
    using hecfda::model::paired_data::GraphicalUncertainPairedData;

    std::vector<double> probs = {.99, .5, .1, .02, .01, .002};
    std::vector<double> flows = {500, 2000, 34900, 66900, 86000, 146000};
    CurveMetaData metadata("residential");
    GraphicalUncertainPairedData graphical(probs, flows, 5, metadata, false);

    for (double probability : hecfda::model::utilities::required_exceedance_probabilities()) {
        auto sampled = graphical.sample_paired_data(probability);
        const auto& yvals = sampled.yvals();
        for (std::size_t j = 1; j < yvals.size(); ++j) {
            CHECK(yvals[j] >= yvals[j - 1]);
        }
    }
}

// Bespoke dispatch for GraphicalUncertainPairedData (Task P2T4b): the non-parametric graphical-
// uncertainty frequency curve, built from GraphicalDistribution (extensions/graphical_
// distribution.hpp), which in turn wraps GraphicalFrequencyUncertaintyCalculators::
// less_simple_method (Task P2T4a). `construct` is {"exceedance_probabilities": [...],
// "flow_or_stage_values": [...], "equivalent_record_length": int, "using_stages_not_flows": bool,
// "seed": int?, "size": int?} -- seed/size optional, wiring generate_random_numbers() when
// present (mirrors run_uncertain_paired_data_ops's construct handling). CurveMetaData is
// irrelevant to the sampled values (same rationale as UncertainPairedData's fixture dispatch), so
// a fixed CurveMetaData("hello") is used, matching the emitter's EvalGupd. Fresh construction per
// assertion (move-only type, same convention as UncertainPairedData/GraphicalDistribution).
static hecfda::model::paired_data::GraphicalUncertainPairedData make_gupd(const json& ctor) {
    std::vector<double> exceedance_probabilities = ctor["exceedance_probabilities"].get<std::vector<double>>();
    std::vector<double> flow_or_stage_values = ctor["flow_or_stage_values"].get<std::vector<double>>();
    int erl = ctor["equivalent_record_length"].get<int>();
    bool using_stages_not_flows = ctor["using_stages_not_flows"].get<bool>();
    hecfda::model::paired_data::CurveMetaData metadata("hello");
    hecfda::model::paired_data::GraphicalUncertainPairedData gupd(exceedance_probabilities, flow_or_stage_values, erl,
                                                                   metadata, using_stages_not_flows);
    if (ctor.contains("seed") && ctor.contains("size")) {
        gupd.generate_random_numbers(ctor["seed"].get<int>(), ctor["size"].get<long>());
    }
    return gupd;
}

// Dispatch methods: "sample_paired_data"/"sample_paired_data_iteration" return the sampled
// curve's Yvals (vector mode, mirroring uncertain_paired_data_ops.json); "sample_paired_data_f"/
// "sample_paired_data_iteration_f" additionally evaluate f() at a point on that sampled curve
// (mirroring GraphicalUncertaintyPairedDataTests.cs's SamplePairedDataShould/
// DeterministicSamplingShouldReturnInputRelationship, which both read `sampledCurve.f(x)` off the
// result rather than the raw Yvals) -- wrapped in a single-element vector so compare_by_mode's
// "abs" dispatch applies uniformly.
static std::vector<double> run_gupd(const json& c, const std::string& method, const json& args) {
    auto gupd = make_gupd(c["construct"]);
    if (method == "sample_paired_data") return gupd.sample_paired_data(args[0].get<double>()).yvals();
    if (method == "sample_paired_data_iteration")
        return gupd.sample_paired_data(args[0].get<long>(), args[1].get<double>() != 0.0).yvals();
    if (method == "sample_paired_data_f")
        return {gupd.sample_paired_data(args[0].get<double>()).f(args[1].get<double>())};
    if (method == "sample_paired_data_iteration_f")
        return {gupd.sample_paired_data(args[0].get<long>(), args[1].get<double>() != 0.0).f(args[2].get<double>())};
    auto msg = std::string("unknown graphical_uncertain_paired_data method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("graphical_uncertain_paired_data fixture") {
    std::ifstream f(fixtures_dir() + "/paired_data/graphical.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "graphical_uncertain_paired_data");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_gupd(c, a["method"], a["args"]);
            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for ValueUncertainty (Phase 3 Task 1): a plain Validation-derived
// per-structure uncertainty sampler, not an IDistribution, so it is constructed directly here
// like ShiftedGamma/PearsonIII/ConvergenceCriteria. `construct` is {"dist": "<name>",
// "std_or_min": ..., "max": ...}, `dist` mapped via the shared distribution_type_from_name()
// already used by run_distribution. `sample` takes [probability]; `sample_iteration` takes
// [iteration, computeIsDeterministic] (both encoded as JSON numbers; the flag is interpreted via
// != 0).
static double run_value_uncertainty(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    auto dist_type = hecfda::statistics::distributions::distribution_type_from_name(
        ctor["dist"].get<std::string>());
    hecfda::model::structures::ValueUncertainty vu(dist_type, ctor["std_or_min"].get<double>(),
                                                     ctor["max"].get<double>());
    if (method == "sample") return vu.sample(args[0].get<double>());
    if (method == "sample_iteration")
        return vu.sample(args[0].get<long>(), args[1].get<double>() != 0.0);
    auto msg = std::string("unknown value_uncertainty method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("value_uncertainty fixture") {
    std::ifstream f(fixtures_dir() + "/structures/value_uncertainty.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "value_uncertainty");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_value_uncertainty(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for FirstFloorElevationUncertainty (Phase 3 Task 3): same shape as
// run_value_ratio_with_uncertainty above -- a plain Validation-derived per-structure uncertainty
// sampler, not an IDistribution. `construct` is {"dist": "<name>", "std_or_min": ..., "max": ...}
// (no "central" field -- the center of the distribution is hardcoded to 0 inside the class, unlike
// ValueRatioWithUncertainty). `sample` takes [probability]; `sample_iteration` takes [iteration,
// computeIsDeterministic] (both JSON numbers; the flag is interpreted via != 0).
static double run_first_floor_elevation_uncertainty(const json& c, const std::string& method,
                                                       const json& args) {
    const auto& ctor = c["construct"];
    auto dist_type = hecfda::statistics::distributions::distribution_type_from_name(
        ctor["dist"].get<std::string>());
    hecfda::model::structures::FirstFloorElevationUncertainty ffeu(
        dist_type, ctor["std_or_min"].get<double>(), ctor["max"].get<double>());
    if (method == "sample") return ffeu.sample(args[0].get<double>());
    if (method == "sample_iteration")
        return ffeu.sample(args[0].get<long>(), args[1].get<double>() != 0.0);
    auto msg = std::string("unknown first_floor_elevation_uncertainty method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("first_floor_elevation_uncertainty fixture") {
    std::ifstream f(fixtures_dir() + "/structures/first_floor_elevation_uncertainty.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "first_floor_elevation_uncertainty");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_first_floor_elevation_uncertainty(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for ValueRatioWithUncertainty (Phase 3 Task 2): same shape as
// run_value_uncertainty above -- a plain Validation-derived per-structure uncertainty sampler,
// not an IDistribution. `construct` is {"dist": "<name>", "std_or_min": ..., "central": ...,
// "max": ...}. `sample` takes [probability]; `sample_iteration` takes [iteration,
// computeIsDeterministic] (both JSON numbers; the flag is interpreted via != 0).
static double run_value_ratio_with_uncertainty(const json& c, const std::string& method,
                                                 const json& args) {
    const auto& ctor = c["construct"];
    auto dist_type = hecfda::statistics::distributions::distribution_type_from_name(
        ctor["dist"].get<std::string>());
    hecfda::model::structures::ValueRatioWithUncertainty vru(
        dist_type, ctor["std_or_min"].get<double>(), ctor["central"].get<double>(),
        ctor["max"].get<double>());
    if (method == "sample") return vru.sample(args[0].get<double>());
    if (method == "sample_iteration")
        return vru.sample(args[0].get<long>(), args[1].get<double>() != 0.0);
    auto msg = std::string("unknown value_ratio_with_uncertainty method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("value_ratio_with_uncertainty fixture") {
    std::ifstream f(fixtures_dir() + "/structures/value_ratio_with_uncertainty.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "value_ratio_with_uncertainty");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_value_ratio_with_uncertainty(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for OccupancyType + DeterministicOccupancyType (Phase 3 Task 4): the
// integration class that binds the three leaf samplers (ValueUncertainty,
// ValueRatioWithUncertainty, FirstFloorElevationUncertainty) to the Phase-2 UncertainPairedData
// via OccupancyType::builder(). `construct` is {name, damage_category, depths,
// struct_damages:[{type,params}], content_damages:[{type,params}], ffe:{dist,std_or_min,[max]},
// structure_value:{dist,std_or_min,[max]}, csvr:{dist,std_or_min,central,[max]}} -- the "max"
// fields are optional, matching the C# ctors' default-max-parameter convention (each type's
// default max is used when omitted). `sample_iteration_*` methods dispatch
// Sample(iteration, computeIsDeterministic) and pull one field off the resulting
// DeterministicOccupancyType; `generate_then_sample_iteration_struct_yvals` additionally reads a
// "size" field off the assertion itself and calls GenerateRandomNumbers(size) before sampling
// with computeIsDeterministic=false, exercising the per-category-seed RNG wiring end to end.
static hecfda::model::paired_data::UncertainPairedData make_occupancy_type_upd(const json& xs_json,
                                                                                const json& ys_json) {
    std::vector<double> xs = xs_json.get<std::vector<double>>();
    std::vector<std::unique_ptr<hecfda::statistics::distributions::IDistribution>> ys;
    for (const auto& y : ys_json) {
        ys.push_back(hecfda::statistics::distributions::IDistributionFactory::create(
            hecfda::statistics::distributions::distribution_type_from_name(y["type"].get<std::string>()),
            y["params"].get<std::vector<double>>()));
    }
    return hecfda::model::paired_data::UncertainPairedData(std::move(xs), std::move(ys));
}

static hecfda::model::structures::OccupancyType make_occupancy_type(const json& ctor) {
    using namespace hecfda::model::structures;
    using hecfda::statistics::distributions::distribution_type_from_name;

    auto struct_upd = make_occupancy_type_upd(ctor["depths"], ctor["struct_damages"]);
    auto content_upd = make_occupancy_type_upd(ctor["depths"], ctor["content_damages"]);

    const auto& ffe_c = ctor["ffe"];
    auto ffe_dist = distribution_type_from_name(ffe_c["dist"].get<std::string>());
    FirstFloorElevationUncertainty ffe = ffe_c.contains("max")
        ? FirstFloorElevationUncertainty(ffe_dist, ffe_c["std_or_min"].get<double>(), ffe_c["max"].get<double>())
        : FirstFloorElevationUncertainty(ffe_dist, ffe_c["std_or_min"].get<double>());

    const auto& sv_c = ctor["structure_value"];
    auto sv_dist = distribution_type_from_name(sv_c["dist"].get<std::string>());
    ValueUncertainty structure_value = sv_c.contains("max")
        ? ValueUncertainty(sv_dist, sv_c["std_or_min"].get<double>(), sv_c["max"].get<double>())
        : ValueUncertainty(sv_dist, sv_c["std_or_min"].get<double>());

    const auto& csvr_c = ctor["csvr"];
    auto csvr_dist = distribution_type_from_name(csvr_c["dist"].get<std::string>());
    ValueRatioWithUncertainty csvr = csvr_c.contains("max")
        ? ValueRatioWithUncertainty(csvr_dist, csvr_c["std_or_min"].get<double>(),
                                     csvr_c["central"].get<double>(), csvr_c["max"].get<double>())
        : ValueRatioWithUncertainty(csvr_dist, csvr_c["std_or_min"].get<double>(),
                                     csvr_c["central"].get<double>());

    auto builder = OccupancyType::builder()
                       .with_name(ctor["name"].get<std::string>())
                       .with_damage_category(ctor["damage_category"].get<std::string>())
                       .with_structure_depth_percent_damage(std::move(struct_upd))
                       .with_content_depth_percent_damage(std::move(content_upd))
                       .with_first_floor_elevation_uncertainty(ffe)
                       .with_structure_value_uncertainty(structure_value)
                       .with_content_to_structure_value_ratio(csvr);

    // Optional: vehicle/other depth-percent-damage + value uncertainty (Phase 4 Task 2's
    // inventory_compute_damages fixture is the first construct to use these; existing
    // occupancy_type.json/inventory.json cases omit them, leaving compute_vehicle_damage()/
    // compute_other_damage() false, same as the emitter's MakeOccupancyType). Each `with_*` mutates
    // `builder`'s internal OccupancyType in place and returns an (unused, here) rvalue reference to
    // the same object -- called as a plain statement on the `builder` lvalue rather than
    // reassigned, to avoid a self-move-assignment (`builder = std::move(builder).with_x(...)` would
    // move-assign `builder` from a reference to itself).
    if (ctor.contains("vehicle_damages")) {
        auto vehicle_upd = make_occupancy_type_upd(ctor["depths"], ctor["vehicle_damages"]);
        builder.with_vehicle_depth_percent_damage(std::move(vehicle_upd));
    }
    if (ctor.contains("other_damages")) {
        auto other_upd = make_occupancy_type_upd(ctor["depths"], ctor["other_damages"]);
        builder.with_other_depth_percent_damage(std::move(other_upd));
    }
    if (ctor.contains("vehicle_value")) {
        const auto& vv_c = ctor["vehicle_value"];
        auto vv_dist = distribution_type_from_name(vv_c["dist"].get<std::string>());
        ValueUncertainty vehicle_value = vv_c.contains("max")
            ? ValueUncertainty(vv_dist, vv_c["std_or_min"].get<double>(), vv_c["max"].get<double>())
            : ValueUncertainty(vv_dist, vv_c["std_or_min"].get<double>());
        builder.with_vehicle_value_uncertainty(vehicle_value);
    }
    if (ctor.contains("other_value")) {
        const auto& ov_c = ctor["other_value"];
        auto ov_dist = distribution_type_from_name(ov_c["dist"].get<std::string>());
        ValueUncertainty other_value = ov_c.contains("max")
            ? ValueUncertainty(ov_dist, ov_c["std_or_min"].get<double>(), ov_c["max"].get<double>())
            : ValueUncertainty(ov_dist, ov_c["std_or_min"].get<double>());
        builder.with_other_value_uncertainty(other_value);
    }

    return builder.build();
}

static std::vector<double> run_occupancy_type(const json& c, const json& a) {
    auto occ = make_occupancy_type(c["construct"]);
    std::string method = a["method"].get<std::string>();
    const auto& args = a["args"];
    if (method == "validate_error_level") {
        // Exercises OccupancyType::validate() -> each sampler's Validation::validate(), i.e. the
        // path that was formerly UB (see value_uncertainty.hpp/value_ratio_with_uncertainty.hpp/
        // first_floor_elevation_uncertainty.hpp add_rules() comments on the [this]-capture fix).
        occ.validate();
        return {static_cast<double>(static_cast<int>(occ.error_level()))};
    }
    if (method == "validate_has_errors") {
        occ.validate();
        return {occ.has_errors() ? 1.0 : 0.0};
    }
    if (method == "generate_then_sample_iteration_struct_yvals") {
        occ.generate_random_numbers(a["size"].get<long>());
        auto sampled = occ.sample(args[0].get<long>(), args[1].get<double>() != 0.0);
        return sampled.struct_percent_damage_paired_data().yvals();
    }
    auto sampled = occ.sample(args[0].get<long>(), args[1].get<double>() != 0.0);
    if (method == "sample_iteration_struct_yvals") return sampled.struct_percent_damage_paired_data().yvals();
    if (method == "sample_iteration_content_yvals") return sampled.content_percent_damage_paired_data().yvals();
    if (method == "sample_iteration_structure_value_offset") return {sampled.structure_value_offset()};
    if (method == "sample_iteration_ffe_offset") return {sampled.first_floor_elevation_offset()};
    if (method == "sample_iteration_csvr") return {sampled.content_to_structure_value_ratio()};
    auto msg = std::string("unknown occupancy_type method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("occupancy_type fixture") {
    std::ifstream f(fixtures_dir() + "/structures/occupancy_type.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "occupancy_type");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_occupancy_type(c, a);
            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for Structure (Phase 3 Task 5): the per-structure numeric depth-damage compute.
// `construct` is {occupancy_type: {name, damage_category, struct_depths,
// struct_damages:[{type,params}], content_depths, content_damages:[{type,params}], ffe:{...},
// structure_value:{...}, csvr:{...}}, sample:[iteration, computeIsDeterministic],
// structure:{fid, first_floor_elevation, val_struct, st_damcat, occtype, impact_area_id,
// [val_cont], [val_vehic], [val_other], [ground_elevation]}}. Unlike make_occupancy_type (which
// shares one "depths" array between struct/content), struct_depths/content_depths are separate
// here because the SELA fixture case's structure and content curves have different lengths -- see
// fixtures/structures/structure.json's note. compute_damage is non-const and mutates the segment
// indices (see structure.hpp), so `structure` below is a fresh, non-const local per assertion,
// matching the fixture's "fresh per assertion" convention used throughout this file.
static hecfda::model::structures::OccupancyType make_structure_occupancy_type(const json& ctor) {
    using namespace hecfda::model::structures;
    using hecfda::statistics::distributions::distribution_type_from_name;

    auto struct_upd = make_occupancy_type_upd(ctor["struct_depths"], ctor["struct_damages"]);
    auto content_upd = make_occupancy_type_upd(ctor["content_depths"], ctor["content_damages"]);

    const auto& ffe_c = ctor["ffe"];
    auto ffe_dist = distribution_type_from_name(ffe_c["dist"].get<std::string>());
    FirstFloorElevationUncertainty ffe = ffe_c.contains("max")
        ? FirstFloorElevationUncertainty(ffe_dist, ffe_c["std_or_min"].get<double>(), ffe_c["max"].get<double>())
        : FirstFloorElevationUncertainty(ffe_dist, ffe_c["std_or_min"].get<double>());

    const auto& sv_c = ctor["structure_value"];
    auto sv_dist = distribution_type_from_name(sv_c["dist"].get<std::string>());
    ValueUncertainty structure_value = sv_c.contains("max")
        ? ValueUncertainty(sv_dist, sv_c["std_or_min"].get<double>(), sv_c["max"].get<double>())
        : ValueUncertainty(sv_dist, sv_c["std_or_min"].get<double>());

    const auto& csvr_c = ctor["csvr"];
    auto csvr_dist = distribution_type_from_name(csvr_c["dist"].get<std::string>());
    ValueRatioWithUncertainty csvr = csvr_c.contains("max")
        ? ValueRatioWithUncertainty(csvr_dist, csvr_c["std_or_min"].get<double>(),
                                     csvr_c["central"].get<double>(), csvr_c["max"].get<double>())
        : ValueRatioWithUncertainty(csvr_dist, csvr_c["std_or_min"].get<double>(),
                                     csvr_c["central"].get<double>());

    return OccupancyType::builder()
        .with_name(ctor["name"].get<std::string>())
        .with_damage_category(ctor["damage_category"].get<std::string>())
        .with_structure_depth_percent_damage(std::move(struct_upd))
        .with_content_depth_percent_damage(std::move(content_upd))
        .with_first_floor_elevation_uncertainty(ffe)
        .with_structure_value_uncertainty(structure_value)
        .with_content_to_structure_value_ratio(csvr)
        .build();
}

static hecfda::model::structures::Structure make_structure(const json& ctor) {
    using hecfda::model::structures::Structure;
    double val_cont = ctor.contains("val_cont") ? ctor["val_cont"].get<double>() : 0;
    double val_vehic = ctor.contains("val_vehic") ? ctor["val_vehic"].get<double>() : 0;
    double val_other = ctor.contains("val_other") ? ctor["val_other"].get<double>() : 0;
    double ground_elevation = ctor.contains("ground_elevation") ? ctor["ground_elevation"].get<double>()
                                                                 : Structure::kDefaultMissingValue;
    return Structure(ctor["fid"].get<std::string>(), ctor["first_floor_elevation"].get<double>(),
                      ctor["val_struct"].get<double>(), ctor["st_damcat"].get<std::string>(),
                      ctor["occtype"].get<std::string>(), ctor["impact_area_id"].get<int>(), val_cont,
                      val_vehic, val_other, "unassigned", Structure::kDefaultMissingValue, ground_elevation);
}

static double run_structure(const json& c, const json& a) {
    const auto& ctor = c["construct"];
    auto occ = make_structure_occupancy_type(ctor["occupancy_type"]);
    const auto& sample = ctor["sample"];
    auto sampled = occ.sample(sample[0].get<long>(), sample[1].get<double>() != 0.0);
    auto structure = make_structure(ctor["structure"]);
    std::string method = a["method"].get<std::string>();
    const auto& args = a["args"];
    float wse = static_cast<float>(args[0].get<double>());
    auto [struct_damage, cont_damage, vehicle_damage, other_damage] = structure.compute_damage(wse, sampled);
    if (method == "compute_damage_struct") return struct_damage;
    if (method == "compute_damage_content") return cont_damage;
    if (method == "compute_damage_vehicle") return vehicle_damage;
    if (method == "compute_damage_other") return other_damage;
    auto msg = std::string("unknown structure method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("structure fixture") {
    std::ifstream f(fixtures_dir() + "/structures/structure.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "structure");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_structure(c, a);
            double exp = a["expected"].get<double>();
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            bool ok = mode == "rel" ? (std::abs(got - exp) / (std::abs(exp) > 0 ? std::abs(exp) : 1.0)) <= tol
                                     : std::abs(got - exp) <= tol;
            if (!ok) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for Inventory (Phase 3 Task 6, the last core-code task of the phase): the
// numeric subset (in-memory ctor, damage-category enumeration, impact-area trim, per-occ-type RNG
// generation + sampling, ground elevations, Validate aggregation). `construct` is {occ_types:
// [<occupancy_type construct>, ...], structures: [<structure construct>, ...], [price_index]} --
// see fixtures/structures/inventory.json's note and inventory.hpp's class comment for the full
// design rationale (why occ_types is a shared_ptr<map>, why Structure needed its this-capture
// bugfix, and why the validate_* cases use an empty occ_types list).
static hecfda::model::structures::Inventory make_inventory(const json& ctor) {
    using namespace hecfda::model::structures;
    std::map<std::string, OccupancyType> occ_types;
    for (const auto& occ_ctor : ctor["occ_types"]) {
        occ_types.emplace(occ_ctor["name"].get<std::string>(), make_occupancy_type(occ_ctor));
    }
    std::vector<Structure> structures;
    structures.reserve(ctor["structures"].size());
    for (const auto& struct_ctor : ctor["structures"]) {
        structures.push_back(make_structure(struct_ctor));
    }
    double price_index = ctor.contains("price_index") ? ctor["price_index"].get<double>() : 1.0;
    return Inventory(std::move(occ_types), std::move(structures), price_index);
}

static std::vector<double> run_inventory(const json& c, const json& a) {
    using namespace hecfda::model::structures;
    auto inv = make_inventory(c["construct"]);
    std::string method = a["method"].get<std::string>();
    const auto& args = a["args"];
    if (method == "damage_category_count") {
        return {static_cast<double>(inv.get_damage_categories().size())};
    }
    if (method == "trim_to_impact_area_count") {
        auto trimmed = inv.get_inventory_trimmed_to_impact_area(args[0].get<int>());
        return {static_cast<double>(trimmed.structures().size())};
    }
    if (method == "generate_then_sample_struct_yvals") {
        const auto& conv = a["convergence"];
        hecfda::statistics::ConvergenceCriteria cc(conv["min_iterations"].get<int>(), conv["max_iterations"].get<int>());
        inv.generate_random_numbers(cc);
        auto sampled = inv.sample_occupancy_types(args[0].get<long>(), args[1].get<double>() != 0.0);
        return sampled[0].struct_percent_damage_paired_data().yvals();
    }
    if (method == "validate_error_level") {
        inv.validate();
        return {static_cast<double>(static_cast<unsigned char>(inv.error_level()))};
    }
    if (method == "validate_has_errors") {
        inv.validate();
        return {inv.has_errors() ? 1.0 : 0.0};
    }
    auto msg = std::string("unknown inventory method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("inventory fixture") {
    std::ifstream f(fixtures_dir() + "/structures/inventory.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "inventory");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_inventory(c, a);
            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for ConsequenceResult (Phase 4 Task 1): a plain per-structure damage
// accumulator, not an IDistribution, constructed directly here like ValueUncertainty/Structure
// above. `construct` is {"damage_category": "<name>"}; `increments` is a list of [structureDamage,
// contentDamage, vehicleDamage, otherDamage] tuples applied in order via increment_consequence
// before every assertion in the case -- each assertion reconstructs a fresh ConsequenceResult and
// replays the full increment list, matching run_value_uncertainty's per-call reconstruction
// pattern. `method` dispatches one of the eight accessors; the four *_quantity accessors return
// int, cast to double for the shared double-comparison harness. `equals` builds a second
// ConsequenceResult from the case's `compare_to` block (same {construct, increments} shape) and
// returns 1.0/0.0 for cr.equals(cr2).
static double run_consequence_result(const json& c, const std::string& method, const json& args) {
    (void)args;
    const auto& ctor = c["construct"];
    hecfda::model::metrics::ConsequenceResult cr(ctor["damage_category"].get<std::string>());
    for (const auto& inc : c["increments"]) {
        cr.increment_consequence(inc[0].get<double>(), inc[1].get<double>(), inc[2].get<double>(),
                                  inc[3].get<double>());
    }
    if (method == "structure_damage") return cr.structure_damage();
    if (method == "content_damage") return cr.content_damage();
    if (method == "vehicle_damage") return cr.vehicle_damage();
    if (method == "other_damage") return cr.other_damage();
    if (method == "damaged_structures_quantity")
        return static_cast<double>(cr.damaged_structures_quantity());
    if (method == "damaged_contents_quantity")
        return static_cast<double>(cr.damaged_contents_quantity());
    if (method == "damaged_vehicles_quantity")
        return static_cast<double>(cr.damaged_vehicles_quantity());
    if (method == "damaged_others_quantity")
        return static_cast<double>(cr.damaged_others_quantity());
    if (method == "equals") {
        const auto& ctor2 = c["compare_to"]["construct"];
        hecfda::model::metrics::ConsequenceResult cr2(ctor2["damage_category"].get<std::string>());
        for (const auto& inc : c["compare_to"]["increments"]) {
            cr2.increment_consequence(inc[0].get<double>(), inc[1].get<double>(),
                                       inc[2].get<double>(), inc[3].get<double>());
        }
        return cr.equals(cr2) ? 1.0 : 0.0;
    }
    auto msg = std::string("unknown consequence_result method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("consequence_result fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/consequence_result.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "consequence_result");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_consequence_result(c, a["method"], a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// Bespoke dispatch for Inventory::compute_damages/aggregate_results (Phase 4 Task 2): the parallel
// damage collection severed from the Phase 3 Inventory port pending ConsequenceResult (Task 1).
// `construct` extends make_inventory's shape (occ_types/structures/[price_index]) with `wses`
// ([profile][structure] float matrix), `analysis_year`, `damage_category`, and `sample`
// ([iteration, computeIsDeterministic] fed to sample_occupancy_types). Every assertion builds a
// fresh Inventory + samples once, then calls compute_damages(wses, analysisYear, damageCategory,
// det) and returns one of the four per-profile damage arrays (`compute_damages_struct/_content/
// _vehicle/_other`) -- see inventory.hpp's compute_damages doc comment for the faithful
// store/aggregate_results-argument swap this fixture locks (the occ_types construct below uses
// DISTINCT nonzero vehicle/other depth-percent-damage curves so the wiring is actually observed).
static std::vector<double> run_inventory_compute_damages(const json& c, const std::string& method) {
    using namespace hecfda::model::structures;
    const auto& ctor = c["construct"];
    auto inv = make_inventory(ctor);
    const auto& sample = ctor["sample"];
    auto det = inv.sample_occupancy_types(sample[0].get<long>(), sample[1].get<double>() != 0.0);

    std::vector<std::vector<float>> wses;
    for (const auto& pf : ctor["wses"]) {
        wses.push_back(pf.get<std::vector<float>>());
    }
    int analysis_year = ctor["analysis_year"].get<int>();
    std::string damage_category = ctor["damage_category"].get<std::string>();
    auto results = inv.compute_damages(wses, analysis_year, damage_category, det);

    std::vector<double> out;
    out.reserve(results.size());
    if (method == "compute_damages_struct") {
        for (const auto& r : results) out.push_back(r.structure_damage());
    } else if (method == "compute_damages_content") {
        for (const auto& r : results) out.push_back(r.content_damage());
    } else if (method == "compute_damages_vehicle") {
        for (const auto& r : results) out.push_back(r.vehicle_damage());
    } else if (method == "compute_damages_other") {
        for (const auto& r : results) out.push_back(r.other_damage());
    } else {
        auto msg = std::string("unknown inventory_compute_damages method: ") + method;
        FAIL(msg.c_str());
    }
    return out;
}

TEST_CASE("inventory_compute_damages fixture") {
    std::ifstream f(fixtures_dir() + "/stage_damage/inventory_compute_damages.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "inventory_compute_damages");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_inventory_compute_damages(c, a["method"].get<std::string>());
            std::vector<double> exp = a["expected"].get<std::vector<double>>();
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bespoke dispatch for AggregatedConsequencesBinned (Phase 4 Task 3): the histogram-staging Monte
// Carlo accumulator. `construct` is {damage_category, asset_category, convergence:
// {min_iterations, max_iterations}, impact_area_id, consequence_type, risk_type} matching the
// compute ctor; ConvergenceCriteria is built with the 2-arg (minIterations, maxIterations) ctor,
// same as run_inventory's `generate_then_sample_struct_yvals` case. `realizations` is applied in
// order via add_consequence_realization(damage, iteration, count) before a single
// put_data_into_histogram() call -- ONE object per case, shared across all of the case's
// assertions (not rebuilt per assertion, since PutDataIntoHistogram is only meaningful once per
// staged batch). `method` dispatches sample_mean_expected_annual_consequences (args []),
// consequence_exceeded_with_probability_q (args [q]), or quantity_exceeded_with_probability_q
// (args [q]). See fixtures/metrics/aggregated_consequences_binned.json's note for what each case
// exercises (the DEFAULT_BIN_WIDTH vs range/INITIAL_BIN_QUANTITY branches, and the staged-array
// min/max quirk documented in aggregated_consequences_binned.hpp).
static hecfda::model::metrics::ConsequenceType parse_consequence_type(const std::string& name) {
    if (name == "UNASSIGNED") return hecfda::model::metrics::ConsequenceType::UNASSIGNED;
    if (name == "Damage") return hecfda::model::metrics::ConsequenceType::Damage;
    if (name == "LifeLoss") return hecfda::model::metrics::ConsequenceType::LifeLoss;
    if (name == "All") return hecfda::model::metrics::ConsequenceType::All;
    FAIL((std::string("unknown consequence_type: ") + name).c_str());
    return hecfda::model::metrics::ConsequenceType::UNASSIGNED;
}

static hecfda::model::metrics::RiskType parse_risk_type(const std::string& name) {
    if (name == "Fail") return hecfda::model::metrics::RiskType::Fail;
    if (name == "Non_Fail") return hecfda::model::metrics::RiskType::Non_Fail;
    if (name == "Total") return hecfda::model::metrics::RiskType::Total;
    if (name == "Unassigned") return hecfda::model::metrics::RiskType::Unassigned;
    FAIL((std::string("unknown risk_type: ") + name).c_str());
    return hecfda::model::metrics::RiskType::Unassigned;
}

static hecfda::model::metrics::AggregatedConsequencesBinned make_aggregated_consequences_binned(
    const json& ctor) {
    using namespace hecfda::model::metrics;
    const auto& conv = ctor["convergence"];
    hecfda::statistics::ConvergenceCriteria cc(conv["min_iterations"].get<int>(),
                                                conv["max_iterations"].get<int>());
    return AggregatedConsequencesBinned(ctor["damage_category"].get<std::string>(),
                                         ctor["asset_category"].get<std::string>(), cc,
                                         ctor["impact_area_id"].get<int>(),
                                         parse_consequence_type(ctor["consequence_type"].get<std::string>()),
                                         parse_risk_type(ctor["risk_type"].get<std::string>()));
}

static double run_aggregated_consequences_binned(const json& c, const std::string& method,
                                                   const json& args) {
    auto acb = make_aggregated_consequences_binned(c["construct"]);
    for (const auto& r : c["realizations"]) {
        acb.add_consequence_realization(r["damage"].get<double>(), r["iteration"].get<std::int64_t>(),
                                         r["count"].get<int>());
    }
    acb.put_data_into_histogram();
    if (method == "sample_mean_expected_annual_consequences") {
        return acb.sample_mean_expected_annual_consequences();
    }
    if (method == "consequence_exceeded_with_probability_q") {
        return acb.consequence_exceeded_with_probability_q(args[0].get<double>());
    }
    if (method == "quantity_exceeded_with_probability_q") {
        return acb.quantity_exceeded_with_probability_q(args[0].get<double>());
    }
    auto msg = std::string("unknown aggregated_consequences_binned method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("aggregated_consequences_binned fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/aggregated_consequences_binned.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "aggregated_consequences_binned");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_aggregated_consequences_binned(c, a["method"].get<std::string>(), a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bespoke dispatch for AssuranceResultStorage (Phase 5 Task 1): the histogram-staging Monte Carlo
// accumulator for one assurance metric. `construct` is {assurance_type, bin_width, convergence:
// {min_iterations, max_iterations}, standard_non_exceedance_probability} matching the compute
// ctor; ConvergenceCriteria is built with the 2-arg (minIterations, maxIterations) ctor, same
// convention as run_aggregated_consequences_binned. `observations` is applied in order via
// add_observation(result, iteration) before a single put_data_into_histogram() call -- ONE object
// per case, shared across all of the case's assertions. `method` dispatches sample_mean (args [])
// or inverse_cdf (args [p]), both read off assurance_histogram(). See
// fixtures/metrics/assurance_result_storage.json's note for what each case exercises.
static hecfda::model::metrics::AssuranceResultStorage make_assurance_result_storage(const json& ctor) {
    using namespace hecfda::model::metrics;
    const auto& conv = ctor["convergence"];
    hecfda::statistics::ConvergenceCriteria cc(conv["min_iterations"].get<int>(),
                                                conv["max_iterations"].get<int>());
    return AssuranceResultStorage(ctor["assurance_type"].get<std::string>(),
                                   ctor["bin_width"].get<double>(), cc,
                                   ctor["standard_non_exceedance_probability"].get<double>());
}

static double run_assurance_result_storage(const json& c, const std::string& method, const json& args) {
    auto ars = make_assurance_result_storage(c["construct"]);
    for (const auto& o : c["observations"]) {
        ars.add_observation(o["result"].get<double>(), o["iteration"].get<int>());
    }
    ars.put_data_into_histogram();
    if (method == "sample_mean") {
        return ars.assurance_histogram().sample_mean();
    }
    if (method == "inverse_cdf") {
        return ars.assurance_histogram().inverse_cdf(args[0].get<double>());
    }
    auto msg = std::string("unknown assurance_result_storage method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("assurance_result_storage fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/assurance_result_storage.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "assurance_result_storage");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_assurance_result_storage(c, a["method"].get<std::string>(), a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// SystemPerformanceResults (Phase 5 Task 2) is the system-performance metrics container: AEP +
// stage assurance histograms wrapping Task-1 AssuranceResultStorage, plus the FP-sensitive levee
// fragility-curve integration (calculate_assurance_for_levee). Three case shapes selected by
// `construct.case_kind` (see fixtures/metrics/system_performance_results.json's note for the full
// rationale; mirrors EvalSystemPerformanceResults in Program.cs case-for-case):
//  - "aep": SystemPerformanceResults(ConvergenceCriteria), stages `aep_observations` via
//    add_aep_for_assurance, put_data_into_histograms() once, dispatches mean_aep/median_aep/
//    long_term_exceedance_probability(years).
//  - "rng_conformance": the PerformanceTest.AssuranceResultStorageShould RNG-port-conformance pin.
//    Seeds `iteration_count` master seeds via DotNetRandom(master_seed).internal_sample() (the C++
//    equivalent of C#'s parameterless `Random.Next()`, which is `InternalSample()` unscaled -- see
//    dotnet_random.hpp), matching the real test's masterSeedList loop (only the first
//    IterationCount of MinIterations are ever consumed by the real test's Parallel.For bound, so
//    generating exactly IterationCount reproduces the identical seed prefix). For `compute_chunks`
//    outer passes: for each seed, RandomProvider(seed).next_random() -> Normal(0,1).inverse_cdf()
//    feeds add_stage_for_assurance; put_data_into_histograms() once per pass. Dispatches
//    assurance_of_event (proving the seeded DotNetRandom(1234) -> RandomProvider -> Normal
//    InverseCDF chain reproduces the real C#) and normal_cdf_reference (the same
//    Normal(0,1).CDF(threshold) PerformanceTest.cs cross-checks against, pinned from the identical
//    C# run for an apples-to-apples comparison).
//  - "levee": SystemPerformanceResults(UncertainPairedData, ConvergenceCriteria) built from
//    system_response_xs/ys (Deterministic-distribution ys, mirroring ComputeLeveeAEP_Test's
//    fragility curve), stages `stage_observations` via add_stage_for_assurance,
//    put_data_into_histograms() once, dispatches assurance_of_event through
//    calculate_assurance_for_levee.
static double run_system_performance_results(const json& c, const std::string& method, const json& args) {
    using namespace hecfda::model::metrics;
    using namespace hecfda::model::paired_data;
    using hecfda::statistics::ConvergenceCriteria;
    using hecfda::statistics::distributions::Deterministic;
    using hecfda::statistics::distributions::IDistribution;
    using hecfda::statistics::distributions::Normal;

    const auto& ctor = c["construct"];
    std::string case_kind = ctor["case_kind"].get<std::string>();
    const auto& conv = ctor["convergence"];
    ConvergenceCriteria cc(conv["min_iterations"].get<int>(), conv["max_iterations"].get<int>());

    if (case_kind == "aep") {
        SystemPerformanceResults spr(cc);
        for (const auto& o : ctor["aep_observations"]) {
            spr.add_aep_for_assurance(o["result"].get<double>(), o["iteration"].get<int>());
        }
        spr.put_data_into_histograms();
        if (method == "mean_aep") return spr.mean_aep();
        if (method == "median_aep") return spr.median_aep();
        if (method == "long_term_exceedance_probability") {
            return spr.long_term_exceedance_probability(args[0].get<int>());
        }
        FAIL("unknown system_performance_results (aep) method");
        return 0.0;
    }

    if (case_kind == "rng_conformance") {
        double standard_probability = ctor["standard_probability"].get<double>();
        int master_seed = ctor["master_seed"].get<int>();
        double threshold_value = ctor["threshold_value"].get<double>();
        int compute_chunks = ctor["compute_chunks"].get<int>();

        SystemPerformanceResults spr(cc);
        spr.add_stage_assurance_histogram(standard_probability);

        int iteration_count = cc.iteration_count();
        hecfda::sampling::DotNetRandom master_seed_list(master_seed);
        std::vector<int> seeds(static_cast<std::size_t>(iteration_count));
        for (int i = 0; i < iteration_count; ++i) {
            seeds[static_cast<std::size_t>(i)] = master_seed_list.internal_sample();
        }

        Normal standard_normal(0.0, 1.0);
        for (int j = 0; j < compute_chunks; ++j) {
            for (int i = 0; i < iteration_count; ++i) {
                hecfda::model::compute::RandomProvider provider(seeds[static_cast<std::size_t>(i)]);
                double inv_cdf = standard_normal.inverse_cdf(provider.next_random());
                spr.add_stage_for_assurance(standard_probability, inv_cdf, i);
            }
            spr.put_data_into_histograms();
        }
        if (method == "assurance_of_event") {
            return spr.assurance_of_event(standard_probability, threshold_value);
        }
        if (method == "normal_cdf_reference") {
            return standard_normal.cdf(threshold_value);
        }
        FAIL("unknown system_performance_results (rng_conformance) method");
        return 0.0;
    }

    if (case_kind == "levee") {
        std::vector<double> xs = ctor["system_response_xs"].get<std::vector<double>>();
        std::vector<double> ys = ctor["system_response_ys"].get<std::vector<double>>();
        std::vector<std::unique_ptr<IDistribution>> failure_probs;
        for (double y : ys) {
            failure_probs.push_back(std::make_unique<Deterministic>(y));
        }
        UncertainPairedData system_response(xs, std::move(failure_probs));
        double standard_probability = ctor["standard_probability"].get<double>();

        SystemPerformanceResults spr(std::move(system_response), cc);
        spr.add_stage_assurance_histogram(standard_probability);
        for (const auto& o : ctor["stage_observations"]) {
            spr.add_stage_for_assurance(standard_probability, o["result"].get<double>(), o["iteration"].get<int>());
        }
        spr.put_data_into_histograms();
        if (method == "assurance_of_event") {
            return spr.assurance_of_event(standard_probability, args[0].get<double>());
        }
        FAIL("unknown system_performance_results (levee) method");
        return 0.0;
    }
    FAIL("unknown system_performance_results case_kind");
    return 0.0;
}

TEST_CASE("system_performance_results fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/system_performance_results.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "system_performance_results");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_system_performance_results(c, a["method"].get<std::string>(), a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// PerformanceByThresholds (Phase 5 Task 3) is the Threshold container. `construct.thresholds` is a
// list of {id, type (ThresholdEnum name string), value, convergence}; each is built via the (id,
// ConvergenceCriteria, ThresholdEnum, value) ctor and add_threshold'd in list order.
// `construct.get_threshold_id` selects which one get_threshold retrieves; `threshold_value`/
// `threshold_type`/`threshold_id` read straight off that retrieved Threshold (plain ctor-assigned
// data, not oracle math -- exact literals in the fixture). `construct.aep_observations`
// ({iteration, result}) are then fed into the retrieved Threshold's own SystemPerformanceResults
// via add_aep_for_assurance, followed by one put_data_into_histograms() call, so `mean_aep` proves
// add_threshold/get_threshold hand back the SAME live SystemPerformanceResults the ctor built (not
// a copy) -- pinned from the real C# via the oracle gate. Mirrors EvalPerformanceByThresholds in
// Program.cs case-for-case.
static hecfda::model::metrics::ThresholdEnum threshold_enum_from_name(const std::string& name) {
    using hecfda::model::metrics::ThresholdEnum;
    if (name == "NotSupported") return ThresholdEnum::NotSupported;
    if (name == "DefaultExteriorStage") return ThresholdEnum::DefaultExteriorStage;
    if (name == "TopOfLevee") return ThresholdEnum::TopOfLevee;
    if (name == "LeveeSystemResponse") return ThresholdEnum::LeveeSystemResponse;
    if (name == "AdditionalExteriorStage") return ThresholdEnum::AdditionalExteriorStage;
    FAIL(("unknown ThresholdEnum name: " + name).c_str());
    return ThresholdEnum::NotSupported;
}

static double run_performance_by_thresholds(const json& c, const std::string& method, const json& /*args*/) {
    using namespace hecfda::model::metrics;
    using hecfda::statistics::ConvergenceCriteria;

    const auto& ctor = c["construct"];
    PerformanceByThresholds pbt;
    for (const auto& t : ctor["thresholds"]) {
        int id = t["id"].get<int>();
        ThresholdEnum type = threshold_enum_from_name(t["type"].get<std::string>());
        double value = t["value"].get<double>();
        const auto& conv = t["convergence"];
        ConvergenceCriteria cc(conv["min_iterations"].get<int>(), conv["max_iterations"].get<int>());
        pbt.add_threshold(Threshold(id, cc, type, value));
    }
    int get_threshold_id = ctor["get_threshold_id"].get<int>();
    Threshold& threshold = pbt.get_threshold(get_threshold_id);

    if (method == "threshold_value") return threshold.threshold_value();
    if (method == "threshold_type") return static_cast<double>(static_cast<int>(threshold.threshold_type()));
    if (method == "threshold_id") return static_cast<double>(threshold.threshold_id());
    if (method == "mean_aep") {
        for (const auto& o : ctor["aep_observations"]) {
            threshold.system_performance_results().add_aep_for_assurance(o["result"].get<double>(),
                                                                           o["iteration"].get<int>());
        }
        threshold.system_performance_results().put_data_into_histograms();
        return threshold.system_performance_results().mean_aep();
    }
    FAIL("unknown performance_by_thresholds method");
    return 0.0;
}

TEST_CASE("performance_by_thresholds fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/performance_by_thresholds.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "performance_by_thresholds");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_performance_by_thresholds(c, a["method"].get<std::string>(), a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// StudyAreaConsequencesBinned (Phase 4 Task 4) is the collection wrapper over per-asset-category
// AggregatedConsequencesBinned results. `construct` is {damage_category, impact_area_id,
// convergence: {min_iterations, max_iterations}, asset_categories: [...]}; one
// AggregatedConsequencesBinned is built per asset_category (in list order, matching
// ConsequenceType::Damage/RiskType::Fail -- StudyAreaConsequencesBinned's own
// get_consequence_result defaults). `consequence_results` is a list of {iteration, increments:
// [[structureDamage,contentDamage,vehicleDamage,otherDamage], ...]}; each entry builds a fresh
// ConsequenceResult(damage_category), replays every increment tuple via increment_consequence (same
// convention as consequence_result.json), then feeds it through the stage-damage
// add_consequence_realization(ConsequenceResult, damage_category, impact_area_id, iteration)
// overload -- the 4-way asset-category split this task ports. put_data_into_histograms() runs once
// after every consequence_results entry is applied (matching AggregatedConsequencesBinned's
// staged-batch convention), then to_uncertain_paired_data(xs, {study}, impact_area_id) is called
// fresh per assertion (mirrors run_uncertain_paired_data_ops). `args[0]` indexes into the
// construct's asset_categories list, selecting which of the resulting (single damage_category x N
// asset_category) UncertainPairedData pairs to sample -- get_damage_categories/get_asset_categories
// walk ConsequenceResultList in first-seen == construction order, so this indexing is deterministic.
// `method` dispatches to_uncertain_paired_data_damage_yvals or
// to_uncertain_paired_data_quantity_yvals, each returning sample_paired_data(0, true)'s
// (deterministic, monotonicity-forced) yvals -- exercising the IHistogram branch of
// convert_distribution_to_deterministic added by this task.
static hecfda::model::metrics::StudyAreaConsequencesBinned make_study_area_consequences_binned(
    const json& ctor) {
    using namespace hecfda::model::metrics;
    const auto& conv = ctor["convergence"];
    hecfda::statistics::ConvergenceCriteria cc(conv["min_iterations"].get<int>(),
                                                conv["max_iterations"].get<int>());
    std::string damage_category = ctor["damage_category"].get<std::string>();
    int impact_area_id = ctor["impact_area_id"].get<int>();
    std::vector<AggregatedConsequencesBinned> results;
    for (const auto& asset_category_json : ctor["asset_categories"]) {
        results.emplace_back(damage_category, asset_category_json.get<std::string>(), cc, impact_area_id,
                              ConsequenceType::Damage, RiskType::Fail);
    }
    return StudyAreaConsequencesBinned(std::move(results));
}

static std::vector<double> run_study_area_consequences_binned(const json& c, const std::string& method,
                                                                 const json& args) {
    using namespace hecfda::model::metrics;
    StudyAreaConsequencesBinned study = make_study_area_consequences_binned(c["construct"]);
    std::string damage_category = c["construct"]["damage_category"].get<std::string>();
    int impact_area_id = c["construct"]["impact_area_id"].get<int>();

    for (const auto& realization : c["consequence_results"]) {
        ConsequenceResult cr(damage_category);
        for (const auto& inc : realization["increments"]) {
            cr.increment_consequence(inc[0].get<double>(), inc[1].get<double>(), inc[2].get<double>(),
                                      inc[3].get<double>());
        }
        study.add_consequence_realization(cr, damage_category, impact_area_id,
                                           realization["iteration"].get<int>());
    }
    study.put_data_into_histograms();

    std::vector<double> xs = c["xs"].get<std::vector<double>>();
    std::vector<StudyAreaConsequencesBinned> y_values;
    y_values.push_back(std::move(study));
    auto upds = StudyAreaConsequencesBinned::to_uncertain_paired_data(xs, y_values, impact_area_id);
    auto& damage_upds = upds.first;
    auto& quantity_upds = upds.second;

    int asset_index = args[0].get<int>();
    if (method == "to_uncertain_paired_data_damage_yvals") {
        return damage_upds.at(static_cast<std::size_t>(asset_index)).sample_paired_data(0, true).yvals();
    }
    if (method == "to_uncertain_paired_data_quantity_yvals") {
        return quantity_upds.at(static_cast<std::size_t>(asset_index)).sample_paired_data(0, true).yvals();
    }
    auto msg = std::string("unknown study_area_consequences_binned method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("study_area_consequences_binned fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/study_area_consequences_binned.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "study_area_consequences_binned");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_study_area_consequences_binned(c, a["method"].get<std::string>(), a["args"]);
            std::vector<double> exp = a["expected"].get<std::vector<double>>();
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bespoke dispatch for HydraulicProfiles (Phase 4 Task 5): the hydraulics-as-arrays input
// boundary + CorrectDryStructureWSEs. `construct` is {probabilities, ground_elevations,
// wses_by_profile} ([profile][structure] raw WSEs); every assertion builds a fresh
// HydraulicProfiles(probabilities, wses_by_profile) (exercising the descending-order-enforcing
// ctor) then dispatches `method`: "profile_probabilities" (args []) returns the stored
// probabilities back (mode vector, an ordering sanity check); "get_corrected_wses" (args [])
// calls get_corrected_wses(ground_elevations) and flattens the returned [profile][structure]
// matrix row-major into a flat vector (mode matrix, matching how the fixture's nested "expected"
// array is flattened below).
static std::vector<double> run_hydraulic_profiles(const json& c, const std::string& method) {
    using namespace hecfda::model::stage_damage;
    const auto& ctor = c["construct"];
    std::vector<double> probabilities = ctor["probabilities"].get<std::vector<double>>();
    std::vector<std::vector<float>> wses_by_profile;
    for (const auto& pf : ctor["wses_by_profile"]) {
        wses_by_profile.push_back(pf.get<std::vector<float>>());
    }
    HydraulicProfiles profiles(probabilities, wses_by_profile);

    if (method == "profile_probabilities") {
        return profiles.profile_probabilities();
    }
    if (method == "get_corrected_wses") {
        std::vector<float> ground_elevations = ctor["ground_elevations"].get<std::vector<float>>();
        auto corrected = profiles.get_corrected_wses(ground_elevations);
        std::vector<double> flat;
        for (const auto& row : corrected) {
            for (float v : row) flat.push_back(static_cast<double>(v));
        }
        return flat;
    }
    auto msg = std::string("unknown hydraulic_profiles method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("hydraulic_profiles fixture") {
    std::ifstream f(fixtures_dir() + "/stage_damage/correct_dry_structure_wses.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "correct_dry_structure_wses");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_hydraulic_profiles(c, a["method"].get<std::string>());
            std::string mode = a["mode"].get<std::string>();
            std::vector<double> exp;
            if (mode == "matrix") {
                for (const auto& row : a["expected"]) {
                    for (const auto& v : row) exp.push_back(v.get<double>());
                }
            } else {
                exp = a["expected"].get<std::vector<double>>();
            }
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bespoke dispatch for ImpactAreaStageDamage GEOMETRY (Phase 4 Task 6): the constructor +
// EstablishAggregationStages (central stage-frequency, min/max stage, coordinate quantities) and
// the pure static stage-interval helpers. Two case shapes -- see
// fixtures/stage_damage/stage_damage_geometry.json's note for the full rationale: (a)
// 'extrapolate_from_above'/'extrapolate_from_below' call the two public static helpers directly,
// `construct` empty; (b) 'tractable_geometry' builds a real ImpactAreaStageDamage from a graphical
// STAGE frequency (UsingStagesNotFlows=true) + a tractable 2-structure Residential Inventory
// (content unused by any geometry method -- see the fixture note) + mock HydraulicProfiles built
// the same way TractableStageDamageTests.ComputeStagesAtStructures does.
using ImpactAreaStageDamage = hecfda::model::stage_damage::ImpactAreaStageDamage;

static hecfda::model::structures::Inventory make_tractable_residential_inventory(int impact_area_id) {
    using namespace hecfda::model::structures;
    using hecfda::model::paired_data::UncertainPairedData;
    using hecfda::statistics::distributions::Deterministic;
    using hecfda::statistics::distributions::IDistribution;

    // ported from: TractableStageDamageTests.cs's residential occ-type/structure data (depths,
    // residentialStructureDamage, residentialContentAndCommercialStructureDamage, residentialCSVR,
    // structure1/structure2).
    std::vector<double> depths = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<double> struct_damage_vals = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    std::vector<double> content_damage_vals = {0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95};

    auto make_deterministic_upd = [](const std::vector<double>& xs, const std::vector<double>& vals) {
        std::vector<std::unique_ptr<IDistribution>> ys;
        ys.reserve(vals.size());
        for (double v : vals) ys.push_back(std::make_unique<Deterministic>(v));
        return UncertainPairedData(xs, std::move(ys));
    };

    OccupancyType residential =
        OccupancyType::builder()
            .with_name("Residential")
            .with_damage_category("Residential")
            .with_structure_depth_percent_damage(make_deterministic_upd(depths, struct_damage_vals))
            .with_content_depth_percent_damage(make_deterministic_upd(depths, content_damage_vals))
            .with_content_to_structure_value_ratio(ValueRatioWithUncertainty(50))
            .build();

    std::map<std::string, OccupancyType> occ_types;
    occ_types.emplace("Residential", std::move(residential));

    std::vector<Structure> structures;
    structures.push_back(Structure("1", /*first_floor_elevation=*/14, /*val_struct=*/100, "Residential",
                                    "Residential", impact_area_id, /*val_cont=*/0, /*val_vehic=*/0,
                                    /*val_other=*/0, "unassigned", Structure::kDefaultMissingValue,
                                    /*ground_elevation=*/12));
    structures.push_back(Structure("2", /*first_floor_elevation=*/15, /*val_struct=*/200, "Residential",
                                    "Residential", impact_area_id, /*val_cont=*/0, /*val_vehic=*/0,
                                    /*val_other=*/0, "unassigned", Structure::kDefaultMissingValue,
                                    /*ground_elevation=*/12));

    return Inventory(std::move(occ_types), std::move(structures));
}

// ported from: TractableStageDamageTests.cs's ComputeStagesAtStructures(stage1, stage2): profile 0
// = {stage1, stage2}; each subsequent profile's per-structure WSE = previous profile's + 1. One
// profile per `probabilities` entry.
static std::vector<std::vector<float>> make_mock_wses_by_profile(float stage1, float stage2,
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

TEST_CASE("stage_damage_geometry fixture") {
    using hecfda::model::paired_data::CurveMetaData;
    using hecfda::model::paired_data::GraphicalUncertainPairedData;
    using hecfda::model::stage_damage::HydraulicProfiles;

    std::ifstream f(fixtures_dir() + "/stage_damage/stage_damage_geometry.json");
    REQUIRE(f.good());
    json fx;
    f >> fx;
    CHECK(fx["target"] == "stage_damage_geometry");
    for (const auto& c : fx["cases"]) {
        const auto& ctor = c["construct"];
        for (const auto& a : c["assertions"]) {
            std::string method = a["method"].get<std::string>();
            const auto& args = a["args"];

            if (method == "extrapolate_from_above") {
                std::vector<float> input = args[0].get<std::vector<float>>();
                float upper_interval = args[1].get<float>();
                int step_count = args[2].get<int>();
                auto got = ImpactAreaStageDamage::extrapolate_from_above_at_index_location(input, upper_interval,
                                                                                             step_count);
                std::vector<double> got_d(got.begin(), got.end());
                std::vector<double> exp = a["expected"].get<std::vector<double>>();
                double tol = a["tol"].get<double>();
                std::string mode = a["mode"].get<std::string>();
                if (!hecfda_test::compare_by_mode(got_d, exp, tol, mode)) {
                    FAIL((std::string("comparison failed for case: ") + c["name"].get<std::string>()).c_str());
                }
                continue;
            }
            if (method == "extrapolate_from_below") {
                std::vector<float> input = args[0].get<std::vector<float>>();
                float interval = args[1].get<float>();
                int i = args[2].get<int>();
                int num_interpolated = args[3].get<int>();
                auto got = ImpactAreaStageDamage::extrapolate_from_below_stages_at_index_location(
                    input, interval, i, num_interpolated);
                std::vector<double> got_d(got.begin(), got.end());
                std::vector<double> exp = a["expected"].get<std::vector<double>>();
                double tol = a["tol"].get<double>();
                std::string mode = a["mode"].get<std::string>();
                if (!hecfda_test::compare_by_mode(got_d, exp, tol, mode)) {
                    FAIL((std::string("comparison failed for case: ") + c["name"].get<std::string>()).c_str());
                }
                continue;
            }

            // 'tractable_geometry': build a fresh ImpactAreaStageDamage per assertion (cheap, and
            // matches this file's "fresh construction per assertion" convention).
            int impact_area_id = ctor["impact_area_id"].get<int>();
            std::vector<double> probabilities = ctor["probabilities"].get<std::vector<double>>();
            std::vector<double> graphical_stages = ctor["graphical_stages"].get<std::vector<double>>();
            int erl = ctor["equivalent_record_length"].get<int>();
            float stage1 = ctor["hydraulic_stage1"].get<float>();
            float stage2 = ctor["hydraulic_stage2"].get<float>();

            CurveMetaData stage_frequency_metadata("probability", "stages", "graphical stage frequency");
            GraphicalUncertainPairedData stage_frequency(probabilities, graphical_stages, erl,
                                                          stage_frequency_metadata, /*using_stages_not_flows=*/true);

            auto inventory = make_tractable_residential_inventory(impact_area_id);
            auto wses_by_profile = make_mock_wses_by_profile(stage1, stage2, probabilities.size());
            HydraulicProfiles hydraulics(probabilities, wses_by_profile);

            ImpactAreaStageDamage impact_area_stage_damage(impact_area_id, std::move(inventory),
                                                            std::move(hydraulics), /*analysis_year=*/9999,
                                                            /*analytical_flow_frequency=*/nullptr, &stage_frequency,
                                                            /*discharge_stage=*/nullptr,
                                                            /*unregulated_regulated=*/nullptr,
                                                            /*using_mock_data=*/true);

            std::vector<double> got;
            if (method == "compute_stages_at_index_location") {
                got = impact_area_stage_damage.compute_stages_at_index_location(
                    impact_area_stage_damage.hydraulics().profile_probabilities());
            } else if (method == "bottom_extrapolation_points") {
                got = {static_cast<double>(impact_area_stage_damage.bottom_extrapolation_points())};
            } else if (method == "central_interpolation_points") {
                got = {static_cast<double>(impact_area_stage_damage.central_interpolation_points())};
            } else if (method == "top_extrapolation_points") {
                got = {static_cast<double>(impact_area_stage_damage.top_extrapolation_points())};
            } else if (method == "min_stage_for_area") {
                got = {impact_area_stage_damage.min_stage_for_area()};
            } else if (method == "max_stage_for_area") {
                got = {impact_area_stage_damage.max_stage_for_area()};
            } else {
                FAIL((std::string("unknown stage_damage_geometry method: ") + method).c_str());
            }

            std::vector<double> exp;
            if (a["expected"].is_array()) {
                exp = a["expected"].get<std::vector<double>>();
            } else {
                exp = {a["expected"].get<double>()};
            }
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + method;
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// ImpactAreaStageDamage.Compute() -- Phase 4 Task 7, the headline compute-loop oracle
// (TractableStageDamageTests.TrackStageDamageTest). Reuses make_tractable_residential_inventory
// (Task 6, above -- structures fid 1/2, FFE 14/15, val 100/200, ground 12, Residential occ type
// with CSVR 50) verbatim for the Residential cases; adds a Commercial counterpart below (fid 3/4,
// FFE 17/18, val 300/400, Commercial occ type with CSVR 120, reusing the residential CONTENT curve
// {0,5,15,...,95} as the commercial STRUCTURE curve, matching TractableStageDamageTests'
// `residentialContentAndCommercialStructureDamage` array reuse). See
// fixtures/stage_damage/impact_area_stage_damage.json's note for why building each case's Inventory
// with only the ONE occupancy type its structures reference (rather than upstream's single shared
// dictionary carrying both Residential and Commercial into every Inventory) is numerically
// equivalent.
static hecfda::model::structures::Inventory make_tractable_commercial_inventory(int impact_area_id) {
    using namespace hecfda::model::structures;
    using hecfda::model::paired_data::UncertainPairedData;
    using hecfda::statistics::distributions::Deterministic;
    using hecfda::statistics::distributions::IDistribution;

    std::vector<double> depths = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    // ported from: TractableStageDamageTests.cs's residentialContentAndCommercialStructureDamage
    // (reused verbatim as the COMMERCIAL STRUCTURE curve, matching upstream's array reuse).
    std::vector<double> struct_damage_vals = {0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95};
    std::vector<double> content_damage_vals = {0, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90};

    auto make_deterministic_upd = [](const std::vector<double>& xs, const std::vector<double>& vals) {
        std::vector<std::unique_ptr<IDistribution>> ys;
        ys.reserve(vals.size());
        for (double v : vals) ys.push_back(std::make_unique<Deterministic>(v));
        return UncertainPairedData(xs, std::move(ys));
    };

    OccupancyType commercial =
        OccupancyType::builder()
            .with_name("Commercial")
            .with_damage_category("Commercial")
            .with_structure_depth_percent_damage(make_deterministic_upd(depths, struct_damage_vals))
            .with_content_depth_percent_damage(make_deterministic_upd(depths, content_damage_vals))
            .with_content_to_structure_value_ratio(ValueRatioWithUncertainty(120))
            .build();

    std::map<std::string, OccupancyType> occ_types;
    occ_types.emplace("Commercial", std::move(commercial));

    std::vector<Structure> structures;
    structures.push_back(Structure("3", /*first_floor_elevation=*/17, /*val_struct=*/300, "Commercial",
                                    "Commercial", impact_area_id, /*val_cont=*/0, /*val_vehic=*/0,
                                    /*val_other=*/0, "unassigned", Structure::kDefaultMissingValue,
                                    /*ground_elevation=*/12));
    structures.push_back(Structure("4", /*first_floor_elevation=*/18, /*val_struct=*/400, "Commercial",
                                    "Commercial", impact_area_id, /*val_cont=*/0, /*val_vehic=*/0,
                                    /*val_other=*/0, "unassigned", Structure::kDefaultMissingValue,
                                    /*ground_elevation=*/12));

    return Inventory(std::move(occ_types), std::move(structures));
}

TEST_CASE("impact_area_stage_damage fixture") {
    using hecfda::model::paired_data::CurveMetaData;
    using hecfda::model::paired_data::GraphicalUncertainPairedData;
    using hecfda::model::paired_data::UncertainPairedData;
    using hecfda::model::stage_damage::HydraulicProfiles;
    using hecfda::statistics::distributions::Deterministic;
    using hecfda::statistics::distributions::IDistribution;

    std::ifstream f(fixtures_dir() + "/stage_damage/impact_area_stage_damage.json");
    REQUIRE(f.good());
    json fx;
    f >> fx;
    CHECK(fx["target"] == "impact_area_stage_damage");

    const std::vector<double> probabilities = {.5, .2, .1, .04, .02, .01, .004, .002};

    for (const auto& c : fx["cases"]) {
        const auto& ctor = c["construct"];
        int impact_area_id = ctor["impact_area_id"].get<int>();
        std::string damage_category = ctor["damage_category"].get<std::string>();
        std::string asset_category = ctor["asset_category"].get<std::string>();
        float stage1 = ctor["hydraulic_stage1"].get<float>();
        float stage2 = ctor["hydraulic_stage2"].get<float>();
        bool use_reg_unreg = ctor["use_reg_unreg"].get<bool>();

        for (const auto& a : c["assertions"]) {
            std::string method = a["method"].get<std::string>();
            REQUIRE(method == "f");
            double stage = a["args"][0].get<double>();

            // Fresh construction per assertion, matching this file's established convention.
            hecfda::model::structures::Inventory inventory =
                damage_category == "Residential" ? make_tractable_residential_inventory(impact_area_id)
                                                  : make_tractable_commercial_inventory(impact_area_id);
            auto wses_by_profile = make_mock_wses_by_profile(stage1, stage2, probabilities.size());
            HydraulicProfiles hydraulics(probabilities, wses_by_profile);

            // use_reg_unreg=false: a graphical STAGE frequency directly. use_reg_unreg=true: a
            // graphical FLOW frequency + dischargeStage + unregulatedRegulated, which compose to
            // the identical central stage-frequency curve (see the fixture's note).
            std::vector<double> graphical_stages = {12, 13, 14, 15, 16, 17, 18, 19};
            CurveMetaData stage_frequency_md("probability", "stages", "graphical stage frequency");
            GraphicalUncertainPairedData stage_frequency(probabilities, graphical_stages, /*erl=*/50,
                                                          stage_frequency_md,
                                                          /*using_stages_not_flows=*/true);

            std::vector<double> inflows = {1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900};
            CurveMetaData flow_frequency_md("probability", "discharge", "graphical flow frequency");
            GraphicalUncertainPairedData flow_frequency(probabilities, inflows, /*erl=*/50, flow_frequency_md,
                                                         /*using_stages_not_flows=*/false);

            std::vector<double> outflow_vals = {120, 130, 140, 150, 160, 170, 180, 190};
            std::vector<std::unique_ptr<IDistribution>> outflow_ys;
            for (double v : outflow_vals) outflow_ys.push_back(std::make_unique<Deterministic>(v));
            UncertainPairedData unreg_reg(inflows, std::move(outflow_ys));

            std::vector<double> flows = {120, 130, 140, 150, 160, 170, 180, 190};
            std::vector<double> stage_vals = {12, 13, 14, 15, 16, 17, 18, 19};
            std::vector<std::unique_ptr<IDistribution>> stage_ys;
            for (double v : stage_vals) stage_ys.push_back(std::make_unique<Deterministic>(v));
            UncertainPairedData discharge_stage(flows, std::move(stage_ys));

            using ImpactAreaStageDamage = hecfda::model::stage_damage::ImpactAreaStageDamage;
            ImpactAreaStageDamage impact_area_stage_damage(
                impact_area_id, std::move(inventory), std::move(hydraulics), /*analysis_year=*/9999,
                /*analytical_flow_frequency=*/nullptr,
                use_reg_unreg ? &flow_frequency : &stage_frequency,
                use_reg_unreg ? &discharge_stage : nullptr, use_reg_unreg ? &unreg_reg : nullptr,
                /*using_mock_data=*/true);

            auto compute_result = impact_area_stage_damage.compute(/*compute_is_deterministic=*/true);

            const UncertainPairedData* target = nullptr;
            for (const auto& upd : compute_result.first) {
                if (upd.metadata().damage_category() == damage_category &&
                    upd.metadata().asset_category() == asset_category) {
                    target = &upd;
                    break;
                }
            }
            REQUIRE(target != nullptr);
            auto sampled = target->sample_paired_data(1, true);
            double got = sampled.f(stage);

            double expected = a["expected"].get<double>();
            double tol = a["tol"].get<double>();
            std::string mode = a["mode"].get<std::string>();
            if (!hecfda_test::compare_by_mode({got}, {expected}, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " stage: " + std::to_string(stage);
                FAIL(msg.c_str());
            }
        }
    }
}

// ported from: fixtures/stage_damage/scenario_stage_damage.json's note (Phase 4 Task 8, the
// ScenarioStageDamage outer-loop oracle). Builds ONE ImpactAreaStageDamage per
// `construct.impact_areas[i]` entry, exactly like the impact_area_stage_damage fixture above
// (same tractable Residential/Commercial inventories, mock hydraulics, and graphical
// stage-frequency construction), but returns it BY VALUE from a small helper function rather than
// keeping the local GraphicalUncertainPairedData/UncertainPairedData frequency objects alive in an
// enclosing scope -- safe because ImpactAreaStageDamage's ctor only DEREFERENCES those pointers
// during establish_aggregation_stages() (never re-reads them from compute()), per
// impact_area_stage_damage.hpp's OWNERSHIP class-comment note, and ImpactAreaStageDamage is itself
// copyable/movable (Task 6), so returning it by value out of this helper is safe once its own
// local frequency objects go out of scope.
static hecfda::model::stage_damage::ImpactAreaStageDamage build_impact_area_stage_damage_for_scenario(
    const json& ctor) {
    using hecfda::model::paired_data::CurveMetaData;
    using hecfda::model::paired_data::GraphicalUncertainPairedData;
    using hecfda::model::paired_data::UncertainPairedData;
    using hecfda::model::stage_damage::HydraulicProfiles;
    using hecfda::model::stage_damage::ImpactAreaStageDamage;
    using hecfda::statistics::distributions::Deterministic;
    using hecfda::statistics::distributions::IDistribution;

    const std::vector<double> probabilities = {.5, .2, .1, .04, .02, .01, .004, .002};

    int impact_area_id = ctor["impact_area_id"].get<int>();
    std::string damage_category = ctor["damage_category"].get<std::string>();
    float stage1 = ctor["hydraulic_stage1"].get<float>();
    float stage2 = ctor["hydraulic_stage2"].get<float>();
    bool use_reg_unreg = ctor["use_reg_unreg"].get<bool>();

    hecfda::model::structures::Inventory inventory =
        damage_category == "Residential" ? make_tractable_residential_inventory(impact_area_id)
                                          : make_tractable_commercial_inventory(impact_area_id);
    auto wses_by_profile = make_mock_wses_by_profile(stage1, stage2, probabilities.size());
    HydraulicProfiles hydraulics(probabilities, wses_by_profile);

    std::vector<double> graphical_stages = {12, 13, 14, 15, 16, 17, 18, 19};
    CurveMetaData stage_frequency_md("probability", "stages", "graphical stage frequency");
    GraphicalUncertainPairedData stage_frequency(probabilities, graphical_stages, /*erl=*/50, stage_frequency_md,
                                                  /*using_stages_not_flows=*/true);

    std::vector<double> inflows = {1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900};
    CurveMetaData flow_frequency_md("probability", "discharge", "graphical flow frequency");
    GraphicalUncertainPairedData flow_frequency(probabilities, inflows, /*erl=*/50, flow_frequency_md,
                                                 /*using_stages_not_flows=*/false);

    std::vector<double> outflow_vals = {120, 130, 140, 150, 160, 170, 180, 190};
    std::vector<std::unique_ptr<IDistribution>> outflow_ys;
    for (double v : outflow_vals) outflow_ys.push_back(std::make_unique<Deterministic>(v));
    UncertainPairedData unreg_reg(inflows, std::move(outflow_ys));

    std::vector<double> flows = {120, 130, 140, 150, 160, 170, 180, 190};
    std::vector<double> stage_vals = {12, 13, 14, 15, 16, 17, 18, 19};
    std::vector<std::unique_ptr<IDistribution>> stage_ys;
    for (double v : stage_vals) stage_ys.push_back(std::make_unique<Deterministic>(v));
    UncertainPairedData discharge_stage(flows, std::move(stage_ys));

    return ImpactAreaStageDamage(impact_area_id, std::move(inventory), std::move(hydraulics),
                                  /*analysis_year=*/9999, /*analytical_flow_frequency=*/nullptr,
                                  use_reg_unreg ? &flow_frequency : &stage_frequency,
                                  use_reg_unreg ? &discharge_stage : nullptr,
                                  use_reg_unreg ? &unreg_reg : nullptr, /*using_mock_data=*/true);
}

TEST_CASE("scenario_stage_damage fixture") {
    using hecfda::model::paired_data::UncertainPairedData;
    using hecfda::model::stage_damage::ImpactAreaStageDamage;
    using hecfda::model::stage_damage::ScenarioStageDamage;

    std::ifstream f(fixtures_dir() + "/stage_damage/scenario_stage_damage.json");
    REQUIRE(f.good());
    json fx;
    f >> fx;
    CHECK(fx["target"] == "scenario_stage_damage");

    for (const auto& c : fx["cases"]) {
        std::vector<ImpactAreaStageDamage> impact_area_stage_damages;
        for (const auto& ia : c["construct"]["impact_areas"]) {
            impact_area_stage_damages.push_back(build_impact_area_stage_damage_for_scenario(ia));
        }
        ScenarioStageDamage scenario(std::move(impact_area_stage_damages));
        // Built and computed ONCE per case, shared across all of that case's assertions (matches
        // this file's other multi-assertion-per-object fixtures, e.g. aggregated_consequences_binned).
        auto compute_result = scenario.compute(/*compute_is_deterministic=*/true);

        json case_select = c.value("select", json::object());

        for (const auto& a : c["assertions"]) {
            std::string method = a["method"].get<std::string>();
            double tol = a["tol"].get<double>();
            std::string mode = a["mode"].get<std::string>();
            double expected = a["expected"].get<double>();

            double got;
            if (method == "result_count") {
                got = static_cast<double>(compute_result.first.size());
            } else if (method == "f") {
                const json& sel = a.contains("select") ? a["select"] : case_select;
                int sel_impact_area_id = sel["impact_area_id"].get<int>();
                std::string sel_damage_category = sel["damage_category"].get<std::string>();
                std::string sel_asset_category = sel["asset_category"].get<std::string>();

                const UncertainPairedData* target = nullptr;
                for (const auto& upd : compute_result.first) {
                    if (upd.metadata().impact_area_id() == sel_impact_area_id &&
                        upd.metadata().damage_category() == sel_damage_category &&
                        upd.metadata().asset_category() == sel_asset_category) {
                        target = &upd;
                        break;
                    }
                }
                REQUIRE(target != nullptr);
                double stage = a["args"][0].get<double>();
                auto sampled = target->sample_paired_data(1, true);
                got = sampled.f(stage);
            } else {
                FAIL((std::string("unknown scenario_stage_damage method: ") + method).c_str());
                continue;
            }

            if (!hecfda_test::compare_by_mode({got}, {expected}, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + method;
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bespoke dispatch for CategoriedPairedData + CategoriedUncertainPairedData (Phase 5 Task 4): the
// per-(damageCategory, assetCategory, ConsequenceType, RiskType) damage/FN-frequency curve
// accumulator the EAD compute loop batches MC realizations into. `construct` is EITHER
// {xvals, damage_category, asset_category, consequence_type, risk_type, convergence:
// {min_iterations, max_iterations}} (the 6-arg compute ctor) OR {initial_curve: {xvals, yvals,
// damage_category, asset_category, consequence_type, risk_type}, convergence: {min_iterations,
// max_iterations}} (the CategoriedPairedData-delegating ctor); ConvergenceCriteria uses the 2-arg
// (minIterations, maxIterations) ctor, same convention as run_aggregated_consequences_binned.
// `realization_batches` is a list of batches, each a list of {iteration, yvals}: for every batch,
// construct a fresh PairedData(xvals, yvals) per realization and add_curve_realization it in
// order, THEN put_data_into_histograms() exactly once per batch -- one
// CategoriedUncertainPairedData is built and staged per case, shared across every batch and
// assertion (mirrors run_aggregated_consequences_binned/run_study_area_consequences_binned's
// "one staged object per case" convention). `method` is always
// sample_paired_data_deterministic_yvals (args []): get_uncertain_paired_data().sample_paired_data(
// 1, true)'s (deterministic, monotonicity-forced) Yvals. See
// fixtures/metrics/categoried_uncertain_paired_data.json's note for what each case exercises (the
// 0.001-literal vs range/INITIAL_BIN_QUANTITY bin-width branches, the multi-flush-batch
// accumulation path, the CategoriedPairedData-delegating ctor, and the staged-array zero-
// contamination quirk).
static hecfda::model::metrics::CategoriedUncertainPairedData make_categoried_uncertain_paired_data(
    const json& ctor) {
    using namespace hecfda::model::metrics;
    const auto& conv = ctor["convergence"];
    hecfda::statistics::ConvergenceCriteria cc(conv["min_iterations"].get<int>(),
                                                conv["max_iterations"].get<int>());
    if (ctor.contains("initial_curve")) {
        const auto& ic = ctor["initial_curve"];
        hecfda::model::paired_data::PairedData curve(ic["xvals"].get<std::vector<double>>(),
                                                       ic["yvals"].get<std::vector<double>>());
        CategoriedPairedData initial(std::move(curve), ic["damage_category"].get<std::string>(),
                                      ic["asset_category"].get<std::string>(),
                                      parse_consequence_type(ic["consequence_type"].get<std::string>()),
                                      parse_risk_type(ic["risk_type"].get<std::string>()));
        return CategoriedUncertainPairedData(initial, cc);
    }
    return CategoriedUncertainPairedData(
        ctor["xvals"].get<std::vector<double>>(), ctor["damage_category"].get<std::string>(),
        ctor["asset_category"].get<std::string>(),
        parse_consequence_type(ctor["consequence_type"].get<std::string>()),
        parse_risk_type(ctor["risk_type"].get<std::string>()), cc);
}

static std::vector<double> run_categoried_uncertain_paired_data(const json& c, const std::string& method) {
    using namespace hecfda::model::metrics;
    CategoriedUncertainPairedData cupd = make_categoried_uncertain_paired_data(c["construct"]);
    for (const auto& batch : c["realization_batches"]) {
        for (const auto& r : batch) {
            hecfda::model::paired_data::PairedData curve(cupd.xvals(),
                                                           r["yvals"].get<std::vector<double>>());
            cupd.add_curve_realization(curve, r["iteration"].get<std::int64_t>());
        }
        cupd.put_data_into_histograms();
    }
    if (method == "sample_paired_data_deterministic_yvals") {
        return cupd.get_uncertain_paired_data().sample_paired_data(1, true).yvals();
    }
    auto msg = std::string("unknown categoried_uncertain_paired_data method: ") + method;
    FAIL(msg.c_str());
    return {};
}

TEST_CASE("categoried_uncertain_paired_data fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/categoried_uncertain_paired_data.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "categoried_uncertain_paired_data");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_categoried_uncertain_paired_data(c, a["method"].get<std::string>());
            std::vector<double> exp = a["expected"].get<std::vector<double>>();
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bespoke dispatch for ContinuousDistribution::sample(iteration) + bootstrap_to_paired_data
// (Phase 5 Task 5) -- the analytical-frequency realization Task 8's EAD compute uses to turn a
// fitted flow-frequency distribution (e.g. LogPearson3) into a PairedData flow-frequency curve,
// either deterministically (the distribution's own fit) or via a seeded parametric bootstrap
// resample. `construct` is {mean, standard_deviation, skew, sample_size} for
// LogPearson3(mean, standard_deviation, skew, sample_size); `seed` + `quantity_of_samples`, if
// present, call generate_random_samples_of_numbers(seed, quantity_of_samples) before
// bootstrap_to_paired_data (the seeded case). `iteration_number` + `compute_is_deterministic` are
// passed straight through to bootstrap_to_paired_data, along with
// hecfda::statistics::distributions::required_exceedance_probabilities() (the fixed 173-point
// grid -- see fixtures/compute/bootstrap_to_paired_data.json's note for why it's never restated
// per-case). `method` is always bootstrap_to_paired_data_yvals (args []): the resulting
// PairedData's Yvals.
static std::vector<double> run_bootstrap_to_paired_data(const json& c) {
    using namespace hecfda::statistics::distributions;
    const auto& ctor = c["construct"];
    LogPearson3 lp3(ctor["mean"].get<double>(), ctor["standard_deviation"].get<double>(),
                     ctor["skew"].get<double>(), ctor["sample_size"].get<long>());
    if (c.contains("seed")) {
        lp3.generate_random_samples_of_numbers(c["seed"].get<int>(), c["quantity_of_samples"].get<int>());
    }
    hecfda::model::paired_data::PairedData pd = bootstrap_to_paired_data(
        lp3, c["iteration_number"].get<long>(), required_exceedance_probabilities(),
        c["compute_is_deterministic"].get<bool>());
    return pd.yvals();
}

TEST_CASE("bootstrap_to_paired_data fixture") {
    std::ifstream f(fixtures_dir() + "/compute/bootstrap_to_paired_data.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "bootstrap_to_paired_data");
    for (const auto& c : fx["cases"]) {
        auto got = run_bootstrap_to_paired_data(c);
        for (const auto& a : c["assertions"]) {
            CHECK(a["method"].get<std::string>() == "bootstrap_to_paired_data_yvals");
            std::vector<double> exp = a["expected"].get<std::vector<double>>();
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode(got, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bespoke dispatch for ImpactAreaScenarioResults (Phase 5 Task 6): the compute-output container
// holding one PerformanceByThresholds + one StudyAreaConsequencesBinned. `construct` is
// {impact_area_id, damage_category, asset_category, consequence_convergence: {min_iterations,
// max_iterations}, threshold: {id, type (ThresholdEnum name string), value, convergence:
// {min_iterations, max_iterations}}}. Builds a fresh ImpactAreaScenarioResults(impact_area_id)
// (the 1-arg public ctor), add_threshold's a Threshold built from `threshold`, and
// add_new_consequence_result_object's ONE (damage_category, asset_category) combo into
// consequence_results() with ConsequenceType::Damage/RiskType::Total (Total, not
// StudyAreaConsequencesBinned's own Fail default -- matches the RiskType::Total default
// mean_expected_annual_consequences itself uses at the ImpactAreaScenarioResults level; see
// fixtures/metrics/impact_area_scenario_results.json's note). `consequence_realizations`
// ({iteration, damage}) feed consequence_results().add_consequence_realization(...) (the
// EAD-binning overload, same ConsequenceType::Damage/RiskType::Total), then
// consequence_results().put_data_into_histograms() runs once. `aep_observations` ({iteration,
// result}) feed the retrieved threshold's system_performance_results().add_aep_for_assurance(...),
// then that same system_performance_results().put_data_into_histograms() runs once. One object is
// built and staged per case, shared across every assertion (mirrors run_performance_by_thresholds/
// run_study_area_consequences_binned's "one staged object per case" convention). `method`
// dispatches mean_aep/median_aep (args [threshold_id]), long_term_exceedance_probability (args
// [threshold_id, years]), assurance_of_aep (args [threshold_id, exceedance_probability]),
// mean_expected_annual_consequences (args [impact_area_id], damage_category/asset_category/
// ConsequenceType::Damage/RiskType::Total from construct), and results_are_converged (args
// [upper, lower], mode bool -- ImpactAreaScenarioResults::results_are_converged(upper, lower,
// /*check_consequence_results=*/true), returned as 1.0/0.0).
static double run_impact_area_scenario_results(const json& c, const std::string& method, const json& args) {
    using namespace hecfda::model::metrics;
    using hecfda::statistics::ConvergenceCriteria;

    const auto& ctor = c["construct"];
    int impact_area_id = ctor["impact_area_id"].get<int>();
    std::string damage_category = ctor["damage_category"].get<std::string>();
    std::string asset_category = ctor["asset_category"].get<std::string>();

    ImpactAreaScenarioResults results(impact_area_id);

    const auto& t = ctor["threshold"];
    int threshold_id = t["id"].get<int>();
    ThresholdEnum threshold_type = threshold_enum_from_name(t["type"].get<std::string>());
    double threshold_value = t["value"].get<double>();
    const auto& t_conv = t["convergence"];
    ConvergenceCriteria threshold_cc(t_conv["min_iterations"].get<int>(), t_conv["max_iterations"].get<int>());
    results.performance_by_thresholds().add_threshold(
        Threshold(threshold_id, threshold_cc, threshold_type, threshold_value));

    const auto& cons_conv = ctor["consequence_convergence"];
    ConvergenceCriteria consequence_cc(cons_conv["min_iterations"].get<int>(), cons_conv["max_iterations"].get<int>());
    results.consequence_results().add_new_consequence_result_object(
        damage_category, asset_category, consequence_cc, impact_area_id, ConsequenceType::Damage, RiskType::Total);

    for (const auto& r : c["consequence_realizations"]) {
        results.consequence_results().add_consequence_realization(
            r["damage"].get<double>(), damage_category, asset_category, impact_area_id,
            r["iteration"].get<std::int64_t>(), ConsequenceType::Damage, RiskType::Total);
    }
    results.consequence_results().put_data_into_histograms();

    Threshold& threshold = results.performance_by_thresholds().get_threshold(threshold_id);
    threshold.system_performance_results().add_stage_assurance_histogram(0.98);
    for (const auto& o : c["aep_observations"]) {
        threshold.system_performance_results().add_aep_for_assurance(o["result"].get<double>(),
                                                                        o["iteration"].get<int>());
    }
    for (const auto& o : c["stage_observations"]) {
        threshold.system_performance_results().add_stage_for_assurance(0.98, o["result"].get<double>(),
                                                                          o["iteration"].get<int>());
    }
    threshold.system_performance_results().put_data_into_histograms();

    if (method == "mean_aep") return results.mean_aep(args[0].get<int>());
    if (method == "median_aep") return results.median_aep(args[0].get<int>());
    if (method == "long_term_exceedance_probability") {
        return results.long_term_exceedance_probability(args[0].get<int>(), args[1].get<int>());
    }
    if (method == "assurance_of_aep") {
        return results.assurance_of_aep(args[0].get<int>(), args[1].get<double>());
    }
    if (method == "mean_expected_annual_consequences") {
        return results.mean_expected_annual_consequences(args[0].get<int>(), damage_category, asset_category,
                                                           ConsequenceType::Damage, RiskType::Total);
    }
    if (method == "results_are_converged") {
        bool converged =
            results.results_are_converged(args[0].get<double>(), args[1].get<double>(), /*check_consequence_results=*/true);
        return converged ? 1.0 : 0.0;
    }
    auto msg = std::string("unknown impact_area_scenario_results method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("impact_area_scenario_results fixture") {
    std::ifstream f(fixtures_dir() + "/metrics/impact_area_scenario_results.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "impact_area_scenario_results");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_impact_area_scenario_results(c, a["method"].get<std::string>(), a["args"]);
            std::vector<double> exp = {a["expected"].get<double>()};
            std::string mode = a["mode"].get<std::string>();
            double tol = a["tol"].get<double>();
            if (!hecfda_test::compare_by_mode({got}, exp, tol, mode)) {
                auto msg = std::string("comparison failed for case: ") + c["name"].get<std::string>() +
                           " method: " + a["method"].get<std::string>();
                FAIL(msg.c_str());
            }
        }
    }
}
