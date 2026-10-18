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

inline void test_otb_parser_features() {
    std::cout << "  [RUN] OTB Parser features (findings #1-#8, #12)...\n";

    // #1: empty / truncated input must error, never report success.
    {
        otb::Parser p;
        std::vector<uint8_t> empty;
        auto r = p.parse(empty);
        assert(!r.has_value() && r.error() == otb::ParseError::UnexpectedEof);
    }
    {
        otb::Parser p;
        std::vector<uint8_t> hdr(16, 0x00); // header only, no root node
        auto r = p.parse(hdr);
        assert(!r.has_value() && r.error() == otb::ParseError::UnexpectedEof);
    }
    {
        otb::Parser p;
        std::vector<uint8_t> blob = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFE,0x00, 0xFE,0x00, 0x10,0x01 /* only 1 of 2 bytes of ServerId */
        };
        auto r = p.parse(blob);
        assert(!r.has_value() && r.error() == otb::ParseError::UnexpectedEof);
    }

    // #3: non-OTB input must be InvalidFormat, not success.
    {
        otb::Parser p;
        std::vector<uint8_t> not_otb = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00 /* no 0xFE root start */
        };
        auto r = p.parse(not_otb);
        assert(!r.has_value() && r.error() == otb::ParseError::InvalidFormat);
    }

    // #2: unknown attribute fails loudly by default; SkipAsU16 restores leniency.
    {
        std::vector<uint8_t> blob = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFE,0x00,
            0xFE,0x00,
            0x10,0x01,0x00,                 // ServerId = 1
            0x20,0x00,0x00,                 // unknown attr 0x20, u16 value 0
            0xFF,                           // end itemdef
            0xFF                            // end root
        };
        otb::Parser p;
        auto r = p.parse(blob);
        assert(!r.has_value() && r.error() == otb::ParseError::InvalidFormat);

        otb::Parser lenient;
        lenient.unknown_attribute_policy = otb::UnknownAttributePolicy::SkipAsU16;
        auto r2 = lenient.parse(blob);
        assert(r2.has_value() && lenient.items.count(1) == 1);
    }

    // #4: MaxItems is now populated (was silently dropped).
    {
        std::vector<uint8_t> blob = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFE,0x00,
            0xFE,0x00,
            0x10,0x2A,0x00,                 // ServerId = 42
            0x2B,0x05,0x00,                 // MaxItems = 5
            0xFF,
            0xFF
        };
        otb::Parser p;
        auto r = p.parse(blob);
        assert(r.has_value() && p.items[42].max_items == 5);
    }

    // #5/#6/#12: client-id index + first-wins duplicates.
    {
        std::vector<uint8_t> blob = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFE,0x00,
            // entry A: both ids
            0xFE,0x00, 0x10,0x01,0x00, 0x11,0x64,0x00, 0xFF,
            // entry B: client id only (no server id)
            0xFE,0x00, 0x11,0xC8,0x00, 0xFF,
            0xFF
        };
        otb::Parser p; // default: by_server_id only
        auto r = p.parse(blob);
        assert(r.has_value());
        assert(p.items.count(1) == 1);
        assert(p.client_items.empty()); // opt-in, default off

        otb::Parser q;
        q.index.by_client_id = true;
        size_t client_fires = 0;
        q.on_client_item = [&](uint16_t, const otb::ItemDefinition&) { client_fires++; };
        auto r2 = q.parse(blob);
        assert(r2.has_value());
        assert(q.items.count(1) == 1);
        assert(q.client_items.count(100) == 1);
        assert(q.client_items.count(200) == 1); // client-only entry now reachable
        assert(client_fires == 2);
    }

    // #6: duplicate server_id is first-wins (emplace, not operator[]).
    {
        std::vector<uint8_t> blob = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFE,0x00,
            0xFE,0x00, 0x10,0x01,0x00, 0x12,0x03,0x00,'A','A','A', 0xFF,
            0xFE,0x00, 0x10,0x01,0x00, 0x12,0x03,0x00,'B','B','B', 0xFF,
            0xFF
        };
        otb::Parser p;
        auto r = p.parse(blob);
        assert(r.has_value() && p.items[1].name == "AAA");
    }

    // #8: parse() resets; a second call is not polluted by the first.
    {
        std::vector<uint8_t> a = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFE,0x00, 0xFE,0x00, 0x10,0x01,0x00, 0xFF, 0xFF
        };
        std::vector<uint8_t> b = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFE,0x00, 0xFE,0x00, 0x10,0x02,0x00, 0xFF, 0xFF
        };
        otb::Parser p;
        assert(p.parse(a).has_value());
        assert(p.items.size() == 1 && p.items.count(1) == 1);
        assert(p.parse(b).has_value());
        assert(p.items.size() == 1 && p.items.count(2) == 1 && p.items.count(1) == 0);
    }

    std::cout << "  [OK]  OTB Parser features passed.\n";
}
