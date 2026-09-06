#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <expected>
#include <functional>

namespace otbm {
    enum class NodeType : uint8_t { Root=0x00, MapData=0x02, TileArea=0x04, Tile=0x05, Item=0x06, Towns=0x0C, Town=0x0D, HouseTile=0x0E, Waypoints=0x0F, Waypoint=0x10 };
    enum class DataType : uint8_t { Description=0x01, ExtSpawnFile=0x0B, ExtHouseFile=0x0D, TileFlags=0x03, Count=0x0F, TeleDest=0x08, DepotId=0x0A, Text=0x06, Charges=0x16, ActionId=0x04, UniqueId=0x05, HouseDoorId=0x0E };
    
    struct Position { uint16_t x{0}, y{0}; uint8_t z{0}; };
    struct Tile { Position pos; uint32_t flags{0}; };
    struct Item { uint16_t type{0}; uint8_t count{1}; uint16_t aid{0}, uid{0}, charges{0}; std::string text; bool is_portal{false}; Position portal_exit; bool is_depot{false}; uint16_t depot_id{0}; bool is_house_door{false}; uint8_t door_id{0}; };
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
            auto major = stream.read<uint32_t>();
            if (!major || major.value() != 0) return std::unexpected(ParseError::UnsupportedVersion);
            return parse_root(stream);
        }

    private:
        class Stream {
            std::span<const uint8_t> data_; size_t pos_ = 0;
            static constexpr uint8_t START = 0xFE, END = 0xFF, ESC = 0xFD;
        public:
            explicit Stream(std::span<const uint8_t> d) : data_(d) {}
            [[nodiscard]] Expected<uint8_t> read_byte() {
                if (pos_ >= data_.size()) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t b = data_[pos_++];
                if (b == ESC) { if (pos_ >= data_.size()) return std::unexpected(ParseError::UnexpectedEof); b = data_[pos_++]; }
                return b;
            }
            template<typename T> [[nodiscard]] Expected<T> read() {
                T v{}; uint8_t* d = reinterpret_cast<uint8_t*>(&v);
                for (size_t i = 0; i < sizeof(T); ++i) { auto b = read_byte(); if (!b) return std::unexpected(b.error()); d[i] = b.value(); }
                return v;
            }
            [[nodiscard]] Expected<std::string> read_string() {
                auto len = read<uint16_t>(); if (!len) return std::unexpected(len.error());
                std::string r; r.reserve(len.value());
                for (uint16_t i = 0; i < len.value(); ++i) { auto b = read_byte(); if (!b) return std::unexpected(b.error()); r.push_back(b.value()); }
                return r;
            }
            [[nodiscard]] Expected<uint8_t> peek() const {
                if (pos_ >= data_.size()) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t b = data_[pos_]; if (b == ESC && pos_ + 1 < data_.size()) b = data_[pos_ + 1]; return b;
            }
            [[nodiscard]] Expected<uint8_t> peek_node_type() const {
                if (pos_ + 1 >= data_.size()) return std::unexpected(ParseError::UnexpectedEof);
                if (data_[pos_] != START) return std::unexpected(ParseError::InvalidFormat);
                uint8_t t = data_[pos_ + 1]; if (t == ESC && pos_ + 2 < data_.size()) t = data_[pos_ + 2]; return t;
            }
        };

        [[nodiscard]] Expected<bool> parse_root(Stream& s) {
            auto start = s.read_byte();
            auto type = s.read<uint8_t>();
            if (!start || start.value() != 0xFE || !type || static_cast<NodeType>(type.value()) != NodeType::Root) 
                return std::unexpected(ParseError::InvalidNode);
            
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
            if (!st || st.value() != 0xFE || !tp || static_cast<NodeType>(tp.value()) != NodeType::MapData) 
                return std::unexpected(ParseError::InvalidNode);
            
            while (true) { 
                auto p = s.peek(); 
                if (!p || p.value() != static_cast<uint8_t>(DataType::Description)) break; 
                (void)s.read_byte(); 
                auto d = s.read_string(); 
                if (!d) return std::unexpected(d.error()); 
                if (on_map_description) on_map_description(d.value()); 
            }
            
            auto spawn_type = s.peek();
            if (spawn_type && spawn_type.value() == static_cast<uint8_t>(DataType::ExtSpawnFile)) {
                (void)s.read_byte();
                auto spawn = s.read_string();
                if (!spawn) return std::unexpected(spawn.error());
                if (on_map_spawn_file) on_map_spawn_file(spawn.value());
            }
            
            auto house_type = s.peek();
            if (house_type && house_type.value() == static_cast<uint8_t>(DataType::ExtHouseFile)) {
                (void)s.read_byte();
                auto houses = s.read_string();
                if (!houses) return std::unexpected(houses.error());
                if (on_map_houses_file) on_map_houses_file(houses.value());
            }
            
            while (true) { 
                auto p = s.peek(); 
                if (!p) return std::unexpected(p.error());
                if (p.value() == 0xFF) break; 
                if (p.value() != 0xFE) break; 
                
                auto nt = s.peek_node_type(); 
                if (!nt || static_cast<NodeType>(nt.value()) != NodeType::TileArea) break; 
                auto r = parse_tile_area(s); 
                if (!r) return std::unexpected(r.error()); 
            }
            
            auto towns_p = s.peek();
            if (towns_p && towns_p.value() == 0xFE) {
                auto nt = s.peek_node_type();
                if (nt && static_cast<NodeType>(nt.value()) == NodeType::Towns) {
                    auto r = parse_towns(s); 
                    if (!r) return std::unexpected(r.error());
                }
            }
            
            auto waypoints_p = s.peek();
            if (waypoints_p && waypoints_p.value() == 0xFE) {
                auto nt = s.peek_node_type();
                if (nt && static_cast<NodeType>(nt.value()) == NodeType::Waypoints) {
                    auto r = parse_waypoints(s); 
                    if (!r) return std::unexpected(r.error());
                }
            }
            
            auto end = s.read_byte(); 
            if (!end || end.value() != 0xFF) return std::unexpected(ParseError::InvalidFormat);
            return true;
        }

        [[nodiscard]] Expected<bool> parse_tile_area(Stream& s) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>(); 
            auto x = s.read<uint16_t>();
            auto y = s.read<uint16_t>();
            auto z = s.read<uint8_t>();
            if (!x || !y || !z) return std::unexpected(ParseError::UnexpectedEof);
            
            while (true) { 
                auto p = s.peek(); 
                if (!p) return std::unexpected(p.error());
                if (p.value() == 0xFF) { (void)s.read_byte(); break; }
                if (p.value() != 0xFE) break;
                
                auto nt = s.peek_node_type(); 
                if (!nt) return std::unexpected(nt.error()); 
                if (static_cast<NodeType>(nt.value()) == NodeType::Tile) { 
                    auto r = parse_tile(s, x.value(), y.value(), z.value()); 
                    if(!r) return std::unexpected(r.error()); 
                } else if (static_cast<NodeType>(nt.value()) == NodeType::HouseTile) { 
                    auto r = parse_house_tile(s, x.value(), y.value(), z.value()); 
                    if(!r) return std::unexpected(r.error()); 
                } else break; 
            }
            return true;
        }

        [[nodiscard]] Expected<bool> parse_tile(Stream& s, uint16_t bx, uint16_t by, uint8_t z) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>(); 
            auto dx = s.read<uint8_t>();
            auto dy = s.read<uint8_t>();
            if (!dx || !dy) return std::unexpected(ParseError::UnexpectedEof);
            
            uint16_t final_x = static_cast<uint16_t>(bx + dx.value());
            uint16_t final_y = static_cast<uint16_t>(by + dy.value());
            
            Tile t{{final_x, final_y, z}, 0}; 
            if (on_tile) on_tile(t);
            
            while (true) { 
                auto p = s.peek(); 
                if (!p) return std::unexpected(p.error()); 
                uint8_t n = p.value(); 
                if (n == 0xFF) { (void)s.read_byte(); break; } 
                if (n == 0xFE) { 
                    auto nt = s.peek_node_type(); 
                    if (nt && static_cast<NodeType>(nt.value()) == NodeType::Item) { 
                        auto r = parse_item(s, &t, nullptr); 
                        if(!r) return std::unexpected(r.error()); 
                    } else break; 
                } else if (n == static_cast<uint8_t>(DataType::TileFlags)) { 
                    (void)s.read_byte(); 
                    auto f = s.read<uint32_t>(); 
                    if(f) t.flags = f.value(); 
                } else {
                    (void)s.read_byte(); 
                }
            }
            return true;
        }

        [[nodiscard]] Expected<bool> parse_house_tile(Stream& s, uint16_t bx, uint16_t by, uint8_t z) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>(); 
            auto dx = s.read<uint8_t>();
            auto dy = s.read<uint8_t>();
            auto hid = s.read<uint32_t>();
            if (!dx || !dy || !hid) return std::unexpected(ParseError::UnexpectedEof);
            
            uint16_t final_x = static_cast<uint16_t>(bx + dx.value());
            uint16_t final_y = static_cast<uint16_t>(by + dy.value());
            
            Tile t{{final_x, final_y, z}, 0};
            while (true) { 
                auto p = s.peek(); 
                if (!p) return std::unexpected(p.error()); 
                if (p.value() == 0xFF) { (void)s.read_byte(); break; } 
                if (p.value() == 0xFE) { 
                    auto nt = s.peek_node_type(); 
                    if (nt && static_cast<NodeType>(nt.value()) == NodeType::Item) { 
                        auto r = parse_item(s, &t, nullptr, hid.value()); 
                        if(!r) return std::unexpected(r.error()); 
                    } else break; 
                } else {
                    (void)s.read_byte(); 
                }
            }
            return true;
        }

        [[nodiscard]] Expected<bool> parse_item(Stream& s, const Tile* t, const Item* p, uint32_t hid = 0) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>(); 
            Item i; 
            auto it = s.read<uint16_t>(); 
            if (!it) return std::unexpected(ParseError::UnexpectedEof); 
            i.type = it.value();
            
            while (true) { 
                auto pk = s.peek(); 
                if (!pk) return std::unexpected(pk.error()); 
                uint8_t n = pk.value(); 
                if (n == 0xFF) { (void)s.read_byte(); break; } 
                if (n == 0xFE) { 
                    auto nt = s.peek_node_type(); 
                    if (nt && static_cast<NodeType>(nt.value()) == NodeType::Item) { 
                        auto r = parse_item(s, t, &i, hid); 
                        if(!r) return std::unexpected(r.error()); 
                    } else break; 
                } else { 
                    (void)s.read_byte();
                    switch (static_cast<DataType>(n)) { 
                        case DataType::Count: { 
                            auto v = s.read<uint8_t>(); 
                            if(v) i.count = v.value(); 
                            break; 
                        } 
                        case DataType::TeleDest: { 
                            i.is_portal = true; 
                            auto px = s.read<uint16_t>();
                            auto py = s.read<uint16_t>();
                            auto pz = s.read<uint8_t>();
                            if (px && py && pz) i.portal_exit = {px.value(), py.value(), pz.value()}; 
                            break; 
                        } 
                        case DataType::DepotId: { 
                            i.is_depot = true; 
                            auto v = s.read<uint16_t>(); 
                            if(v) i.depot_id = v.value(); 
                            break; 
                        }
                        case DataType::Text: { 
                            auto v = s.read_string(); 
                            if(v) i.text = std::move(v.value()); 
                            break; 
                        }
                        case DataType::Charges: { 
                            auto v = s.read<uint16_t>(); 
                            if(v) i.charges = v.value(); 
                            break; 
                        }
                        case DataType::ActionId: { 
                            auto v = s.read<uint16_t>(); 
                            if(v) i.aid = v.value(); 
                            break; 
                        }
                        case DataType::UniqueId: { 
                            auto v = s.read<uint16_t>(); 
                            if(v) i.uid = v.value(); 
                            break; 
                        }
                        case DataType::HouseDoorId: { 
                            i.is_house_door = true; 
                            auto v = s.read<uint8_t>(); 
                            if(v) i.door_id = v.value(); 
                            break; 
                        }
                        default: break; 
                    } 
                } 
            }
            if (on_item && t) on_item(*t, i, p); 
            return true; 
        }

        [[nodiscard]] Expected<bool> parse_towns(Stream& s) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>();
            while (true) {
                auto p = s.peek(); 
                if (!p) return std::unexpected(p.error());
                if (p.value() == 0xFF) { (void)s.read_byte(); break; }
                if (p.value() != 0xFE) break;
                auto nt = s.peek_node_type(); 
                if (!nt || static_cast<NodeType>(nt.value()) != NodeType::Town) break;
                auto r = parse_town(s); 
                if (!r) return std::unexpected(r.error());
            }
            return true;
        }

        [[nodiscard]] Expected<bool> parse_town(Stream& s) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>();
            auto id = s.read<uint32_t>();
            auto name = s.read_string();
            auto x = s.read<uint16_t>();
            auto y = s.read<uint16_t>();
            auto z = s.read<uint8_t>();
            if (!id || !name || !x || !y || !z) return std::unexpected(ParseError::UnexpectedEof);
            if (on_town) on_town({id.value(), std::move(name.value()), {x.value(), y.value(), z.value()}});
            (void)s.read_byte(); // NODE_END
            return true;
        }

        [[nodiscard]] Expected<bool> parse_waypoints(Stream& s) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>();
            while (true) {
                auto p = s.peek(); 
                if (!p) return std::unexpected(p.error());
                if (p.value() == 0xFF) { (void)s.read_byte(); break; }
                if (p.value() != 0xFE) break;
                auto nt = s.peek_node_type(); 
                if (!nt || static_cast<NodeType>(nt.value()) != NodeType::Waypoint) break;
                auto r = parse_waypoint(s); 
                if (!r) return std::unexpected(r.error());
            }
            return true;
        }

        [[nodiscard]] Expected<bool> parse_waypoint(Stream& s) {
            (void)s.read_byte(); 
            (void)s.read<uint8_t>();
            auto name = s.read_string();
            auto x = s.read<uint16_t>();
            auto y = s.read<uint16_t>();
            auto z = s.read<uint8_t>();
            if (!name || !x || !y || !z) return std::unexpected(ParseError::UnexpectedEof);
            if (on_waypoint) on_waypoint({std::move(name.value()), {x.value(), y.value(), z.value()}});
            (void)s.read_byte(); // NODE_END
            return true;
        }
    };
}
