#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fstream>
#include <string>
#include <vector>
#include "doctest.h"
#include "json.hpp"
#include "check.hpp"
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/normal.hpp"

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

static double run_normal(const json& c, const std::string& method, const json& args) {
    const auto& ctor = c["construct"];
    hecfda::statistics::distributions::Normal dist(ctor["mean"].get<double>(), ctor["sd"].get<double>(),
                                                     ctor["sample_size"].get<long>());
    double x = args[0].get<double>();
    if (method == "pdf") return dist.pdf(x);
    if (method == "cdf") return dist.cdf(x);
    if (method == "inverse_cdf") return dist.inverse_cdf(x);
    auto msg = std::string("unknown distribution method: ") + method;
    FAIL(msg.c_str());
    return 0.0;
}

TEST_CASE("normal fixture") {
    std::ifstream f(fixtures_dir() + "/distributions/normal.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "normal");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            double got = run_normal(c, a["method"], a["args"]);
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
