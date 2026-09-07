#pragma once
#include "otbm_parser.hpp"
#include <fstream>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

inline void test_otbm_integration() {
    std::cout << "  [RUN] OTBM Integration (fixture)...\n";
    
    std::filesystem::path fixture_path = "tests/fixture/world.otbm";

    // Load the whole file via mmap (same robust approach as analyze-otbm.cpp).
    // std::ifstream + tellg/seekg can leave the stream in a bad state and yield
    // a zeroed buffer, which the parser then rejects with InvalidNode.
    int fd = open(fixture_path.string().c_str(), O_RDONLY);
    if (fd == -1) {
        std::cout << "  [SKIP] Fixture not found: " << fixture_path << "\n";
        return;
    }
    struct stat sb;
    if (fstat(fd, &sb) == -1) { close(fd); std::cout << "  [SKIP] Cannot stat fixture\n"; return; }
    const size_t size = static_cast<size_t>(sb.st_size);
    const uint8_t* mapped = static_cast<const uint8_t*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (mapped == MAP_FAILED) { close(fd); std::cout << "  [SKIP] mmap failed\n"; return; }
    std::span<const uint8_t> buffer(mapped, size);

    otbm::Parser parser;
    uint32_t tile_count = 0, item_count = 0, town_count = 0, waypoint_count = 0;
    bool description_found = false;
    uint16_t captured_width = 0, captured_height = 0;
    
    parser.on_header = [&](const otbm::MapHeader& h) { captured_width = h.width; captured_height = h.height; };
    parser.on_map_description = [&](std::string_view desc) { description_found = !desc.empty(); };
    parser.on_town = [&](const otbm::Town&) { town_count++; };
    parser.on_waypoint = [&](const otbm::Waypoint&) { waypoint_count++; };
    parser.on_tile = [&](const otbm::Tile&) { tile_count++; };
    parser.on_item = [&](const otbm::Tile&, const otbm::Item&, const otbm::Item*) { item_count++; };
    
    auto result = parser.parse(buffer);
    
    if (!result) {
        std::cerr << "  [FAIL] Parser returned error code: " << static_cast<int>(result.error()) << "\n";
    }
    
    // NOTE: assert() is compiled out under NDEBUG (Release), so we fail loudly
    // here with std::abort() regardless of build config.
    if (!result.has_value()) { std::cerr << "  [FAIL] Parser should complete without error\n"; std::abort(); }
    if (!(captured_width > 0 && captured_height > 0)) { std::cerr << "  [FAIL] Map dimensions should be valid\n"; std::abort(); }
    if (!description_found) { std::cerr << "  [FAIL] Map should have a description\n"; std::abort(); }
    if (!(town_count > 0)) { std::cerr << "  [FAIL] Map should have at least 1 town\n"; std::abort(); }
    if (!(waypoint_count > 0)) { std::cerr << "  [FAIL] Map should have at least 1 waypoint\n"; std::abort(); }
    if (!(tile_count > 0)) { std::cerr << "  [FAIL] Map should have tiles\n"; std::abort(); }
    if (!(item_count > 0)) { std::cerr << "  [FAIL] Map should have items\n"; std::abort(); }
    
    std::cout << "  [OK]  Integration passed: " 
              << captured_width << "x" << captured_height 
              << ", " << tile_count << " tiles, " 
              << item_count << " items, "
              << town_count << " towns, "
              << waypoint_count << " waypoints\n";

    munmap(const_cast<uint8_t*>(mapped), size);
    close(fd);
}
