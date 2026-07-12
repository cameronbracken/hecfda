// ported from: HEC.FDA.Model/metrics/ConsequenceTypes.cs @ f63682a86a30dc306a105689714a92bfd95956c5
// ported from: HEC.FDA.Model/metrics/RiskType.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_CONSEQUENCE_TYPE_HPP
#define HECFDA_MODEL_METRICS_CONSEQUENCE_TYPE_HPP
namespace hecfda {
namespace model {
namespace metrics {

// ported from: ConsequenceTypes.cs `public enum ConsequenceType`, values transcribed verbatim
// (implicit C# numbering: UNASSIGNED=0, Damage=1, LifeLoss=2, All=3). Note the file name is
// plural ("ConsequenceTypes.cs") but the enum itself is singular (`ConsequenceType`) -- kept as
// upstream named it.
enum class ConsequenceType {
    UNASSIGNED = 0,
    Damage = 1,
    LifeLoss = 2,
    All = 3,
};

// ported from: RiskType.cs `public enum RiskType`, values transcribed verbatim (implicit C#
// numbering: Fail=0, Non_Fail=1, Total=2, Unassigned=3). `Non_Fail` keeps the C# underscore
// verbatim (not PascalCase `NonFail`).
enum class RiskType {
    Fail = 0,
    Non_Fail = 1,
    Total = 2,
    Unassigned = 3,
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_CONSEQUENCE_TYPE_HPP
