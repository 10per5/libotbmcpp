#include "ut/otbm_parser.test.hpp"
#include "ut/otb_parser.test.hpp"
#include "it/otbm_it.test.hpp"

int main() {
    std::cout << "=== Running OTB/OTBM Tests ===\n";
    
    // Unit tests
    test_otbm_parser();
    test_otb_parser();
    
    // Integration test
    test_otbm_integration();
    
    std::cout << "=== All tests passed ===\n";
    return 0;
}
