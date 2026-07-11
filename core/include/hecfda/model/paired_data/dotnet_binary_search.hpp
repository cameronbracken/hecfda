// ported from: .NET Array.BinarySearch<T> @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_DOTNET_BINARY_SEARCH_HPP
#define HECFDA_MODEL_PAIRED_DATA_DOTNET_BINARY_SEARCH_HPP
#include <vector>
namespace hecfda {
namespace model {
namespace paired_data {

// ported from: System.Array.BinarySearch<T>(T[] array, T value) as it resolves for T = double,
// i.e. the generic ArraySortHelper<T>.InternalBinarySearch(T[] array, int index, int length, T
// value, IComparer<T> comparer) core loop with the default comparer
// (Comparer<double>.Default == double.CompareTo). .NET semantics, NOT std::lower_bound:
// lower_bound always returns the FIRST element equal to `value` in a run of duplicates;
// Array.BinarySearch returns an ARBITRARY-but-deterministic (midpoint-driven) matching index
// within that run, since it never keeps searching once `arr[i] == value` is found. On arrays with
// duplicate values (flat frequency/damage segments, ubiquitous in real FDA curves) the two
// disagree about WHICH index matches, which can select a different interpolation neighbor
// downstream. Every call site in the real C# (PairedData.f/f_inverse, Empirical.CDF/InverseCDF)
// decodes the "not found" result the same way: `int index = Array.BinarySearch(...); if (index <
// 0) index = ~index;` -- callers here should do the same with the bitwise-complement `~` operator
// on the returned `long`.
//
// Not found encoding: `~lo`, where `lo` is the index of the first element greater than `value`
// (or `arr.size()` if `value` exceeds every element). `~lo == -lo - 1`, which is always negative
// for `lo >= 0`, so `index >= 0` unambiguously means "found" and `index < 0` means "insertion
// point encoded, decode with `~index`" -- exactly mirroring the C# branch shape.
//
// CompareTo / NaN semantics: `double.CompareTo(double)` (what `Comparer<double>.Default` uses)
// orders NaN as LESS than every other value including negative infinity, and treats -0.0 and +0.0
// as equal via ordinary IEEE comparison (not bit equality) -- so `<`/`>` already match .NET for
// every finite/infinite value and for -0.0 vs +0.0, but NOT for NaN, since IEEE `<`/`>` are always
// false when either operand is NaN (which would make the `c == 0` branch below fire and treat NaN
// as "equal" to anything, unlike .NET's "NaN sorts below everything" rule). PairedData's x/y
// arrays and Empirical's Quantiles/CumulativeProbabilities arrays are curve coordinates -- FDA
// never constructs a curve with a NaN coordinate, and no caller of `f`/`f_inverse`/`CDF`/
// `InverseCDF` in the ported surface passes a NaN query value -- so NaN cannot occur in practice
// here. On that basis this port keeps the simple IEEE `<`/`>` comparison (matching the
// already-shipped Empirical.hpp private `binary_search` helper this function supersedes) rather
// than adding NaN special-casing that no real caller can exercise.
inline long dotnet_binary_search(const std::vector<double>& arr, double value) {
    long lo = 0;
    long hi = static_cast<long>(arr.size()) - 1;
    while (lo <= hi) {
        long i = lo + ((hi - lo) >> 1);
        double v = arr[static_cast<std::size_t>(i)];
        int c = (v < value) ? -1 : (v > value ? 1 : 0);
        if (c == 0) return i;
        if (c < 0) {
            lo = i + 1;
        } else {
            hi = i - 1;
        }
    }
    return ~lo;
}

}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_DOTNET_BINARY_SEARCH_HPP
