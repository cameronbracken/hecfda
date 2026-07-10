// ported from: HEC.MVVMFramework.Base/Enumerations/ErrorLevel.cs,
// HEC.MVVMFramework.Base/Implementations/Validation.cs,
// HEC.MVVMFramework.Base/Implementations/PropertyRule.cs,
// HEC.MVVMFramework.Base/Implementations/Rule.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_VALIDATION_HPP
#define HECFDA_STATISTICS_VALIDATION_HPP
#include <functional>
#include <string>
#include <vector>
namespace hecfda {
namespace statistics {

// ported from: ErrorLevel.cs. The C# enum is declared [Flags] : byte so the
// values below are mirrored exactly, including the gap between Major (0x04)
// and Fatal (0x20): Unassigned=0x00, Info=0x01, Minor=0x02, Major=0x04,
// Fatal=0x20, Severe=0x40. The numeric ordering (Unassigned < Info < Minor <
// Major < Fatal < Severe) is what upstream relies on via operator> when
// comparing levels, so it is preserved verbatim; the bitwise-combination
// (flags) behavior itself is not used by this port -- see note below.
enum class ErrorLevel : unsigned char {
    Unassigned = 0x00,
    Info = 0x01,
    Minor = 0x02,
    Major = 0x04,
    Fatal = 0x20,
    Severe = 0x40,
};

// ported from: Rule.cs / PropertyRule.cs / Validation.cs, scoped to the
// minimal surface the ported distributions need: AddSinglePropertyRule,
// Validate, HasErrors, ErrorLevel, and the failing-rule messages.
//
// DONE_WITH_CONCERNS (scoped out, not ported):
//  - Validation.AddMultiPropertyRule, the RuleMap-by-property grouping, and
//    the incremental single-property Validate(string) overload: upstream
//    keys rules by property name mainly for WPF's INotifyDataErrorInfo
//    (GetErrors(propertyName), ErrorsChanged). Nothing in the ported core
//    needs per-property lookups or change notification, so rules are kept
//    in one flat list in declaration order; `property` is still recorded
//    per rule for parity/debugging even though nothing queries it yet.
//  - The upstream ErrorLevel aggregation in Validation.Validate() is a
//    [Flags] bitwise-OR combine at the PropertyRule level, but at the
//    Validation level it actually just *overwrites* _errorLevel with the
//    last-iterated failing property's level rather than combining/maxing
//    across properties (see Validation.cs lines ~150-167) -- iteration
//    order of a Dictionary in .NET is insertion order in practice but is
//    not a documented guarantee, so which level "wins" when two different
//    properties fail at different levels is effectively unspecified
//    upstream. This port instead computes the true maximum ErrorLevel
//    across all failing rules, matching the documented intent ("ErrorLevel
//    is the highest level among failing rules") and the ported
//    Uniform/distribution tests, which only ever exercise a single failing
//    rule at a time.
//  - INotifyDataErrorInfo plumbing (ErrorsChanged event, GetErrors,
//    GetErrorMessages, ErrorsAction/INamedAction) is WPF/UI-binding
//    machinery with no equivalent need in the C++ core.
class Validation {
   public:
    virtual ~Validation() = default;

    // ported from: Validation.AddSinglePropertyRule(string, IRule). A rule's
    // predicate (is_valid) must return true when the property is VALID; the
    // rule contributes an error (message + level) when it returns false.
    void add_single_property_rule(std::string property, std::function<bool()> is_valid,
                                   std::string message, ErrorLevel level) {
        rules_.push_back(
            Rule{std::move(property), std::move(is_valid), std::move(message), level});
    }

    // ported from: Validation.Validate(). Evaluates every rule's predicate,
    // records the messages of the failing ones, and sets the aggregate
    // error level to the maximum level among the failing rules (Unassigned
    // if none failed).
    void validate() {
        errors_.clear();
        error_level_ = ErrorLevel::Unassigned;
        for (const auto& rule : rules_) {
            if (!rule.is_valid()) {
                errors_.push_back(rule.message);
                if (rule.level > error_level_) {
                    error_level_ = rule.level;
                }
            }
        }
    }

    // ported from: Validation.HasErrors (_errorLevel > ErrorLevel.Unassigned).
    bool has_errors() const { return error_level_ > ErrorLevel::Unassigned; }

    // ported from: Validation.ErrorLevel getter.
    ErrorLevel error_level() const { return error_level_; }

    // messages of the currently-failing rules, in rule declaration order.
    std::vector<std::string> errors() const { return errors_; }

   private:
    struct Rule {
        std::string property;
        std::function<bool()> is_valid;
        std::string message;
        ErrorLevel level;
    };

    std::vector<Rule> rules_;
    std::vector<std::string> errors_;
    ErrorLevel error_level_ = ErrorLevel::Unassigned;
};

}  // namespace statistics
}  // namespace hecfda
#endif
