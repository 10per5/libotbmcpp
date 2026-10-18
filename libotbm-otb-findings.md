# `otb_parser.hpp` — findings and a feature request

Review of `src/otb_parser.hpp` @ `165b9c5`, header-only, vendored read-only as a
third-party dependency. Line numbers refer to that revision. Everything below was
found by reading the file; each item includes a concrete repro so it can be
confirmed independently.

None of this blocks us today — we resolve item ids through the `.dat` index and
don't call this parser. Sending it because the two structural issues (#1, #2) are
silent-corruption class, and the feature request (#7) makes the parser usable by
consumers that hold client ids.

---

## Summary

| # | severity | finding | lines |
|---|---|---|---|
| 1 | **High** | Truncated or empty input parses "successfully" with zero items | 22, 70, 88 |
| 2 | **High** | Unknown attribute is skipped as `u16`; a non-16-bit attribute desyncs the stream and corrupts every subsequent item | 115–117 |
| 3 | **Medium** | No header validation — `ParseError::InvalidFormat` is unreachable, and a non-OTB file parses cleanly | 12, 22, 68–69 |
| 4 | **Medium** | `MaxItems` / `max_items` is declared but never populated | 10, 11, 115–117 |
| 5 | **Medium** | Entries with a `client_id` but no `server_id` are dropped entirely | 120–123 |
| 6 | **Low** | Duplicate server ids are last-wins (file-order dependent) | 121 |
| 7 | **Low** | Read errors on known attributes are swallowed, leaving partial data and continuing | 96–98, 101–103, 107–108, 111–112 |
| 8 | **Low** | `parse()` does not reset `items`; a second call merges into the first | 17, 20–24 |
| 9 | **Low** | `read<T>()` assumes little-endian by address aliasing — wrong on a big-endian host | 38–46 |
| 10 | **Low** | `Expected<bool>` value is always `true` on success | throughout |
| 11 | **Low** | `unordered_map` gives no stable iteration order for a serialization use case | 17 |
| 12 | **Feature** | Optional client-id index (full spec below) | — |

---

## 1. Truncated or empty input reports success — High

**Where:** `parse()` line 22, `parse_root()` line 70, `parse_item_def()` line 88.

Three loops terminate on `remaining() == 0` rather than on a structural error, and
the 16-byte header skip discards its results:

```cpp
for(int i=0; i<16; ++i) (void)stream.read_byte();   // every error discarded
```

For a **0-byte input**: all 16 `read_byte()` calls return `std::unexpected`
(discarded), `parse_root` reads two more (discarded), `remaining() == 0` so the
loop body never runs, and it returns `true`. `parse()` therefore returns
`Expected<bool>{true}` with an empty `items`.

**Repro:** `Parser{}.parse({})` → `has_value() == true`, `items.empty() == true`.

The same shape means a file truncated *mid-item* also returns `true` — the loop at
line 88 exits on `remaining() == 0` and line 124 returns `true` with a
half-populated `ItemDefinition`.

**Impact:** a consumer cannot distinguish "this OTB has no items" from "this file
is truncated". Silent empty result, no error, no log line.

**Suggested fix** — propagate the header skip and treat exhaustion inside a node as
an error rather than a normal exit:

```cpp
[[nodiscard]] Expected<bool> parse(std::span<const uint8_t> data) {
    Stream stream(data);
    for (int i = 0; i < 16; ++i)
        if (auto b = stream.read_byte(); !b) return std::unexpected(b.error());
    return parse_root(stream);
}
```

and in `parse_item_def`, replace `while (s.remaining() > 0)` with a check that
distinguishes "hit the 0xFF terminator" (fine) from "ran out of data" (not fine):

```cpp
// on loop exit without seeing 0xFF:
return std::unexpected(ParseError::UnexpectedEof);
```

---

## 2. Unknown attributes are skipped as `u16` — High

**Where:** lines 115–117.

```cpp
default:
    (void)s.read<uint16_t>();
    break; // Safe skip for unknown
```

The comment says "safe skip", but the width of the skipped value is **assumed**,
not known. The switch handles four attributes: `ServerId` (u16), `ClientId` (u16),
`Name` (u16-prefixed string), `Flags` (u32). Any other attribute id — including
every attribute added by a client version this parser doesn't know about — is
skipped as exactly two bytes.

If the real value is a u8, a u32, or a length-prefixed string, the read lands at
the wrong offset. Everything after it is misparsed: the next byte is interpreted as
an attribute *type*, which may consume the node's 0xFF terminator as data, which
makes the parser believe the item continues into the next item's node header.
There is no resync marker in the format, so the damage is unrecoverable.

**Failure mode is silent and wrong, not loud**: `parse()` returns `true` with a
plausible-looking but incorrect `items` map.

**Question for you:** is there a reference table of attribute-id → width you're
skipping to a fixed value for, or was `u16` chosen as a "most attributes are u16"
heuristic? If the former, the table belongs in the parser as a
`constexpr` lookup rather than a `default:` branch. If the latter, the honest
behaviour is to fail loudly:

```cpp
default:
    // Cannot know an unknown attribute's width, so we cannot skip it safely.
    return std::unexpected(ParseError::InvalidFormat);
```

which finally makes `InvalidFormat` reachable (see #3) and turns silent corruption
into a diagnosable error. If you'd rather stay lenient, an opt-in
`UnknownAttributePolicy { SkipAsU16, Fail }` on the `Parser` covers both.

---

## 3. No header validation; `InvalidFormat` is unreachable — Medium

**Where:** `ParseError` line 12, header skip line 22, root prologue lines 68–69.

`ParseError::InvalidFormat` is declared and never returned anywhere in the file —
the only error the parser can produce is `UnexpectedEof`. The prologue is:

```cpp
for (int i = 0; i < 16; ++i) (void)stream.read_byte();   // otb::Parser::parse
...
(void)s.read_byte();  // expected: root node start (0xFE)
(void)s.read_byte();  // expected: root node type  (0x0D, ItemDef)
```

Neither pair is checked against what it's supposed to be. Any 16-byte prefix is
accepted as an OTB header, and any two bytes are accepted as a root node. Feeding
the parser a `.otbm`, a `.spr`, or random bytes yields `true` with an empty or
nonsense `items`.

Related: the 16-byte skip is hard-coded to a single header layout. If OTB versions
differ in header size, an older or newer file is misread from byte 0 — and with no
signature check the symptom is garbage rather than an error (see #2).

**Suggested fix** — read the header explicitly and validate:

```cpp
constexpr std::span<const uint8_t, 16> kHeaderSize{};
// ...
if (data.size() < 16) return std::unexpected(ParseError::UnexpectedEof);
// validate the OTB signature/version words here; reject with InvalidFormat
```

This makes the error enum meaningful and turns "wrong file type" from a silent
empty result into a clear diagnostic. It also gives you the natural place to
accept several header versions explicitly.

---

## 4. `MaxItems` / `max_items` declared but never populated — Medium

**Where:** line 10 (`enum`), line 11 (`struct` field), lines 115–117 (`default`).

`ItemAttribute::MaxItems = 0x2B` is in the enum and `ItemDefinition::max_items`
exists in the struct, but there is no `case` for it. `0x2B` falls into `default`,
which reads two bytes and throws them away.

The width happens to be right (it is a u16), so this does **not** desync the
stream — it is a purely silent data loss. Any consumer reading
`item.max_items` gets `0` for every item with no indication that the field was
never filled.

**Suggested fix:** add the case. If it's intentionally unparsed, drop both the enum
value and the struct field so the type doesn't advertise something it can't deliver.

```cpp
case ItemAttribute::MaxItems: {
    auto v = s.read<uint16_t>();
    if (v) item.max_items = v.value();
    break;
}
```

---

## 5. Entries with a `client_id` but no `server_id` are dropped — Medium

**Where:** lines 120–123.

```cpp
if (item.server_id > 0) {
    items[item.server_id] = item;
    if (on_item) on_item(item.server_id, item);
}
```

The guard gates **both** the insert and the callback on `server_id`. An entry
carrying a valid `client_id` but no `server_id` is therefore invisible — not in the
map, and the callback never fires — even though `client_id` is present in the data
the parser already read.

For a consumer holding client ids (a map file's item values, a `.dat` table, a
FlatBuffers appearance db), this is the interesting case, and it's the one that
gets missed. It fails as a *lookup miss* rather than an error, so it presents as
"items silently don't draw".

**Suggested fix:** treat the two ids independently rather than nesting one behind
the other — see the feature request in §7 for the shape.

---

## 6. Duplicate server ids are last-wins — Low

**Where:** line 121.

`items[item.server_id] = item;` overwrites, so for a file containing two entries
with the same `server_id`, the definition that survives depends on file order.
Malformed input only, but it makes test fixtures order-dependent and makes
reproducible output impossible from a malformed source.

**Suggested fix:** `items.emplace(item.server_id, item);` — first definition wins,
deterministic and independent of how the entries happen to be ordered. Would
change behaviour only for duplicate input; worth a line in the release notes.

---

## 7. Read errors on known attributes are swallowed — Low

**Where:** lines 96–98, 101–103, 107–108, 111–112.

Every known-attribute case uses `if (v) item.x = v.value();` and drops the error
otherwise. Combined with the `while (s.remaining() > 0)` termination, a truncated
file yields a partially-populated `ItemDefinition` and a `true` return. For
`Name` it's worse: a failed `read_string` leaves the stream positioned partway
through the string, and parsing continues from there.

**Suggested fix:** propagate. A failed read inside a node is always fatal to that
node:

```cpp
case ItemAttribute::ServerId: {
    auto v = s.read<uint16_t>();
    if (!v) return std::unexpected(v.error());
    item.server_id = v.value();
    break;
}
```

---

## 8. `parse()` does not reset `items` — Low

**Where:** line 17 (public member), lines 20–24 (`parse`).

`items` is a public member and `parse()` never clears it. Calling `parse()` twice
on the same `Parser` merges both files into one map, and both runs fire `on_item`.
Whether that's desired is arguable, but it's undocumented and non-obvious.

**Suggested fix:** either `items.clear();` at the top of `parse()`, or document the
accumulation explicitly. A `reset()` method would cover both.

---

## 9. `read<T>()` assumes little-endian via address aliasing — Low

**Where:** lines 38–46.

```cpp
T v{}; uint8_t* d = reinterpret_cast<uint8_t*>(&v);
for (size_t i = 0; i < sizeof(T); ++i) { ...; d[i] = b.value(); }
```

This writes successive stream bytes to successive *addresses* of `v`, which
produces a little-endian value **only because every realistic target host is
little-endian**. On a big-endian host every multi-byte value comes out byte-swapped
— a `u32` flags field with bits mirrored across byte boundaries — with no
diagnostic.

Cheap and completely portable fix:

```cpp
template<typename T> [[nodiscard]] Expected<T> read() {
    T v{};
    for (size_t i = 0; i < sizeof(T); ++i) {
        auto b = read_byte();
        if (!b) return std::unexpected(b.error());
        v = static_cast<T>(v | (static_cast<T>(b.value()) << (8 * i)));  // LE
    }
    return v;
}
```

Also makes the byte order of the format explicit in the code rather than implied
by the host.

---

## 10. `Expected<bool>` value is always `true` — Low

`parse`, `parse_root` and `parse_item_def` all return `Expected<bool>` but no path
ever returns `false` — success is always the literal `true`. A caller writing
`if (!result.value()) ...` compiles, and that branch is dead code.

**Suggested fix:** `Expected<void>`, which states the real contract: success means
"no error", and the payload carries nothing.

---

## 11. `unordered_map` has no stable iteration order — Low

`items` is `std::unordered_map<uint16_t, ItemDefinition>`, so iteration order is
unspecified. Consumers that enumerate items — writing an OTB back out, generating
a report, feeding a diff — get a different order between runs, between libstdc++
and libc++, and between debug and release builds. `server_id` is dense and small,
so an ordered container is cheap.

**Suggested fix:** if enumeration order matters to anyone, either use
`std::map<uint16_t, ItemDefinition>` or hand out a `std::span<const ItemDefinition>`
sorted by id alongside the lookup map. Keeping the map for O(1) lookup and adding a
sorted accessor covers both use cases without penalizing either.

---

## 12. Feature request — optional client-id index

**Motivation.** `items` is keyed by `server_id`. Consumers whose ids are *client*
ids — map files (whose item values are client ids), `.dat` tables, appearance
databases — have no index to hit, so `items.find(id)` returns `end()` for every
lookup and the failure is a miss, not an error. Right now the only remedy is for
each consumer to build its own second map while parsing.

Combined with #5, an entry with a client id but no server id is unreachable through
the public API at all.

**Proposal.** Additive, opt-in, default-off:

```cpp
struct IndexOptions {
    bool by_server_id = true;   // existing behaviour, unchanged
    bool by_client_id = false;  // opt-in second index
};

class Parser {
public:
    std::unordered_map<uint16_t, ItemDefinition> items;          // unchanged
    std::unordered_map<uint16_t, ItemDefinition> client_items;   // new

    IndexOptions index{};
    std::function<void(uint16_t, const ItemDefinition&)> on_item;         // unchanged
    std::function<void(uint16_t, const ItemDefinition&)> on_client_item; // new

    [[nodiscard]] Expected<void> parse(std::span<const uint8_t> data);
};
```

and the tail of `parse_item_def`:

```cpp
if (index.by_server_id && item.server_id > 0) {
    items.emplace(item.server_id, item);                 // was: items[...] = item  (see #6)
    if (on_item) on_item(item.server_id, item);
}
if (index.by_client_id && item.client_id > 0) {
    // emplace => FIRST definition wins on a duplicate client id, never clobbers
    client_items.emplace(item.client_id, item);
    if (on_client_item) on_client_item(item.client_id, item);
}
```

**Behaviour contract**

| case | by_server_id (default) | by_client_id (opt-in) |
|---|---|---|
| `server_id > 0`, `client_id > 0` | indexed + `on_item` | indexed + `on_client_item` |
| `server_id == 0`, `client_id > 0` | dropped (today's behaviour, preserved) | indexed + `on_client_item` |
| `client_id == 0` | — | not in `client_items` |
| duplicate `client_id` | — | first wins, never clobbers |
| duplicate `server_id` | first wins (**behaviour change**, see #6) | — |
| index disabled | byte-identical to today | map stays empty, no allocation |

**Why this shape**

| option | why not |
|---|---|
| Template the key type (`Parser<ServerId>` / `Parser<ClientId>`) | invasive; doubles compile time for what is a two-map problem |
| `findByClientId()` with a linear scan | O(n) per lookup × thousands of tiles per map render — the point is an O(1) hit |
| Rename `items` → `items_by_server_id` | source-breaking for every existing user; belongs in a major version |
| `std::unordered_multimap` for client ids | heavier node allocation for what is 1:1 in practice |
| Default `by_client_id = true` | silently doubles memory for everyone to serve a minority; violates least surprise |

**Compatibility:** purely additive — new members, default-off flag, no existing
signature changes. Header-only, so no ABI impact beyond recompilation. The single
behavioural change is #6's `[]=` → `emplace`, which only affects malformed input.

---

## Proposed test plan

Unit, on synthetic blobs built with the same escaping rules as the parser:

1. both ids present → entry in both indexes, both callbacks fire with the correct key
2. `client_id` only → absent from `items`, present in `client_items` (**regression for #5**)
3. duplicate `client_id` across two server ids → `client_items.size() == 1`, first retained
4. `by_client_id == false` → `client_items` empty, `on_client_item` never invoked, `items` byte-identical to today's output
5. both ids zero → ignored entirely
6. empty input → `UnexpectedEof` (**regression for #1**)
7. input truncated mid-item → `UnexpectedEof` (**regression for #1**)
8. input shorter than the 16-byte header → `UnexpectedEof` (**regression for #1**)
9. non-OTBM input (e.g. a `.otbm` header, or zero bytes) → `InvalidFormat` (**regression for #3**)
10. attribute id with a non-u16 width → either `InvalidFormat` or a documented policy (**regression for #2**)
11. `MaxItems` present → `item.max_items` populated (**regression for #4**)
12. `parse()` called twice → second run's `items` not polluted (**regression for #8**)

Integration, against the existing `tests/fixture/world.otbm`: parse once per index
mode and assert the item/tile/town/waypoint counts are unchanged, and that every
`on_item` key resolves in `client_items`.

---

## Appendix — same class of issue in `otbm_parser.hpp`
Separate file, offered because the root causes look identical and fixing one
without the other would leave the library inconsistent. Nothing here blocks us —
we just skip `Spawns` — but A2 and A3 are the same silent-corruption shape as #2.

**A1. `Spawns` are parsed past, never surfaced — Medium.** (line 221)
`parse_map_data` has `case NodeType::Spawns: { skip_node(s); break; }`, while
`NodeType::Monster` already exists in the enum. ~30 lines to add
`std::function<void(const Spawn&)> on_spawn;` (or one callback per `Monster`).
This is the one gap on our side that we actually want filled.

**A2. Unknown `DataType` in `parse_map_data` is assumed to be a string — High.** (lines 196-200)
```cpp
default: {
    // All string-based map attributes in standard OTBM are string length prefixed
    auto skip = s.read_string();
    if (!skip) return std::unexpected(skip.error());
    break;
}
```
The comment is scoped to *map* attributes, and if that scope is exactly the set of
unknown ids this branch can ever receive, the assumption holds. The risk is that the
guard is a comment rather than a check: `DataType` itself spans u8/u16/u32 values as
well as strings (`RuneCharges` u16, `Duration` u32, `DecayingState` u8, `Charges`
u16), so if a non-string id ever reaches this branch the stream desyncs exactly as
in #2. If the map-attribute set is genuinely string-only, enumerating those ids in
the `case` list — or a compile-time width table — would turn the comment into
something the compiler enforces.

**A3. Unknown `DataType` in `parse_item` consumes nothing — High.** (line 341)
```cpp
default: break;
```
No bytes are skipped at all, so the attribute's value bytes are read as the next
attribute *type*. Strictly worse than A2: it desyncs without even attempting a skip.

**A4. `skip_node` counts unescaped marker bytes as structure — Medium.** (lines 128-142)
```cpp
auto b = s.read_byte();                 // unescapes FD-prefixed bytes
...
if (b.value() == 0xFE) { depth++; }
else if (b.value() == 0xFF) { if (depth == 0) return true; depth--; }
```
`read_byte()` unescapes, so a *data* byte whose value is `0xFE` (written `FD FE`) is
counted as a nested node start, and one equal to `0xFF` as a node end. Your own
`examples/generate-otbm.cpp` notes that structural markers are written raw and only
data is escaped — so a skipped node whose data contains `0xFE`/`0xFF` makes
`skip_node` return early and mis-parse the remainder. The format is inherently
ambiguous here and only the writer's convention resolves it, so the parser cannot
distinguish them by inspection — but it could at least refuse to skip a node it
cannot size, rather than returning a plausible-but-wrong result.

**A5. `on_item` fires twice per item — Medium.** (lines 282 and 345)
In `parse_tile_common`, `DataType::Item` reads just the u16 and calls
`on_item(t, i, nullptr)` immediately, with every other field still zero:
```cpp
i.type = it.value();
if (on_item) on_item(t, i, nullptr);
```
Then `parse_item` calls `on_item(*t, i, p)` again at the end with the complete
record. Every item is therefore delivered once partially-populated and once
complete, against the same tile. A consumer that collects items gets each one
twice; one that appends to a list inside `on_item` gets duplicates; one that reads
only `i.type` sees the first call, one that reads `i.text` sees only the second.
The early call looks vestigial — removing it, or splitting it behind a distinct
`on_item_type` callback, would make the contract single-fire.
