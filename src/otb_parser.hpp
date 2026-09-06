#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <expected>
#include <unordered_map>
#include <functional>

namespace otb {
    enum class ItemAttribute : uint8_t { ServerId = 0x10, ClientId = 0x11, Name = 0x12, Flags = 0x00, MaxItems = 0x2B };
    struct ItemDefinition { uint16_t server_id{0}; uint16_t client_id{0}; std::string name; uint32_t flags{0}; uint16_t max_items{0}; };
    enum class ParseError { Success = 0, InvalidFormat, UnexpectedEof };
    template<typename T> using Expected = std::expected<T, ParseError>;

    class Parser {
    public:
        std::unordered_map<uint16_t, ItemDefinition> items;
        std::function<void(uint16_t, const ItemDefinition&)> on_item;

        [[nodiscard]] Expected<bool> parse(std::span<const uint8_t> data) {
            Stream stream(data);
            for(int i=0; i<16; ++i) (void)stream.read_byte(); // Explicitly discard nodiscard
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
            [[nodiscard]] size_t remaining() const { return data_.size() - pos_; }
        };

        [[nodiscard]] Expected<bool> parse_root(Stream& s) {
            (void)s.read_byte(); 
            (void)s.read_byte(); 
            while (s.remaining() > 0) {
                auto p = s.peek(); 
                if (!p) break;
                if (p.value() == 0xFF) { (void)s.read_byte(); break; }
                if (p.value() == 0xFE) { 
                    auto r = parse_item_def(s); 
                    if (!r) return std::unexpected(r.error()); 
                } else {
                    (void)s.read_byte();
                }
            }
            return true;
        }

        [[nodiscard]] Expected<bool> parse_item_def(Stream& s) {
            (void)s.read_byte(); 
            (void)s.read_byte(); 
            ItemDefinition item;
            while (s.remaining() > 0) {
                auto p = s.peek(); 
                if (!p) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t n = p.value(); 
                if (n == 0xFF) { (void)s.read_byte(); break; }
                (void)s.read_byte(); // Consume attribute type
                switch (static_cast<ItemAttribute>(n)) {
                    case ItemAttribute::ServerId: { 
                        auto v = s.read<uint16_t>(); 
                        if(v) item.server_id = v.value(); 
                        break; 
                    }
                    case ItemAttribute::ClientId: { 
                        auto v = s.read<uint16_t>(); 
                        if(v) item.client_id = v.value(); 
                        break; 
                    }
                    case ItemAttribute::Name: { 
                        auto v = s.read_string(); 
                        if(v) item.name = std::move(v.value()); 
                        break; 
                    }
                    case ItemAttribute::Flags: { 
                        auto v = s.read<uint32_t>(); 
                        if(v) item.flags = v.value(); 
                        break; 
                    }
                    default: 
                        (void)s.read<uint16_t>(); 
                        break; // Safe skip for unknown
                }
            }
            if (item.server_id > 0) { 
                items[item.server_id] = item; 
                if (on_item) on_item(item.server_id, item); 
            }
            return true;
        }
    };
}
