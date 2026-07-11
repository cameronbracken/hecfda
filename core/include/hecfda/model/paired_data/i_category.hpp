// ported from: HEC.FDA.Model/paireddata/ICategory.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_CATEGORY_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_CATEGORY_HPP
#include <string>
namespace hecfda {
namespace model {
namespace paired_data {
// Not yet implemented by any ported class in this phase (CategoriedPairedData, the C# type that
// implements it, is out of scope) -- ported ahead of need per the task brief since it is a
// dependency-free 2-accessor interface. Left unimplemented deliberately; a later task wires it up.
class ICategory {
   public:
    virtual ~ICategory() = default;
    virtual const std::string& damage_category() const = 0;
    virtual const std::string& asset_category() const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_CATEGORY_HPP
