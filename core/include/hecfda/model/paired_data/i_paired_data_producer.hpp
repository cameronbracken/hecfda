// ported from: HEC.FDA.Model/paireddata/IPairedDataProducer.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_I_PAIRED_DATA_PRODUCER_HPP
#define HECFDA_MODEL_PAIRED_DATA_I_PAIRED_DATA_PRODUCER_HPP
namespace hecfda {
namespace model {
namespace paired_data {
class PairedData;
// Implemented by UncertainPairedData in the real C# (SamplePairedData(long, bool) /
// SamplePairedData(double)); UncertainPairedData's C++ port is not yet wired to this interface --
// that generalization is Phase 2 Task 3. Ported ahead of need here since it is a
// dependency-free 2-method interface, per the task brief.
class IPairedDataProducer {
   public:
    virtual ~IPairedDataProducer() = default;
    virtual PairedData sample_paired_data(long iteration, bool compute_is_deterministic) const = 0;
    virtual PairedData sample_paired_data(double probability) const = 0;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_I_PAIRED_DATA_PRODUCER_HPP
