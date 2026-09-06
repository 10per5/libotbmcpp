#pragma once
#include "otbm_parser.hpp"
#include <fstream>
#include <vector>
#include <cassert>
#include <iostream>
#include <filesystem>

inline void test_otbm_integration() {
    std::cout << "  [RUN] OTBM Integration (fixture)...\n";
    
    std::filesystem::path fixture_path = "tests/fixture/world.otbm";
    std::ifstream file(fixture_path, std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        std::cout << "  [SKIP] Fixture not found: " << fixture_path << "\n";
        return;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    assert(file.read(reinterpret_cast<char*>(buffer.data()), size));
    
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
    
    assert(result.has_value() && "Parser should complete without error");
    assert(captured_width > 0 && captured_height > 0 && "Map dimensions should be valid");
    assert(description_found && "Map should have a description");
    assert(town_count > 0 && "Map should have at least 1 town");
    assert(waypoint_count > 0 && "Map should have at least 1 waypoint");
    assert(tile_count > 0 && "Map should have tiles");
    assert(item_count > 0 && "Map should have items");
    
    std::cout << "  [OK]  Integration passed: " 
              << captured_width << "x" << captured_height 
              << ", " << tile_count << " tiles, " 
              << item_count << " items, "
              << town_count << " towns, "
              << waypoint_count << " waypoints\n";
}
