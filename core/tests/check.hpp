#ifndef HECFDA_TESTS_CHECK_HPP
#define HECFDA_TESTS_CHECK_HPP
#include <cmath>
#include <cstddef>
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
}  // namespace hecfda_test
#endif
