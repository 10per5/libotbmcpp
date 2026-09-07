// Unified example dispatcher:
//   ./example analyze <otbm_file>   -> benchmark + analyze an OTBM map
//   ./example generate              -> regenerate the test fixture
#include <iostream>
#include <string_view>

int analyze_main(int argc, char* argv[]);
int generate_main();

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage:\n"
                  << "  " << argv[0] << " analyze <otbm_file>   Analyze/benchmark an OTBM map\n"
                  << "  " << argv[0] << " generate               Regenerate the test fixture\n";
        return 1;
    }

    std::string_view cmd = argv[1];
    if (cmd == "generate") return generate_main();
    if (cmd == "analyze")  return analyze_main(argc - 1, argv + 1);

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
