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
