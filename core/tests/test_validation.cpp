#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "hecfda/statistics/validation.hpp"

namespace {

// A minimal Validation subclass with two rules on the same captured value:
// one Fatal (x > 0) and one Minor (x > 1). Mirrors how Uniform's addRules
// wires up predicates that return true when the property is valid.
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

TEST_CASE("Validation: value failing both rules reports the higher Fatal level") {
    TwoRuleValidation v(0.0);
    CHECK(v.has_errors());
    CHECK(v.error_level() == hecfda::statistics::ErrorLevel::Fatal);
    CHECK(v.errors().size() == 2);
}
