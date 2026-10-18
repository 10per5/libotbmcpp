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

    // How to treat an attribute id this parser does not recognise.
    // Historically the parser assumed every unknown attribute was a u16 and
    // silently skipped it; that desynchronises the stream whenever the real
    // value is a different width (finding #2). Defaulting to Fail turns that
    // silent corruption into a diagnosable error; SkipAsU16 restores the old,
    // lenient behaviour for callers that know their files use only u16 unknowns.
    enum class UnknownAttributePolicy { SkipAsU16, Fail };

    // Opt-in indexing. by_server_id preserves the original behaviour; by_client_id
    // adds a second index keyed by client_id so consumers that hold client ids
    // (map files, .dat tables, appearance dbs) get an O(1) lookup instead of
    // building their own second map (finding #12).
    struct IndexOptions { bool by_server_id = true; bool by_client_id = false; };

    class Parser {
    public:
        std::unordered_map<uint16_t, ItemDefinition> items;          // keyed by server_id (original behaviour)
        std::unordered_map<uint16_t, ItemDefinition> client_items;   // keyed by client_id (opt-in, finding #12)

        IndexOptions index{};
        UnknownAttributePolicy unknown_attribute_policy = UnknownAttributePolicy::Fail;

        std::function<void(uint16_t, const ItemDefinition&)> on_item;
        std::function<void(uint16_t, const ItemDefinition&)> on_client_item;

        // Success carries no payload (the previous Expected<bool> always returned
        // true — see finding #10). Any structural problem is reported as an error.
        [[nodiscard]] Expected<void> parse(std::span<const uint8_t> data) {
            items.clear();
            client_items.clear();
            // A file shorter than the 16-byte header is truncated, not a valid
            // (empty) OTB — fail rather than report success (finding #1/#3).
            if (data.size() < 16) return std::unexpected(ParseError::UnexpectedEof);
            Stream stream(data);
            for (int i = 0; i < 16; ++i) {               // OTB 16-byte header
                auto b = stream.read_byte();
                if (!b) return std::unexpected(b.error());
            }
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

        [[nodiscard]] Expected<void> parse_root(Stream& s) {
            auto st = s.read_byte();
            if (!st) return std::unexpected(ParseError::UnexpectedEof);
            if (st.value() != 0xFE) return std::unexpected(ParseError::InvalidFormat); // not an OTB node stream (#3)
            auto tp = s.read_byte();
            if (!tp) return std::unexpected(ParseError::UnexpectedEof);
            while (true) {
                auto p = s.peek();
                if (!p) return std::unexpected(ParseError::UnexpectedEof); // truncated root (#1)
                uint8_t n = p.value();
                if (n == 0xFF) { (void)s.read_byte(); return {}; }        // root terminator
                if (n == 0xFE) {
                    auto r = parse_item_def(s);
                    if (!r) return std::unexpected(r.error());
                } else {
                    (void)s.read_byte();
                }
            }
        }

        [[nodiscard]] Expected<void> parse_item_def(Stream& s) {
            auto st = s.read_byte();
            if (!st) return std::unexpected(ParseError::UnexpectedEof);
            if (st.value() != 0xFE) return std::unexpected(ParseError::InvalidFormat);
            auto tp = s.read_byte();
            if (!tp) return std::unexpected(ParseError::UnexpectedEof);
            ItemDefinition item;
            bool terminated = false;
            while (s.remaining() > 0) {
                auto p = s.peek();
                if (!p) return std::unexpected(ParseError::UnexpectedEof);
                uint8_t n = p.value();
                if (n == 0xFF) { (void)s.read_byte(); terminated = true; break; }
                (void)s.read_byte(); // consume attribute type
                switch (static_cast<ItemAttribute>(n)) {
                    case ItemAttribute::ServerId: {
                        auto v = s.read<uint16_t>();
                        if (!v) return std::unexpected(v.error());
                        item.server_id = v.value();
                        break;
                    }
                    case ItemAttribute::ClientId: {
                        auto v = s.read<uint16_t>();
                        if (!v) return std::unexpected(v.error());
                        item.client_id = v.value();
                        break;
                    }
                    case ItemAttribute::Name: {
                        auto v = s.read_string();
                        if (!v) return std::unexpected(v.error());
                        item.name = std::move(v.value());
                        break;
                    }
                    case ItemAttribute::Flags: {
                        auto v = s.read<uint32_t>();
                        if (!v) return std::unexpected(v.error());
                        item.flags = v.value();
                        break;
                    }
                    case ItemAttribute::MaxItems: {           // finding #4: was silently dropped
                        auto v = s.read<uint16_t>();
                        if (!v) return std::unexpected(v.error());
                        item.max_items = v.value();
                        break;
                    }
                    default: {                                // finding #2
                        // We cannot know an unknown attribute's width, so we cannot
                        // skip it safely. Fail loudly (default) or, if the caller
                        // opts in, assume the legacy u16 skip.
                        if (unknown_attribute_policy == UnknownAttributePolicy::Fail)
                            return std::unexpected(ParseError::InvalidFormat);
                        (void)s.read<uint16_t>();            // lenient legacy behaviour
                        break;
                    }
                }
            }
            if (!terminated) return std::unexpected(ParseError::UnexpectedEof); // ran out before 0xFF (#1)
            if (index.by_server_id && item.server_id > 0) {
                items.emplace(item.server_id, item);          // first-wins on duplicates (#6)
                if (on_item) on_item(item.server_id, item);
            }
            if (index.by_client_id && item.client_id > 0) {
                client_items.emplace(item.client_id, item);   // first-wins, never clobbers (#5/#12)
                if (on_client_item) on_client_item(item.client_id, item);
            }
            return {};
        }
    };
}
