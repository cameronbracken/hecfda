// ported from: HEC.FDA.Statistics/SampleStatistics.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_SAMPLE_STATISTICS_HPP
#define HECFDA_STATISTICS_SAMPLE_STATISTICS_HPP
#include <cmath>
#include <utility>
#include <vector>
namespace hecfda {
namespace statistics {

// Transcribed verbatim from SampleStatistics.cs's ctor/InitalizeStats. Two upstream quirks are
// faithful reproductions, not design choices -- confirmed against real C# via
// tools/oracle_emitter (see fixtures/distributions/sample_statistics.json):
//
// 1. `variance()`/`standard_deviation()`: the internal running `_sampleVariance` field (updated
//    by the Welford-style recurrence below) holds the classic Bessel-corrected (n-1 denominator)
//    sample variance, but the public `Variance` getter rescales it by (n-1)/n before returning.
//    That rescale turns it into the POPULATION (n-denominator) variance. So despite the class
//    name and the n-1 recurrence, `variance()`/`standard_deviation()` here return population
//    moments, not sample (n-1) moments.
// 2. `median()`: the C# ctor calls `Array.Sort(observations.ToArray())`, but `ToArray()` always
//    allocates a new copy, so the sort has no effect on the `observations` sequence actually
//    iterated afterward -- Median indexes into the ORIGINAL (caller-supplied) sample order, not
//    sorted order. For the NIST Mavro oracle dataset this (buggy) median is 2.00145, versus
//    2.0018 for the true sorted median.
class SampleStatistics {
   public:
    explicit SampleStatistics(std::vector<double> data) { initialize(std::move(data)); }

    double mean() const { return mean_; }
    double variance() const {
        return sample_variance_ * (static_cast<double>(n_ - 1) / static_cast<double>(n_));
    }
    double standard_deviation() const { return std::pow(variance(), 0.5); }
    double median() const { return median_; }
    double skewness() const { return skew_; }
    double min() const { return min_; }
    double max() const { return max_; }
    long sample_size() const { return n_; }

   private:
    // ported from: SampleStatistics.cs InitalizeStats(IEnumerable<double> observations)
    void initialize(std::vector<double> observations) {
        for (double observation : observations) {
            if (n_ == 0) {
                max_ = observation;
                min_ = observation;
                mean_ = observation;
                sample_variance_ = 0.0;
                n_ = 1;
            } else {
                if (observation > max_) max_ = observation;
                if (observation < min_) min_ = observation;
                n_ += 1;
                double obs_minus_mean = observation - mean_;
                double tmp_mean = mean_ + (obs_minus_mean / static_cast<double>(n_));
                sample_variance_ = ((static_cast<double>(n_ - 2) / static_cast<double>(n_ - 1)) * sample_variance_) +
                                    ((obs_minus_mean * obs_minus_mean) / static_cast<double>(n_));
                mean_ = tmp_mean;
            }
        }

        double s = std::pow(sample_variance_ * (static_cast<double>(n_ - 1) / static_cast<double>(n_)), 0.5);
        double skew_sums = 0.0;
        double midpoint = (static_cast<double>(n_) - 1) / 2.0;
        bool no_rounding = false;
        if (std::floor(midpoint) == midpoint) {
            no_rounding = true;
        } else {
            midpoint = std::floor(midpoint);
        }
        // NOTE: upstream sorts a throwaway copy here (`Array.Sort(observations.ToArray())`); the
        // loop below iterates `observations` in its ORIGINAL order, matching that (dead-sort) bug
        // verbatim -- see class comment above.
        long val = 0;
        bool firstpass = true;
        for (double observation : observations) {
            if (midpoint == static_cast<double>(val)) {
                if (no_rounding) {
                    median_ = observation;
                } else {
                    if (firstpass) {
                        midpoint += 1;
                        median_ += observation;
                        firstpass = false;
                    } else {
                        median_ += observation;
                        median_ /= 2.0;
                    }
                }
            }
            double obs_minus_mean = observation - mean_;
            skew_sums += obs_minus_mean * obs_minus_mean * obs_minus_mean;
            ++val;
        }
        double nd = static_cast<double>(n_);
        skew_ = (nd * skew_sums) / ((nd - 1.0) * (nd - 2.0) * (s * s * s));
    }

    double mean_ = 0.0;
    double sample_variance_ = 0.0;
    double min_ = 0.0;
    double max_ = 0.0;
    double median_ = 0.0;
    double skew_ = 0.0;
    long n_ = 0;
};

}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_SAMPLE_STATISTICS_HPP
