// Unified example dispatcher:
//   ./example analyze <otbm_file>   -> benchmark + analyze an OTBM map
//   ./example generate [opts]       -> regenerate the test fixture
//       --embed-spawns / -s            embed a NODE_SPAWNS subtree (TFS-style)
//       --output <path> / -o <path>    write to <path> (default tests/fixture/world.otbm)
#include <iostream>
#include <string_view>

int analyze_main(int argc, char* argv[]);
int generate_main(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage:\n"
                  << "  " << argv[0] << " analyze <otbm_file>                       Analyze/benchmark an OTBM map\n"
                  << "  " << argv[0] << " generate [--embed-spawns] [--output <path>]  Regenerate the test fixture\n";
        return 1;
    }

    std::string_view cmd = argv[1];
    if (cmd == "generate") return generate_main(argc, argv);
    if (cmd == "analyze")  return analyze_main(argc - 1, argv + 1);

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
