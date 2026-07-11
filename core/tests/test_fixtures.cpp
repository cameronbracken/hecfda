#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fstream>
#include <string>
#include <vector>
#include "doctest.h"
#include "json.hpp"
#include "check.hpp"
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/distributions/i_distribution_factory.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/shifted_gamma.hpp"
#include "hecfda/statistics/distributions/triangular.hpp"
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

static double run_uncertain_paired_data(const json& c, const std::string& method) {
    const auto& ctor = c["construct"];
    std::vector<double> xs = ctor["xs"].get<std::vector<double>>();
    std::vector<hecfda::statistics::distributions::Normal> ys;
    for (const auto& y : ctor["ys"]) {
        ys.emplace_back(y["mean"].get<double>(), y["sd"].get<double>(), 1);
    }
    hecfda::model::paired_data::UncertainPairedData upd(xs, ys);
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
