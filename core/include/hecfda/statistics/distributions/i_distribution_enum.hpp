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
// 9 values below (NotSupported..TruncatedNormal) are ever returned by any type() override in this
// port. Those 9 are transcribed verbatim, including the numeric gap between Empirical=8 and
// TruncatedNormal=101.
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

    // Port-internal factory keys (NOT in C# IDistributionEnum). Used only by
    // distribution_type_from_name + IDistributionFactory::create to construct distributions whose
    // C# `Type` property aliases an existing enum value. The instance type() returns the faithful
    // C# value (e.g. TruncatedLogNormal::type() returns DistributionType::Normal), never one of
    // these. Reserve 1006+ for later enum-less distributions (PearsonIII, Gamma, ShiftedGamma,
    // TruncatedLogPearson3).
    TruncatedLogNormal = 1005,
};

// Centralized string -> DistributionType mapping. Maps name (e.g. "Normal", "Uniform") to the
// corresponding enum value. Throws std::invalid_argument if name is unknown. Used by all
// distribution test/glue/binding layers to avoid triplication of the same string-matching logic.
inline DistributionType distribution_type_from_name(const std::string& name) {
    if (name == "Normal") return DistributionType::Normal;
    if (name == "Uniform") return DistributionType::Uniform;
    if (name == "Triangular") return DistributionType::Triangular;
    if (name == "Deterministic") return DistributionType::Deterministic;
    if (name == "LogNormal") return DistributionType::LogNormal;
    if (name == "TruncatedNormal") return DistributionType::TruncatedNormal;
    if (name == "TruncatedLogNormal") return DistributionType::TruncatedLogNormal;
    if (name == "LogPearsonIII") return DistributionType::LogPearsonIII;
    throw std::invalid_argument("distribution_type_from_name: unknown distribution type: " + name);
}

}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_ENUM_HPP
