#include "formats/dfm.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "core/fs.hpp"

namespace tto {
namespace {

enum : std::uint8_t {
    vaNull, vaList, vaInt8, vaInt16, vaInt32, vaExtended, vaString, vaIdent, vaFalse, vaTrue, vaBinary, vaSet,
    vaLString, vaNil, vaCollection, vaSingle, vaCurrency, vaDate, vaWString, vaInt64, vaUTF8String, vaDouble
};

struct Reader {
    std::span<const std::uint8_t> d;
    std::size_t p = 0;
    void need(std::size_t n) const {
        if (n > d.size() - p) throw std::runtime_error("DFM: truncated at " + std::to_string(p));
    }
    std::uint8_t u8() { need(1); return d[p++]; }
    std::uint8_t peek() const { need(1); return d[p]; }
    std::uint64_t le(int n) {
        need(static_cast<std::size_t>(n));
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(d[p + static_cast<std::size_t>(i)]) << (8 * i);
        p += static_cast<std::size_t>(n);
        return v;
    }
    std::string raw(std::size_t n) {
        need(n);
        std::string s(reinterpret_cast<const char*>(d.data() + p), n);
        p += n;
        return s;
    }
    std::string sstr() { return raw(u8()); }
};

void props(Reader& r, std::map<std::string, DfmValue>* out, int depth);

DfmValue value(Reader& r, int depth) {
    if (depth > 64) throw std::runtime_error("DFM: nesting too deep");
    const std::uint8_t t = r.u8();
    switch (t) {
        case vaNull: case vaNil: return {std::int64_t{0}};
        case vaList:
            while (r.peek() != 0) (void)value(r, depth + 1);
            r.u8();
            return {std::int64_t{0}};
        case vaInt8: return {static_cast<std::int64_t>(static_cast<std::int8_t>(r.le(1)))};
        case vaInt16: return {static_cast<std::int64_t>(static_cast<std::int16_t>(r.le(2)))};
        case vaInt32: return {static_cast<std::int64_t>(static_cast<std::int32_t>(r.le(4)))};
        case vaInt64: return {static_cast<std::int64_t>(r.le(8))};
        case vaExtended: {
            const std::uint64_t mant = r.le(8);
            const auto se = static_cast<std::uint16_t>(r.le(2));
            const int exp = se & 0x7FFF;
            double v = (exp == 0 && mant == 0) ? 0.0 : std::ldexp(static_cast<double>(mant), exp - 16383 - 63);
            return {(se & 0x8000) ? -v : v};
        }
        case vaSingle: return {static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(r.le(4))))};
        case vaDouble: case vaDate: return {std::bit_cast<double>(r.le(8))};
        case vaCurrency: return {static_cast<double>(static_cast<std::int64_t>(r.le(8))) / 10000.0};
        case vaString: return {cp1254_to_utf8(r.sstr())};
        case vaIdent: return {r.sstr()};
        case vaFalse: return {std::int64_t{0}};
        case vaTrue: return {std::int64_t{1}};
        case vaLString: return {cp1254_to_utf8(r.raw(static_cast<std::size_t>(r.le(4))))};
        case vaUTF8String: return {r.raw(static_cast<std::size_t>(r.le(4)))};
        case vaWString: {
            const auto n = static_cast<std::size_t>(r.le(4));
            std::string s;
            for (std::size_t i = 0; i < n; ++i) {
                const auto c = static_cast<std::uint16_t>(r.le(2));
                s += c < 0x80 ? static_cast<char>(c) : '?';  // shortcut: BMP-only, unused by this game
            }
            return {s};
        }
        case vaBinary: {
            const auto n = static_cast<std::size_t>(r.le(4));
            r.need(n);
            std::vector<std::uint8_t> b(r.d.begin() + static_cast<std::ptrdiff_t>(r.p), r.d.begin() + static_cast<std::ptrdiff_t>(r.p + n));
            r.p += n;
            return {std::move(b)};
        }
        case vaSet: {
            std::string s;
            for (std::string e = r.sstr(); !e.empty(); e = r.sstr()) s += (s.empty() ? "" : ",") + e;
            return {s};
        }
        case vaCollection:
            while (r.peek() != 0) {
                if (const auto k = r.peek(); k == vaInt8 || k == vaInt16 || k == vaInt32) (void)value(r, depth + 1);
                if (r.u8() != vaList) throw std::runtime_error("DFM: bad collection item");
                props(r, nullptr, depth + 1);
            }
            r.u8();
            return {std::int64_t{0}};
        default: throw std::runtime_error("DFM: unknown value type " + std::to_string(t) + " at " + std::to_string(r.p - 1));
    }
}

void props(Reader& r, std::map<std::string, DfmValue>* out, int depth) {
    for (std::string n = r.sstr(); !n.empty(); n = r.sstr()) {
        DfmValue v = value(r, depth);
        if (out) (*out)[n] = std::move(v);
    }
}

DfmObject component(Reader& r, int depth) {
    if (depth > 64) throw std::runtime_error("DFM: nesting too deep");
    if ((r.peek() & 0xF0) == 0xF0) {
        const std::uint8_t flags = r.u8();
        if (flags & 2) (void)value(r, depth);
    }
    DfmObject o;
    o.cls = r.sstr();
    o.name = r.sstr();
    props(r, &o.props, depth);
    while (r.peek() != 0) o.children.push_back(component(r, depth + 1));
    r.u8();
    return o;
}

}  // namespace

std::optional<std::int64_t> DfmValue::integer() const {
    if (auto* i = std::get_if<std::int64_t>(&v)) return *i;
    return std::nullopt;
}

const DfmObject* DfmObject::find(std::string_view n) const {
    if (name == n) return this;
    for (const auto& c : children)
        if (auto* f = c.find(n)) return f;
    return nullptr;
}

int DfmObject::num(std::string_view prop, int fallback) const {
    auto it = props.find(std::string(prop));
    if (it == props.end()) return fallback;
    return static_cast<int>(it->second.integer().value_or(fallback));
}

std::string DfmObject::text(std::string_view prop) const {
    auto it = props.find(std::string(prop));
    if (it == props.end() || !it->second.str()) return {};
    return *it->second.str();
}

std::span<const std::uint8_t> DfmObject::image(std::string_view prop) const {
    auto it = props.find(std::string(prop));
    if (it == props.end() || !it->second.bin()) return {};
    std::span<const std::uint8_t> b = *it->second.bin();
    // Glyph.Data: int32 size + BMP. Picture.Data: ShortString class name, then for
    // TBitmap int32 size + BMP, for TJPEGImage int32 size + JPEG.
    if (b.size() > 6 && b[4] == 'B' && b[5] == 'M') return b.subspan(4);
    if (!b.empty() && b[0] < b.size()) {
        auto rest = b.subspan(1 + b[0]);
        if (rest.size() > 6 && ((rest[4] == 'B' && rest[5] == 'M') || (rest[4] == 0xFF && rest[5] == 0xD8))) return rest.subspan(4);
    }
    return {};
}

DfmObject parse_dfm(std::span<const std::uint8_t> data) {
    if (data.size() < 4 || std::memcmp(data.data(), "TPF0", 4) != 0) throw std::runtime_error("DFM: missing TPF0 signature");
    Reader r{data, 4};
    return component(r, 0);
}

std::map<std::string, std::vector<std::uint8_t>> pe_rcdata(std::span<const std::uint8_t> d) {
    auto rd = [&](std::size_t o, int n) -> std::uint32_t {
        if (o > d.size() || static_cast<std::size_t>(n) > d.size() - o) throw std::runtime_error("PE: truncated");
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<std::uint32_t>(d[o + static_cast<std::size_t>(i)]) << (8 * i);
        return v;
    };
    if (rd(0, 2) != 0x5A4D) throw std::runtime_error("PE: no MZ header");
    const std::size_t pe = rd(0x3C, 4);
    if (rd(pe, 4) != 0x4550) throw std::runtime_error("PE: no PE signature");
    const std::size_t nsec = rd(pe + 6, 2), opt = pe + 24, optSize = rd(pe + 20, 2);
    if (rd(opt, 2) != 0x10B) throw std::runtime_error("PE: not PE32");
    const std::uint32_t resRva = rd(opt + 96 + 2 * 8, 4);
    struct Sec { std::uint32_t va, vs, raw, rs; };
    std::vector<Sec> secs;
    for (std::size_t i = 0, s = opt + optSize; i < nsec; ++i, s += 40) secs.push_back({rd(s + 12, 4), rd(s + 8, 4), rd(s + 20, 4), rd(s + 16, 4)});
    auto off = [&](std::uint32_t rva) -> std::size_t {
        for (const auto& s : secs)
            if (rva >= s.va && rva < s.va + std::max(s.vs, s.rs)) return s.raw + (rva - s.va);
        throw std::runtime_error("PE: RVA outside sections");
    };
    std::map<std::string, std::vector<std::uint8_t>> out;
    if (!resRva) return out;
    const std::size_t root = off(resRva);
    auto entries = [&](std::size_t dir) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> e;
        const std::size_t n = rd(dir + 12, 2) + rd(dir + 14, 2);
        if (n > 4096) throw std::runtime_error("PE: bad resource directory");
        for (std::size_t i = 0; i < n; ++i) e.push_back({rd(dir + 16 + i * 8, 4), rd(dir + 20 + i * 8, 4)});
        return e;
    };
    for (auto [type, sub] : entries(root)) {
        if (type != 10 || !(sub & 0x80000000u)) continue;  // RT_RCDATA
        for (auto [nameId, sub2] : entries(root + (sub & 0x7FFFFFFF))) {
            std::string name;
            if (nameId & 0x80000000u) {
                const std::size_t s = root + (nameId & 0x7FFFFFFF);
                const std::size_t len = rd(s, 2);
                for (std::size_t i = 0; i < len; ++i) name += static_cast<char>(rd(s + 2 + 2 * i, 2) & 0x7F);
            } else {
                name = std::to_string(nameId);
            }
            if (!(sub2 & 0x80000000u)) continue;
            const auto langs = entries(root + (sub2 & 0x7FFFFFFF));
            if (langs.empty()) continue;
            const std::size_t de = root + langs[0].second;
            const std::uint32_t rva = rd(de, 4), size = rd(de + 4, 4);
            const std::size_t o = off(rva);
            if (o > d.size() || size > d.size() - o) throw std::runtime_error("PE: resource outside file");
            out[name].assign(d.begin() + static_cast<std::ptrdiff_t>(o), d.begin() + static_cast<std::ptrdiff_t>(o + size));
        }
    }
    return out;
}

}  // namespace tto
