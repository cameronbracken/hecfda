#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fstream>
#include <string>
#include <vector>
#include "doctest.h"
#include "json.hpp"
#include "check.hpp"
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/model/paired_data/interpolate_quantiles.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/model/utilities/graphical_frequency_uncertainty_calculators.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
#include "hecfda/statistics/distributions/deterministic.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
#include "hecfda/statistics/distributions/i_distribution_factory.hpp"
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
