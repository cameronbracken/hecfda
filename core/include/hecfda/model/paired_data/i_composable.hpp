// ported from: HEC.FDA.Model/paireddata/IComposable.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_COMPOSABLE_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_COMPOSABLE_HPP
namespace hecfda {
namespace model {
namespace paired_data {
class IPairedData;
class PairedData;
// ported from: IComposable.cs -- `compose(IPairedData g)` returns the CONCRETE PairedData type
// in the C# source (not IPairedData), preserved as-is.
class IComposable {
   public:
    virtual ~IComposable() = default;
    virtual PairedData compose(const IPairedData& g) const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_COMPOSABLE_HPP
