// ported from: HEC.FDA.Statistics/Distributions/IDistributionEnum.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_ENUM_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_ENUM_HPP
#include <stdexcept>
#include <string>
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: IDistributionEnum.cs, values transcribed verbatim -- including the numeric gap
// between Empirical=8 and TruncatedNormal=101. NotSupported=0 is the C# "default value likely to
// lead to an error"; it has no IDistributionFactory case and is not constructible via
// IDistributionFactory::create.
//
// Concrete distribution classes whose C# `Type` getter returns a DIFFERENT distribution's enum
// value (e.g. `TruncatedLogNormal.Type => IDistributionEnum.Normal`,
// `TruncatedLogPearson3.Type => IDistributionEnum.LogPearsonIII`) are NOT separate enum members
// upstream -- confirmed by grep across every `Distributions/*.cs` file's `Type` getter, only the
// 9 values below are ever returned. No additional internal-only enum values are invented here.
enum class DistributionType {
    NotSupported = 0,
    Normal = 1,
    Uniform = 2,
    Triangular = 3,
    LogPearsonIII = 4,
    LogNormal = 5,
    Deterministic = 6,
    IHistogram = 7,
    Empirical = 8,
    TruncatedNormal = 101,
};

// Centralized string -> DistributionType mapping. Maps name (e.g. "Normal", "Uniform") to the
// corresponding enum value. Throws std::invalid_argument if name is unknown. Used by all
// distribution test/glue/binding layers to avoid triplication of the same string-matching logic.
inline DistributionType distribution_type_from_name(const std::string& name) {
    if (name == "Normal") return DistributionType::Normal;
    if (name == "Uniform") return DistributionType::Uniform;
    throw std::invalid_argument("distribution_type_from_name: unknown distribution type: " + name);
}

}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_ENUM_HPP
