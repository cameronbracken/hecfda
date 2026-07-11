// ported from: HEC.FDA.Model/paireddata/IPairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_PAIRED_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_PAIRED_DATA_HPP
#include <vector>
#include "hecfda/model/paired_data/i_composable.hpp"
#include "hecfda/model/paired_data/i_integrate.hpp"
#include "hecfda/model/paired_data/i_multiply.hpp"
#include "hecfda/model/paired_data/i_sample.hpp"
namespace hecfda {
namespace model {
namespace paired_data {
// ported from: IPairedData.cs -- the C# source carries a "TODO: This class does not have utility
// in the current design / REMOVE" comment; ported verbatim regardless since PairedData still
// implements it and compose/SumYsForGivenX/multiply still take it as their parameter type.
// `IReadOnlyList<double> Xvals/Yvals` become `const std::vector<double>&`-returning accessors.
class IPairedData : public ISample, public IComposable, public IIntegrate, public IMultiply {
   public:
    virtual ~IPairedData() = default;
    virtual const std::vector<double>& xvals() const = 0;
    virtual const std::vector<double>& yvals() const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_PAIRED_DATA_HPP
