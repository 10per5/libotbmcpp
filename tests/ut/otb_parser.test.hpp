#pragma once
#include "otb_parser.hpp"
#include <cassert>
#include <vector>
#include <iostream>

inline void test_otb_parser() {
    std::cout << "  [RUN] OTB Parser...\n";
    std::vector<uint8_t> mock = {
        // 16-byte header
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        // Root Node
        0xFE, 0x00,
        // ItemDef Node
        0xFE, 0x00,
        // ServerId (0x10) = 1
        0x10, 0x01, 0x00,
        // Name (0x12) = "ABCD" (length 4)
        0x12, 0x04, 0x00, 0x41, 0x42, 0x43, 0x44,
        // End ItemDef & Root
        0xFF, 0xFF
    };
    otb::Parser p;
    auto res = p.parse(mock);
    assert(res.has_value());
    assert(p.items.size() == 1);
    assert(p.items[1].name == "ABCD");
    std::cout << "  [OK]  OTB Parser passed.\n";
}
