// ported from: HEC.FDA.Model/interfaces/ICanBeNull.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_INTERFACES_I_CAN_BE_NULL_HPP
#define HECFDA_MODEL_INTERFACES_I_CAN_BE_NULL_HPP
namespace hecfda {
namespace model {
namespace interfaces {
// ported from: ICanBeNull.cs -- a single `bool IsNull { get; }` accessor. First real implementer
// in this port is GraphicalUncertainPairedData (Task P2T4b), which forwards straight to its
// CurveMetaData's is_null() (matching the C# `CurveMetaData.IsNull` passthrough).
class ICanBeNull {
   public:
    virtual ~ICanBeNull() = default;
    virtual bool is_null() const = 0;
};
}  // namespace interfaces
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_INTERFACES_I_CAN_BE_NULL_HPP
