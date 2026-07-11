// ported from: HEC.FDA.Model/paireddata/IIntegrate.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_INTEGRATE_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_INTEGRATE_HPP
namespace hecfda {
namespace model {
namespace paired_data {
class IIntegrate {
   public:
    virtual ~IIntegrate() = default;
    virtual double integrate(bool with_padding = true) const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_INTEGRATE_HPP
