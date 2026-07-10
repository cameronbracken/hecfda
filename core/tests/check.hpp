#ifndef HECFDA_TESTS_CHECK_HPP
#define HECFDA_TESTS_CHECK_HPP
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>
namespace hecfda_test {
inline bool close_abs(double a, double b, double tol) {
    if (std::isnan(a) && std::isnan(b)) return true;
    return std::fabs(a - b) <= tol;
}
inline bool close_rel(double a, double b, double tol) {
    if (std::isnan(a) && std::isnan(b)) return true;
    double denom = std::fabs(b);
    if (denom == 0.0) return std::fabs(a) <= tol;
    return std::fabs(a - b) / denom <= tol;
}
inline bool close_vec(const std::vector<double>& a, const std::vector<double>& b, double tol) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!close_abs(a[i], b[i], tol)) return false;
    return true;
}
inline bool compare_by_mode(const std::vector<double>& got, const std::vector<double>& expected,
                            double tol, const std::string& mode) {
    if (mode == "vector") {
        return close_vec(got, expected, tol);
    } else if (mode == "rel") {
        if (got.size() != 1 || expected.size() != 1) return false;
        return close_rel(got[0], expected[0], tol);
    } else if (mode == "abs") {
        if (got.size() != 1 || expected.size() != 1) return false;
        return close_abs(got[0], expected[0], tol);
    } else if (mode == "exact") {
        if (got.size() != 1 || expected.size() != 1) return false;
        return close_abs(got[0], expected[0], 0.0);
    } else if (mode == "bool") {
        if (got.size() != 1 || expected.size() != 1) return false;
        bool got_bool = (got[0] != 0.0);
        bool exp_bool = (expected[0] != 0.0);
        return got_bool == exp_bool;
    } else {
        return false;  // Unknown mode; let caller handle via FAIL
    }
}
}  // namespace hecfda_test
#endif
