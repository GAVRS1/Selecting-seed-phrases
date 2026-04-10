#include "engine/matcher.hpp"

#include <cassert>
#include <iostream>
#include <unordered_set>

int main() {
    const std::string ton_user_friendly = "EQCD39VS5pSmpmVkUCGowOvtWbx1SByHpsAoAaxdBApYpSRG";
    const std::string ton_raw = "0:83dfd552e694a6a665645021a8c0ebed59bc75481c87a6c02801ac5d040a58a5";

    engine::Matcher matcher_from_user_friendly(std::unordered_set<std::string>{ton_user_friendly});
    assert(matcher_from_user_friendly.contains_address(ton_user_friendly));
    assert(matcher_from_user_friendly.contains_address(ton_raw));

    engine::Matcher matcher_from_raw(std::unordered_set<std::string>{ton_raw});
    assert(matcher_from_raw.contains_address(ton_raw));
    assert(matcher_from_raw.contains_address(ton_user_friendly));

    std::cout << "test_matcher_ton passed\n";
    return 0;
}
