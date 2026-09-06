#include "otbm_parser.test.hpp"
#include "otb_parser.test.hpp"

int main() {
    std::cout << "=== Running OTB/OTBM Surface Tests ===\n";
    test_otbm_parser();
    test_otb_parser();
    std::cout << "=== All tests passed successfully! ===\n";
    return 0;
}
