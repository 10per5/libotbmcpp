#include "ut/otbm_parser.test.hpp"
#include "ut/otb_parser.test.hpp"
#include "it/otbm_it.test.hpp"

int main() {
    std::cout << "=== Running OTB/OTBM Tests ===\n";
    
    // Unit tests
    test_otbm_parser();
    test_otb_parser();
    test_otbm_parser_robustness();
    test_otbm_parser_spawns();
    test_otb_parser_features();
    
    // Integration tests
    test_otbm_integration();
    test_otbm_integration_with_spawns();
    
    std::cout << "=== All tests passed ===\n";
    return 0;
}
