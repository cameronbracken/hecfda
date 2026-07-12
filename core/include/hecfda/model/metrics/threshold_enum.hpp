// ported from: HEC.FDA.Model/metrics/ThresholdEnum.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_THRESHOLD_ENUM_HPP
#define HECFDA_MODEL_METRICS_THRESHOLD_ENUM_HPP
namespace hecfda {
namespace model {
namespace metrics {

// ported from: ThresholdEnum.cs `public enum ThresholdEnum`, values transcribed verbatim
// (explicit C# numbering: NotSupported=0, DefaultExteriorStage=1, TopOfLevee=2,
// LeveeSystemResponse=3, AdditionalExteriorStage=4). The `[DisplayName(...)]` and
// `[StoredProperty(...)]` attributes (UI label / persistence-key metadata) and the sibling
// `ThresholdEnumExtensions.GetDisplayName` reflection helper are UI/serialization surface with no
// equivalent in this port -- severed, matching the repo-wide XML/UI severance convention. Only the
// bare enum is ported.
enum class ThresholdEnum {
    NotSupported = 0,
    DefaultExteriorStage = 1,
    TopOfLevee = 2,
    LeveeSystemResponse = 3,
    AdditionalExteriorStage = 4,
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_THRESHOLD_ENUM_HPP
