// ported from: HEC.MVVMFramework.Base/Enumerations/ErrorLevel.cs,
// HEC.MVVMFramework.Base/Implementations/Validation.cs,
// HEC.MVVMFramework.Base/Implementations/PropertyRule.cs,
// HEC.MVVMFramework.Base/Implementations/Rule.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_VALIDATION_HPP
#define HECFDA_STATISTICS_VALIDATION_HPP
#include <cstdint>
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
// comparing levels, so it is preserved verbatim. The bitwise-combination
// (flags) behavior IS used by this port -- see the aggregation note below --
// so `error_level()` can hold composite, non-named values (e.g. 0x22).
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
// ErrorLevel aggregation is ported to match C# EXACTLY via two distinct
// mechanisms, transcribed from upstream rather than approximated:
//
//  1. Intra-property (PropertyRule.Update()): within a single property, the
//     ErrorLevels of the FAILING rules are bitwise-OR'd together:
//         if (_errorLevel > ErrorLevel.Unassigned)
//             _errorLevel = _errorLevel | r.ErrorLevel;
//         else
//             _errorLevel = r.ErrorLevel;   // first failing rule
//     Since ErrorLevel is `[Flags] : byte`, two failing rules on the same
//     property (e.g. Fatal=0x20 and Minor=0x02) yield the COMPOSITE 0x22,
//     which is not a named enum value. This is order-independent (OR).
//
//  2. Cross-property (Validation.Validate()): RuleMap is iterated in
//     property-insertion order, and each failing property's composite
//     ErrorLevel OVERWRITES the object's aggregate:
//         if (_RuleMap[s].ErrorLevel > ErrorLevel.Unassigned) {
//             if (_errorLevel > ErrorLevel.Unassigned)
//                 _errorLevel = _RuleMap[s].ErrorLevel;        // overwrite
//             else
//                 _errorLevel = _errorLevel | _RuleMap[s].ErrorLevel; // == assign, since lhs==0
//             _Errors.AddRange(_RuleMap[s].Errors);
//         }
//     Both branches reduce to "assign the current failing property's level"
//     (OR-with-Unassigned(0) is an identity), so the net effect is: the
//     object's aggregate ErrorLevel is simply the composite ErrorLevel of
//     the LAST property (in insertion order) that has a failing rule -- NOT
//     an OR or a max across properties. `validate()` below reproduces this
//     precisely.
//
// `error_level()` still returns `ErrorLevel`; the enum's underlying type is
// `std::uint8_t`, so it can represent composite (non-named) values like
// 0x22 just as the C# `[Flags] byte` enum does.
//
// DONE_WITH_CONCERNS (scoped out, not ported):
//  - Validation.AddMultiPropertyRule and the incremental single-property
//    Validate(string) overload: these exist mainly for WPF's
//    INotifyDataErrorInfo (GetErrors(propertyName), ErrorsChanged). Nothing
//    in the ported core needs per-property lookups or change notification;
//    `property` is still recorded per rule (and used for the grouping
//    described above) even though nothing queries it directly.
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

    // ported from: PropertyRule.Update() + Validation.Validate(). See the
    // class comment above for the exact C# transcription this reproduces.
    void validate() {
        errors_.clear();
        error_level_ = ErrorLevel::Unassigned;

        // Per-property aggregation state, in property-registration (i.e.
        // first-seen) order -- ported from Validation.RuleMap, a
        // Dictionary<string, IPropertyRule> whose iteration order is (in
        // practice) insertion order.
        struct PropertyState {
            std::string property;
            std::uint8_t error_level = 0;  // OR of failing rules' levels for this property
            std::vector<std::string> messages;
        };
        std::vector<PropertyState> properties;
        auto state_for = [&properties](const std::string& property) -> PropertyState& {
            for (auto& state : properties) {
                if (state.property == property) {
                    return state;
                }
            }
            properties.push_back(PropertyState{property, 0, {}});
            return properties.back();
        };

        // Pass 1: ported from PropertyRule.Update() -- within a property, OR
        // together the ErrorLevels of the failing rules.
        for (const auto& rule : rules_) {
            PropertyState& state = state_for(rule.property);
            if (!rule.is_valid()) {
                state.error_level = static_cast<std::uint8_t>(state.error_level |
                                                                static_cast<std::uint8_t>(rule.level));
                state.messages.push_back(rule.message);
            }
        }

        // Pass 2: ported from Validation.Validate() -- across properties, in
        // insertion order, each failing property's composite ErrorLevel
        // OVERWRITES the object's aggregate (last failing property wins).
        for (const auto& state : properties) {
            if (state.error_level != 0) {
                error_level_ = static_cast<ErrorLevel>(state.error_level);
                errors_.insert(errors_.end(), state.messages.begin(), state.messages.end());
            }
        }
    }

    // ported from: Validation.HasErrors (_errorLevel > ErrorLevel.Unassigned).
    bool has_errors() const { return error_level_ > ErrorLevel::Unassigned; }

    // ported from: Validation.ErrorLevel getter.
    ErrorLevel error_level() const { return error_level_; }

    // messages of the currently-failing rules, grouped by property
    // (insertion order), then by rule order within each property -- ported
    // from Validation._Errors, which is built via
    // `_Errors.AddRange(_RuleMap[s].Errors)` per failing property.
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
