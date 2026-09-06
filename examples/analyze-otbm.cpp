#include "otbm_parser.hpp"
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <chrono>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

class OTBMAnalyzer {
public:
    struct BenchmarkResult {
        double mmap_time_ms = 0.0;
        double parse_time_ms = 0.0;
        double total_time_ms = 0.0;
        double throughput_mb_s = 0.0;
    };

    struct Stats {
        otbm::MapHeader header;
        std::string description, spawn_file, house_file;
        uint32_t tile_count = 0, item_count = 0, town_count = 0, waypoint_count = 0;
        std::vector<otbm::Town> towns;
        std::unordered_map<uint16_t, uint32_t> item_type_counts;
        uint32_t portal_count = 0, depot_count = 0, container_count = 0;
        uint32_t pz_tiles = 0, no_pvp_tiles = 0, pvp_tiles = 0, no_logout_tiles = 0, refresh_tiles = 0;
    };

    Stats stats;
    BenchmarkResult bench;

    void analyze(const std::string& filepath) {
        auto total_start = std::chrono::high_resolution_clock::now();
        auto mmap_start = std::chrono::high_resolution_clock::now();

        uint8_t* data = nullptr;
        size_t size = 0;

#if defined(_WIN32)
        HANDLE file = CreateFileA(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            std::cerr << "Error: Cannot open file\n";
            return;
        }
        HANDLE mapping = CreateFileMapping(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) { CloseHandle(file); std::cerr << "Error: Cannot map file\n"; return; }
        
        data = static_cast<uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        size = GetFileSize(file, nullptr);
#else
        int fd = open(filepath.c_str(), O_RDONLY);
        if (fd == -1) { std::cerr << "Error: Cannot open file\n"; return; }
        
        struct stat sb;
        if (fstat(fd, &sb) == -1) { close(fd); std::cerr << "Error: Cannot stat file\n"; return; }
        size = sb.st_size;
        
        data = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        if (data == MAP_FAILED) { close(fd); std::cerr << "Error: mmap failed\n"; return; }
        madvise(data, size, MADV_SEQUENTIAL);
#endif

        auto mmap_end = std::chrono::high_resolution_clock::now();
        bench.mmap_time_ms = std::chrono::duration<double, std::milli>(mmap_end - mmap_start).count();

        // PARSE
        std::span<const uint8_t> buffer(data, size);
        
        auto parse_start = std::chrono::high_resolution_clock::now();
        parse_data(buffer);
        auto parse_end = std::chrono::high_resolution_clock::now();

        bench.parse_time_ms = std::chrono::duration<double, std::milli>(parse_end - parse_start).count();
        
        auto total_end = std::chrono::high_resolution_clock::now();
        bench.total_time_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

        if (bench.parse_time_ms > 0) {
            double size_mb = static_cast<double>(size) / (1024.0 * 1024.0);
            bench.throughput_mb_s = size_mb / (bench.parse_time_ms / 1000.0);
        }

        // Cleanup
#if defined(_WIN32)
        UnmapViewOfFile(data);
        CloseHandle(mapping);
        CloseHandle(file);
#else
        munmap(data, size);
        close(fd);
#endif

        print_report(buffer.size());
    }

private:
    void parse_data(std::span<const uint8_t> buffer) {
        otbm::Parser parser;
        parser.on_header = [this](const otbm::MapHeader& h) { stats.header = h; };
        parser.on_map_description = [this](std::string_view desc) { stats.description = desc; };
        parser.on_map_spawn_file = [this](std::string_view spawn) { stats.spawn_file = spawn; };
        parser.on_map_houses_file = [this](std::string_view houses) { stats.house_file = houses; };
        parser.on_town = [this](const otbm::Town& t) {
            stats.town_count++;
            stats.towns.push_back(t);
        };
        parser.on_waypoint = [this](const otbm::Waypoint&) { stats.waypoint_count++; };
        parser.on_tile = [this](const otbm::Tile& t) {
            stats.tile_count++;
            if (t.flags & static_cast<uint32_t>(otbm::TileState::ProtectionZone)) stats.pz_tiles++;
            if (t.flags & static_cast<uint32_t>(otbm::TileState::NoPvpZone)) stats.no_pvp_tiles++;
            if (t.flags & static_cast<uint32_t>(otbm::TileState::PvpZone)) stats.pvp_tiles++;
            if (t.flags & static_cast<uint32_t>(otbm::TileState::NoLogout)) stats.no_logout_tiles++;
            if (t.flags & static_cast<uint32_t>(otbm::TileState::Refresh)) stats.refresh_tiles++;
        };
        parser.on_item = [this](const otbm::Tile&, const otbm::Item& item, const otbm::Item* parent) {
            stats.item_count++;
            stats.item_type_counts[item.type]++;
            if (item.is_portal) stats.portal_count++;
            if (item.is_depot) stats.depot_count++;
            if (parent != nullptr) stats.container_count++;
        };
        
        auto result = parser.parse(buffer);
        if (!result) {
            std::cerr << "\n❌ FAILED TO PARSE!\n";
            std::cerr << "Error Code: " << static_cast<int>(result.error()) << "\n";
            if (result.error() == otbm::ParseError::InvalidFormat) std::cerr << "Meaning: InvalidFormat (Unexpected byte boundary or malformed node)\n";
            else if (result.error() == otbm::ParseError::UnexpectedEof) std::cerr << "Meaning: UnexpectedEof (File ended prematurely)\n";
            else if (result.error() == otbm::ParseError::InvalidNode) std::cerr << "Meaning: InvalidNode (Expected 0xFE but found something else)\n";
            else if (result.error() == otbm::ParseError::UnsupportedVersion) std::cerr << "Meaning: UnsupportedVersion (OTBM version is not 0)\n";
        }
    }

    void print_report(size_t file_size) {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║               OTBM MAP ANALYZER & BENCHMARK                  ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
        std::cout << "📁 File Size: " << format_bytes(file_size) << "\n";
        std::cout << "🗺️  Dimensions: " << stats.header.width << " x " << stats.header.height 
                  << " | Map v" << stats.header.map_version.major 
                  << " | Items v" << stats.header.items_version.major << "." << stats.header.items_version.minor << "\n\n";
        
        if (!stats.description.empty()) std::cout << "📝 " << stats.description << "\n\n";
        
        std::cout << "🔗 Spawn: " << (stats.spawn_file.empty() ? "none" : stats.spawn_file) 
                  << " | Houses: " << (stats.house_file.empty() ? "none" : stats.house_file) << "\n\n";
        
        std::cout << "📊 Towns: " << stats.town_count << " | Waypoints: " << stats.waypoint_count 
                  << " | Tiles: " << stats.tile_count << " | Items: " << stats.item_count << "\n";
        
        if (!stats.towns.empty()) {
            std::cout << "\n🏙️  REGISTERED TOWNS (" << stats.towns.size() << "):\n";
            std::cout << "──────────────────────────────────────────────────────────────\n";
            for (const auto& t : stats.towns) {
                std::cout << "  • ID " << std::setw(3) << std::left << t.id << " | "
                          << std::setw(25) << std::left << (t.name.empty() ? "<Unnamed Town>" : t.name)
                          << " | Position: (" << t.pos.x << ", " << t.pos.y << ", " << static_cast<int>(t.pos.z) << ")\n";
            }
            std::cout << "──────────────────────────────────────────────────────────────\n";
        }

        if (stats.item_count > 0) {
            std::vector<std::pair<uint16_t, uint32_t>> sorted(stats.item_type_counts.begin(), stats.item_type_counts.end());
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            std::cout << "\n🎒 Top Items: ";
            for (size_t i = 0; i < std::min<size_t>(5, sorted.size()); ++i) {
                std::cout << (i ? ", " : "") << "ID " << sorted[i].first << " (" << sorted[i].second << "x)";
            }
            std::cout << "\n";
        }

        std::cout << "\n⏱️  BENCHMARK RESULTS\n";
        std::cout << "──────────────────────────────────────────────────────────────\n";
        std::cout << "  • Memory Mapping Overhead : " << std::fixed << std::setprecision(3) << bench.mmap_time_ms << " ms\n";
        std::cout << "  • Parsing Duration        : " << std::fixed << std::setprecision(3) << bench.parse_time_ms << " ms\n";
        std::cout << "  • Total Execution Time    : " << std::fixed << std::setprecision(3) << bench.total_time_ms << " ms\n";
        std::cout << "  • Parsing Throughput      : " << std::fixed << std::setprecision(2) << bench.throughput_mb_s << " MB/s\n";
        
        if (bench.parse_time_ms > 0) {
            double tiles_per_sec = (stats.tile_count / (bench.parse_time_ms / 1000.0));
            double items_per_sec = (stats.item_count / (bench.parse_time_ms / 1000.0));
            std::cout << "  • Processing Speed        : " << static_cast<uint64_t>(tiles_per_sec) << " tiles/sec | " 
                      << static_cast<uint64_t>(items_per_sec) << " items/sec\n";
        }
        
        std::cout << "══════════════════════════════════════════════════════════════\n✅ Analysis complete!\n\n";
    }

    std::string format_bytes(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit = 0; double size = static_cast<double>(bytes);
        while (size >= 1024 && unit < 3) { size /= 1024; unit++; }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
        return oss.str();
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <otbm_file>\n";
        return 1;
    }
    if (!std::filesystem::exists(argv[1])) {
        std::cerr << "Error: File does not exist\n";
        return 1;
    }
    OTBMAnalyzer analyzer;
    analyzer.analyze(argv[1]);
    return 0;
}
