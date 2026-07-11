// ported from: HEC.FDA.Model/paireddata/ISample.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_SAMPLE_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_SAMPLE_HPP
namespace hecfda {
namespace model {
namespace paired_data {
// ported from: ISample.cs -- f/f_inverse/f(x, ref index) are all `const`-safe in this port
// (none of PairedData's implementations mutate the curve itself; the ref index is caller state).
class ISample {
   public:
    virtual ~ISample() = default;
    virtual double f(double x) const = 0;
    virtual double f_inverse(double y) const = 0;
    virtual double f(double x, int& index_of_previous_top_of_segment) const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_SAMPLE_HPP
