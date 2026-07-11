// ported from: HEC.FDA.Model/paireddata/IMultiply.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_MULTIPLY_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_MULTIPLY_HPP
namespace hecfda {
namespace model {
namespace paired_data {
class IPairedData;
class PairedData;
// ported from: IMultiply.cs -- `multiply(IPairedData g)` returns the CONCRETE PairedData type
// in the C# source (not IPairedData), preserved as-is.
class IMultiply {
   public:
    virtual ~IMultiply() = default;
    virtual PairedData multiply(const IPairedData& g) const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_MULTIPLY_HPP
