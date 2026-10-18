#pragma once
#include "otbm_parser.hpp"
#include <cassert>
#include <vector>
#include <iostream>

inline void test_otbm_parser() {
    std::cout << "  [RUN] OTBM Parser...\n";
    std::vector<uint8_t> mock = {
        0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x01, 0x00, 0x00, 0x00, 0x64, 0x00, 0x64, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x02, 0xFF, 0xFF
    };
    otbm::Parser p; bool called = false;
    p.on_header = [&](const otbm::MapHeader& h) { called = true; assert(h.width == 100); };
    auto res = p.parse(mock);
    assert(res.has_value() && called);
    std::cout << "  [OK]  OTBM Parser passed.\n";
}

inline void test_otbm_parser_robustness() {
    std::cout << "  [RUN] OTBM Parser robustness (findings #A2, #A3, #A5)...\n";

    // Build a minimal valid map: [version u32][root 0xFE 0x00]...[MapData 0xFE 0x02][body][MapData end][root end].
    auto make_map = [](const std::vector<uint8_t>& body) {
        std::vector<uint8_t> m = {
            0x01,0x00,0x00,0x00,   // version u32 (not the "OTBM" magic)
            0xFE,0x00,             // root start + type
            0x01,0x00,0x00,0x00,   // version
            0x64,0x00,             // width
            0x64,0x00,             // height
            0x03,0x00,0x00,0x00,   // items major
            0x00,0x00,0x00,0x00,   // items minor
            0xFE,0x02              // MapData start + type
        };
        m.insert(m.end(), body.begin(), body.end());
        m.push_back(0xFF);         // MapData end
        m.push_back(0xFF);         // root end
        return m;
    };
    auto tile_area = [](const std::vector<uint8_t>& tile) {
        std::vector<uint8_t> a = { 0xFE,0x04, 0x00,0x00, 0x00,0x00, 0x00 }; // TileArea + base pos
        a.insert(a.end(), tile.begin(), tile.end());
        a.push_back(0xFF); // TileArea end
        return a;
    };

    // #A5: on_item fires exactly once, with the complete record (not the flat-ground partial).
    {
        std::vector<uint8_t> tile = {
            0xFE,0x05, 0x00,0x00,        // Tile + offsets
            0x09, 0x01,0x00,             // flat Item attr (ground), id 1
            0xFE,0x06, 0x02,0x00, 0xFF,  // child Item node, id 2
            0xFF                          // tile end
        };
        auto blob = make_map(tile_area(tile));
        otbm::Parser p;
        int item_calls = 0; uint16_t last_type = 0;
        p.on_item = [&](const otbm::Tile&, const otbm::Item& i, const otbm::Item*) { item_calls++; last_type = i.type; };
        auto r = p.parse(blob);
        assert(r.has_value());
        assert(item_calls == 1);   // not 2 (vestigial early call removed)
        assert(last_type == 2);    // the complete record, not the flat ground
    }

    // #A3: an unknown ITEM attribute must fail loudly (not desync silently).
    {
        std::vector<uint8_t> tile = {
            0xFE,0x05, 0x00,0x00,
            0xFE,0x06, 0x01,0x00, 0x30, 0xFF,  // item id 1, unknown attr 0x30, end
            0xFF
        };
        auto blob = make_map(tile_area(tile));
        otbm::Parser p;
        auto r = p.parse(blob);
        assert(!r.has_value() && r.error() == otbm::ParseError::InvalidFormat);
    }

    // #A2: an unknown MAP-LEVEL attribute must fail loudly.
    {
        std::vector<uint8_t> body = { 0x04, 0x00,0x00 }; // ActionId at map level (unknown there)
        auto blob = make_map(body);
        otbm::Parser p;
        auto r = p.parse(blob);
        assert(!r.has_value() && r.error() == otbm::ParseError::InvalidFormat);
    }

    std::cout << "  [OK]  OTBM Parser robustness passed.\n";
}
