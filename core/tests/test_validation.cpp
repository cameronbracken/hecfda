#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "hecfda/statistics/validation.hpp"

namespace {

// A minimal Validation subclass with two rules on the same captured value,
// both registered under the SAME property "X": one Fatal (x > 0) and one
// Minor (x > 1). Mirrors how Uniform's addRules wires up two rules on
// property "Min" (Min<=Max is Fatal, Min<Max is Minor) with predicates that
// return true when the property is valid.
class TwoRuleValidation : public hecfda::statistics::Validation {
   public:
    explicit TwoRuleValidation(double x) : x_(x) {
        add_single_property_rule(
            "X", [this]() { return x_ > 0; }, "X must be greater than 0.",
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "X", [this]() { return x_ > 1; }, "X must be greater than 1.",
            hecfda::statistics::ErrorLevel::Minor);
        validate();
    }

   private:
    double x_;
};

// A Validation subclass with one failing rule per property, on two DIFFERENT
// properties: "A" (Major, registered first) and "B" (Minor, registered
// second). Used to exercise Validation.Validate()'s cross-property
// aggregation, which is distinct from the intra-property OR above.
class TwoPropertyValidation : public hecfda::statistics::Validation {
   public:
    explicit TwoPropertyValidation(double a, double b) : a_(a), b_(b) {
        add_single_property_rule(
            "A", [this]() { return a_ > 0; }, "A must be greater than 0.",
            hecfda::statistics::ErrorLevel::Major);
        add_single_property_rule(
            "B", [this]() { return b_ > 0; }, "B must be greater than 0.",
            hecfda::statistics::ErrorLevel::Minor);
        validate();
    }

   private:
    double a_;
    double b_;
};

}  // namespace

TEST_CASE("Validation: value satisfying all rules has no errors") {
    TwoRuleValidation v(2.0);
    CHECK(!v.has_errors());
    CHECK(v.error_level() == hecfda::statistics::ErrorLevel::Unassigned);
    CHECK(v.errors().empty());
}

TEST_CASE("Validation: value failing only the Minor rule reports Minor") {
    TwoRuleValidation v(1.0);
    CHECK(v.has_errors());
    CHECK(v.error_level() == hecfda::statistics::ErrorLevel::Minor);
    CHECK(v.errors().size() == 1);
}

TEST_CASE(
    "Validation: value failing both rules on the same property reports the OR composite "
    "(Fatal|Minor), not just the higher level") {
    // Mirrors PropertyRule.Update()'s bitwise-OR of failing rules' ErrorLevels within a
    // property ("_errorLevel = _errorLevel | r.ErrorLevel"): both the Fatal (x>0) and Minor
    // (x>1) rules fail on property "X", so the composite is Fatal|Minor = 0x20|0x02 = 0x22,
    // which is NOT a named ErrorLevel value -- exactly as real C# produces for Uniform's
    // Min<=Max (Fatal) and Min<Max (Minor) rules both failing on property "Min".
    TwoRuleValidation v(0.0);
    const auto expected = static_cast<hecfda::statistics::ErrorLevel>(
        static_cast<unsigned char>(hecfda::statistics::ErrorLevel::Fatal) |
        static_cast<unsigned char>(hecfda::statistics::ErrorLevel::Minor));
    CHECK(v.has_errors());
    CHECK(v.error_level() == expected);
    CHECK(v.errors().size() == 2);
}

TEST_CASE(
    "Validation: failing rules on two DIFFERENT properties -- the LAST failing property (in "
    "registration order) overwrites the aggregate; it is not OR'd or maxed across properties") {
    // Mirrors Validation.Validate()'s cross-property aggregation: RuleMap is iterated in
    // property-insertion order and each failing property's composite ErrorLevel OVERWRITES
    // (not ORs) the object's aggregate. "A" (Major) is registered first and "B" (Minor)
    // second; both fail, so "B" -- the LAST failing property -- wins, even though Major is
    // numerically higher severity than Minor.
    TwoPropertyValidation v(0.0, 0.0);
    CHECK(v.has_errors());
    CHECK(v.error_level() == hecfda::statistics::ErrorLevel::Minor);
    CHECK(v.errors().size() == 2);
}
