#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <expected>
#include <functional>
#include <cstdio>

namespace otbm {
    enum class NodeType : uint8_t { 
        Root = 0x00, RootV1 = 0x01, MapData = 0x02, ItemDef = 0x03, 
        TileArea = 0x04, Tile = 0x05, Item = 0x06, TileSquare = 0x07, TileRef = 0x08, 
        Spawns = 0x09, SpawnArea = 0x0A, Monster = 0x0B, 
        Towns = 0x0C, Town = 0x0D, HouseTile = 0x0E, Waypoints = 0x0F, Waypoint = 0x10 
    };

    enum class DataType : uint8_t { 
        Description = 0x01, ExtFile = 0x02, TileFlags = 0x03, ActionId = 0x04,
        UniqueId = 0x05, Text = 0x06, Desc = 0x07, TeleDest = 0x08, Item = 0x09,
        DepotId = 0x0A, ExtSpawnFile = 0x0B, RuneCharges = 0x0C, ExtHouseFile = 0x0D,
        HouseDoorId = 0x0E, Count = 0x0F, Duration = 0x10, DecayingState = 0x11,
        WrittenDate = 0x12, WrittenBy = 0x13, SleeperGuid = 0x14, SleepStart = 0x15,
        Charges = 0x16
    };

    enum class TileState : uint32_t {
        None = 0, ProtectionZone = 1 << 0, DeprecatedHouse = 1 << 1,
        NoPvpZone = 1 << 2, NoLogout = 1 << 3, PvpZone = 1 << 4, Refresh = 1 << 5
    };
    
    struct Position { uint16_t x{0}, y{0}; uint8_t z{0}; };
    struct Tile { Position pos; uint32_t flags{0}; uint32_t house_id{0}; };
    struct Item { 
        uint16_t type{0}; uint8_t count{1}; uint16_t aid{0}, uid{0}, charges{0}; 
        std::string text; bool is_portal{false}; Position portal_exit; 
        bool is_depot{false}; uint16_t depot_id{0}; bool is_house_door{false}; uint8_t door_id{0}; 
    };
    struct Waypoint { std::string name; Position pos; };
    struct Town { uint32_t id{0}; std::string name; Position pos; };
    struct Version { uint32_t major{0}, minor{0}; };
    struct MapHeader { Version map_version; uint16_t width{0}, height{0}; Version items_version; };

    enum class ParseError { Success = 0, InvalidFormat, UnsupportedVersion, UnexpectedEof, InvalidNode };
    template<typename T> using Expected = std::expected<T, ParseError>;

    class Parser {
    public:
        std::function<void(const MapHeader&)> on_header;
        std::function<void(std::string_view)> on_map_description, on_map_spawn_file, on_map_houses_file;
        std::function<void(const Town&)> on_town;
        std::function<void(const Waypoint&)> on_waypoint;
        std::function<void(const Tile&)> on_tile;
        std::function<void(const Tile&, const Item&, const Item* parent)> on_item;

        [[nodiscard]] Expected<bool> parse(std::span<const uint8_t> data) {
            Stream stream(data);
            
            // Checking OTBM Header Magic (Optional 4 bytes if present, standard header otherwise)
            auto val = stream.read<uint32_t>();
            if (!val) return std::unexpected(ParseError::UnexpectedEof);

            // If file begins with "OTBM" (0x4D42544F), skip the 4 magic bytes
            if (val.value() == 0x4D42544F) {
                val = stream.read<uint32_t>();
                if (!val) return std::unexpected(ParseError::UnexpectedEof);
            }

            return parse_root(stream);
        }

    private:
        class Stream {
            std::span<const uint8_t> data_; size_t pos_ = 0;
            static constexpr uint8_t START = 0xFE, END = 0xFF, ESC = 0xFD;
        public:
            explicit Stream(std::span<const uint8_t> d) : data_(d) {}
            size_t pos() const { return pos_; }
            size_t size() const { return data_.size(); }

            [[nodiscard]] Expected<uint8_t> read_byte() {
                if (pos_ >= data_.size()) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t b = data_[pos_++];
                if (b == ESC) { 
                    if (pos_ >= data_.size()) return std::unexpected(ParseError::UnexpectedEof); 
                    b = data_[pos_++]; 
                }
                return b;
            }

            template<typename T> [[nodiscard]] Expected<T> read() {
                T v{}; uint8_t* d = reinterpret_cast<uint8_t*>(&v);
                for (size_t i = 0; i < sizeof(T); ++i) { 
                    auto b = read_byte(); 
                    if (!b) return std::unexpected(b.error()); 
                    d[i] = b.value(); 
                }
                return v;
            }

            [[nodiscard]] Expected<std::string> read_string() {
                auto len = read<uint16_t>(); 
                if (!len) return std::unexpected(len.error());
                std::string r; r.reserve(len.value());
                for (uint16_t i = 0; i < len.value(); ++i) { 
                    auto b = read_byte(); 
                    if (!b) return std::unexpected(b.error()); 
                    r.push_back(b.value()); 
                }
                return r;
            }

            [[nodiscard]] Expected<uint8_t> peek() const {
                if (pos_ >= data_.size()) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t b = data_[pos_]; 
                if (b == ESC && pos_ + 1 < data_.size()) b = data_[pos_ + 1]; 
                return b;
            }

            [[nodiscard]] Expected<uint8_t> peek_node_type() const {
                if (pos_ + 1 >= data_.size()) return std::unexpected(ParseError::UnexpectedEof);
                if (data_[pos_] != START) return std::unexpected(ParseError::InvalidFormat);
                uint8_t t = data_[pos_ + 1]; 
                if (t == ESC && pos_ + 2 < data_.size()) t = data_[pos_ + 2]; 
                return t;
            }
        };

        [[nodiscard]] Expected<bool> skip_node(Stream& s) {
            int depth = 0;
            while (true) {
                auto b = s.read_byte();
                if (!b) return std::unexpected(ParseError::UnexpectedEof);
                if (b.value() == 0xFE) {
                    depth++;
                } else if (b.value() == 0xFF) {
                    if (depth == 0) return true;
                    depth--;
                }
            }
        }

        [[nodiscard]] Expected<bool> parse_root(Stream& s) {
            auto start = s.read_byte();
            auto type = s.read<uint8_t>();
            if (!start || start.value() != 0xFE) return std::unexpected(ParseError::InvalidNode);
            if (!type || (type.value() != 0x00 && type.value() != 0x01 && type.value() != 0x02)) {
                return std::unexpected(ParseError::InvalidNode);
            }
            
            auto v = s.read<uint32_t>();
            auto w = s.read<uint16_t>();
            auto h = s.read<uint16_t>();
            auto im = s.read<uint32_t>();
            auto in = s.read<uint32_t>();
            if (!v || !w || !h || !im || !in) return std::unexpected(ParseError::UnexpectedEof);
            if (on_header) on_header({{v.value(), 0}, w.value(), h.value(), {im.value(), in.value()}});
            
            auto result = parse_map_data(s);
            if (!result) return std::unexpected(result.error());
            
            auto end = s.read_byte();
            if (!end || end.value() != 0xFF) return std::unexpected(ParseError::InvalidFormat);
            return true;
        }

        [[nodiscard]] Expected<bool> parse_map_data(Stream& s) {
            auto st = s.read_byte(); 
            auto tp = s.read<uint8_t>();
            if (!st || st.value() != 0xFE || !tp || static_cast<NodeType>(tp.value()) != NodeType::MapData) {
                return std::unexpected(ParseError::InvalidNode);
            }
            
            // Attributes parsing loop
            while (true) {
                auto p = s.peek();
                if (!p) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t next = p.value();
                if (next == 0xFF) { (void)s.read_byte(); return true; }
                if (next == 0xFE) break;
                
                (void)s.read_byte();
                switch (static_cast<DataType>(next)) {
                    case DataType::Description: {
                        auto d = s.read_string(); if (!d) return std::unexpected(d.error());
                        if (on_map_description) on_map_description(d.value()); break;
                    }
                    case DataType::ExtSpawnFile:
                    case DataType::ExtFile: {
                        auto spawn = s.read_string(); if (!spawn) return std::unexpected(spawn.error());
                        if (on_map_spawn_file) on_map_spawn_file(spawn.value()); break;
                    }
                    case DataType::ExtHouseFile: {
                        auto houses = s.read_string(); if (!houses) return std::unexpected(houses.error());
                        if (on_map_houses_file) on_map_houses_file(houses.value()); break;
                    }
                    default: {
                        // All string-based map attributes in standard OTBM are string length prefixed
                        auto skip = s.read_string();
                        if (!skip) return std::unexpected(skip.error());
                        break;
                    }
                }
            }
            
            // Node parsing loop
            while (true) {
                auto p = s.peek();
                if (!p) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t next = p.value();
                
                if (next == 0xFF) { (void)s.read_byte(); return true; }
                if (next != 0xFE) { (void)s.read_byte(); continue; }
                
                auto nt = s.peek_node_type();
                if (!nt) return std::unexpected(nt.error());
                
                switch (static_cast<NodeType>(nt.value())) {
                    case NodeType::TileArea: {
                        auto r = parse_tile_area(s); if (!r) return std::unexpected(r.error()); break;
                    }
                    case NodeType::Spawns: {
                        auto r = skip_node(s); if (!r) return std::unexpected(r.error()); break;
                    }
                    case NodeType::Towns: {
                        auto r = parse_towns(s); if (!r) return std::unexpected(r.error()); break;
                    }
                    case NodeType::Waypoints: {
                        auto r = parse_waypoints(s); if (!r) return std::unexpected(r.error()); break;
                    }
                    default: {
                        auto r = skip_node(s); if (!r) return std::unexpected(r.error()); break;
                    }
                }
            }
        }

        [[nodiscard]] Expected<bool> parse_tile_area(Stream& s) {
            (void)s.read_byte(); (void)s.read<uint8_t>(); 
            auto x = s.read<uint16_t>();
            auto y = s.read<uint16_t>();
            auto z = s.read<uint8_t>();
            if (!x || !y || !z) return std::unexpected(ParseError::UnexpectedEof);
            
            while (true) { 
                auto p = s.peek(); if (!p) return std::unexpected(p.error());
                if (p.value() == 0xFF) { (void)s.read_byte(); return true; }
                if (p.value() != 0xFE) { (void)s.read_byte(); continue; }
                
                auto nt = s.peek_node_type(); if (!nt) return std::unexpected(nt.error()); 
                if (static_cast<NodeType>(nt.value()) == NodeType::Tile) { 
                    auto r = parse_tile(s, x.value(), y.value(), z.value()); if(!r) return std::unexpected(r.error()); 
                } else if (static_cast<NodeType>(nt.value()) == NodeType::HouseTile) { 
                    auto r = parse_house_tile(s, x.value(), y.value(), z.value()); if(!r) return std::unexpected(r.error()); 
                } else {
                    auto r = skip_node(s); if(!r) return std::unexpected(r.error());
                }
            }
        }

        [[nodiscard]] Expected<bool> parse_tile_common(Stream& s, Tile& t, uint32_t house_id = 0) {
            while (true) { 
                auto p = s.peek(); if (!p) return std::unexpected(p.error()); 
                uint8_t n = p.value(); 
                if (n == 0xFF) { (void)s.read_byte(); return true; } 
                if (n == 0xFE) { 
                    auto nt = s.peek_node_type(); 
                    if (nt && static_cast<NodeType>(nt.value()) == NodeType::Item) { 
                        auto r = parse_item(s, &t, nullptr, house_id); if(!r) return std::unexpected(r.error()); 
                    } else { 
                        auto r = skip_node(s); if(!r) return std::unexpected(r.error()); 
                    }
                } else if (n == static_cast<uint8_t>(DataType::TileFlags)) { 
                    (void)s.read_byte(); 
                    auto f = s.read<uint32_t>(); 
                    if (f) t.flags = f.value(); 
                } else if (n == static_cast<uint8_t>(DataType::Item)) {
                    (void)s.read_byte();
                    Item i;
                    auto it = s.read<uint16_t>();
                    if (it) {
                        i.type = it.value();
                        if (on_item) on_item(t, i, nullptr);
                    }
                } else {
                    (void)s.read_byte();
                }
            }
        }

        [[nodiscard]] Expected<bool> parse_tile(Stream& s, uint16_t bx, uint16_t by, uint8_t z) {
            (void)s.read_byte(); (void)s.read<uint8_t>(); 
            auto dx = s.read<uint8_t>();
            auto dy = s.read<uint8_t>();
            if (!dx || !dy) return std::unexpected(ParseError::UnexpectedEof);
            
            Tile t{{static_cast<uint16_t>(bx + dx.value()), static_cast<uint16_t>(by + dy.value()), z}, 0}; 
            if (on_tile) on_tile(t);
            
            return parse_tile_common(s, t);
        }

        [[nodiscard]] Expected<bool> parse_house_tile(Stream& s, uint16_t bx, uint16_t by, uint8_t z) {
            (void)s.read_byte(); (void)s.read<uint8_t>(); 
            auto dx = s.read<uint8_t>();
            auto dy = s.read<uint8_t>();
            auto hid = s.read<uint32_t>();
            if (!dx || !dy || !hid) return std::unexpected(ParseError::UnexpectedEof);
            
            Tile t{{static_cast<uint16_t>(bx + dx.value()), static_cast<uint16_t>(by + dy.value()), z}, 0, hid.value()};
            if (on_tile) on_tile(t);
            
            return parse_tile_common(s, t, hid.value());
        }

        [[nodiscard]] Expected<bool> parse_item(Stream& s, const Tile* t, const Item* p, uint32_t hid = 0) {
            (void)s.read_byte(); (void)s.read<uint8_t>(); 
            Item i; auto it = s.read<uint16_t>(); if (!it) return std::unexpected(ParseError::UnexpectedEof); 
            i.type = it.value();
            while (true) { 
                auto pk = s.peek(); if (!pk) return std::unexpected(pk.error()); 
                uint8_t n = pk.value(); 
                if (n == 0xFF) { (void)s.read_byte(); break; } 
                if (n == 0xFE) { 
                    auto nt = s.peek_node_type(); 
                    if (nt && static_cast<NodeType>(nt.value()) == NodeType::Item) { 
                        auto r = parse_item(s, t, &i, hid); if(!r) return std::unexpected(r.error()); 
                    } else { 
                        auto r = skip_node(s); if(!r) return std::unexpected(r.error()); 
                    }
                } else { 
                    (void)s.read_byte();
                    switch (static_cast<DataType>(n)) { 
                        case DataType::Count: { auto v = s.read<uint8_t>(); if(v) i.count = v.value(); break; } 
                        case DataType::TeleDest: { i.is_portal = true; auto px = s.read<uint16_t>(); auto py = s.read<uint16_t>(); auto pz = s.read<uint8_t>(); if (px && py && pz) i.portal_exit = {px.value(), py.value(), pz.value()}; break; } 
                        case DataType::DepotId: { i.is_depot = true; auto v = s.read<uint16_t>(); if(v) i.depot_id = v.value(); break; }
                        case DataType::Text: { auto v = s.read_string(); if(v) i.text = std::move(v.value()); break; }
                        case DataType::Charges: { auto v = s.read<uint16_t>(); if(v) i.charges = v.value(); break; }
                        case DataType::ActionId: { auto v = s.read<uint16_t>(); if(v) i.aid = v.value(); break; }
                        case DataType::UniqueId: { auto v = s.read<uint16_t>(); if(v) i.uid = v.value(); break; }
                        case DataType::HouseDoorId: { i.is_house_door = true; auto v = s.read<uint8_t>(); if(v) i.door_id = v.value(); break; }
                        default: break; 
                    } 
                } 
            }
            if (on_item && t) on_item(*t, i, p); 
            return true; 
        }

        [[nodiscard]] Expected<bool> parse_towns(Stream& s) {
            (void)s.read_byte(); (void)s.read<uint8_t>();
            while (true) {
                auto p = s.peek(); if (!p) return std::unexpected(p.error());
                if (p.value() == 0xFF) { (void)s.read_byte(); return true; }
                if (p.value() != 0xFE) { (void)s.read_byte(); continue; }
                auto nt = s.peek_node_type(); 
                if (!nt || static_cast<NodeType>(nt.value()) != NodeType::Town) { (void)skip_node(s); continue; }
                auto r = parse_town(s); if (!r) return std::unexpected(r.error());
            }
        }

        [[nodiscard]] Expected<bool> parse_town(Stream& s) {
            (void)s.read_byte(); (void)s.read<uint8_t>();
            auto id = s.read<uint32_t>();
            auto name = s.read_string();
            auto x = s.read<uint16_t>();
            auto y = s.read<uint16_t>();
            auto z = s.read<uint8_t>();
            if (!id || !name || !x || !y || !z) return std::unexpected(ParseError::UnexpectedEof);
            if (on_town) on_town({id.value(), std::move(name.value()), {x.value(), y.value(), z.value()}});
            (void)s.read_byte();
            return true;
        }

        [[nodiscard]] Expected<bool> parse_waypoints(Stream& s) {
            (void)s.read_byte(); (void)s.read<uint8_t>();
            while (true) {
                auto p = s.peek(); if (!p) return std::unexpected(p.error());
                if (p.value() == 0xFF) { (void)s.read_byte(); return true; }
                if (p.value() != 0xFE) { (void)s.read_byte(); continue; }
                auto nt = s.peek_node_type(); 
                if (!nt || static_cast<NodeType>(nt.value()) != NodeType::Waypoint) { (void)skip_node(s); continue; }
                auto r = parse_waypoint(s); if (!r) return std::unexpected(r.error());
            }
        }

        [[nodiscard]] Expected<bool> parse_waypoint(Stream& s) {
            (void)s.read_byte(); (void)s.read<uint8_t>();
            auto name = s.read_string();
            auto x = s.read<uint16_t>();
            auto y = s.read<uint16_t>();
            auto z = s.read<uint8_t>();
            if (!name || !x || !y || !z) return std::unexpected(ParseError::UnexpectedEof);
            if (on_waypoint) on_waypoint({std::move(name.value()), {x.value(), y.value(), z.value()}});
            (void)s.read_byte();
            return true;
        }
    };
}
