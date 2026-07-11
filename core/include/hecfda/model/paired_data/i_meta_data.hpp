// ported from: HEC.FDA.Model/paireddata/IMetaData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_META_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_META_DATA_HPP
#include "hecfda/model/paired_data/curve_meta_data.hpp"
namespace hecfda {
namespace model {
namespace paired_data {
// Not yet implemented by any ported class in this phase -- ported ahead of need per the task
// brief. UncertainPairedData (HEC.FDA.Model UncertainPairedData.cs) does not implement this
// interface even in the real C#; it exposes a plain `CurveMetaData` property instead.
class IMetaData {
   public:
    virtual ~IMetaData() = default;
    virtual const CurveMetaData& curve_meta_data() const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_META_DATA_HPP
