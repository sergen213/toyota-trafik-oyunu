#include "core/fs.hpp"

#include <array>
#include <fstream>
#include <stdexcept>

namespace tto {

std::vector<std::uint8_t> read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    f.seekg(0, std::ios::end);
    const auto n = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::uint8_t> d(n);
    if (n && !f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(n)))
        throw std::runtime_error("cannot read " + p.string());
    return d;
}

std::string read_text(const std::filesystem::path& p) {
    auto d = read_file(p);
    return {d.begin(), d.end()};
}

namespace {
// CP1254 0x80..0xFF -> Unicode. 0 marks undefined positions.
constexpr std::array<char32_t, 128> kCp1254 = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x011E, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x0130, 0x015E, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x011F, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x0131, 0x015F, 0x00FF,
};

void put_utf8(std::string& o, char32_t c) {
    if (c < 0x80) {
        o += static_cast<char>(c);
    } else if (c < 0x800) {
        o += static_cast<char>(0xC0 | (c >> 6));
        o += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        o += static_cast<char>(0xE0 | (c >> 12));
        o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (c & 0x3F));
    }
}
}  // namespace

std::string cp1254_to_utf8(std::string_view s) {
    std::string o;
    o.reserve(s.size() + s.size() / 4);
    for (unsigned char ch : s) {
        if (ch < 0x80) {
            o += static_cast<char>(ch);
        } else {
            char32_t c = kCp1254[ch - 0x80];
            put_utf8(o, c ? c : U'?');
        }
    }
    return o;
}

std::string utf8_to_cp1254(std::string_view s) {
    std::string o;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        char32_t c;
        int n;
        if (b < 0x80) { c = b; n = 1; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < s.size()) { c = (b & 0x1F) << 6 | (s[i + 1] & 0x3F); n = 2; }
        else if ((b & 0xF0) == 0xE0 && i + 2 < s.size()) { c = (b & 0x0F) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F); n = 3; }
        else { c = U'?'; n = 1; }
        i += static_cast<std::size_t>(n);
        if (c < 0x80) { o += static_cast<char>(c); continue; }
        char out = '?';
        for (std::size_t k = 0; k < kCp1254.size(); ++k)
            if (kCp1254[k] == c) { out = static_cast<char>(0x80 + k); break; }
        o += out;
    }
    return o;
}

std::string to_lower_ascii(std::string_view s) {
    std::string o(s);
    for (auto& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

GameFs::GameFs(std::filesystem::path root) : root_(std::move(root)) {}

std::optional<std::filesystem::path> GameFs::find(std::string_view winPath) const {
    namespace fs = std::filesystem;
    fs::path cur = root_;
    std::size_t i = 0;
    while (i <= winPath.size()) {
        std::size_t j = winPath.find_first_of("\\/", i);
        if (j == std::string_view::npos) j = winPath.size();
        std::string_view part = winPath.substr(i, j - i);
        i = j + 1;
        if (part.empty() || part == ".") continue;
        fs::path exact = cur / std::string(part);
        std::error_code ec;
        if (fs::exists(exact, ec)) { cur = exact; continue; }
        const std::string want = to_lower_ascii(part);
        bool found = false;
        for (const auto& e : fs::directory_iterator(cur, ec)) {
            if (to_lower_ascii(e.path().filename().string()) == want) {
                cur = e.path();
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return cur;
}

std::filesystem::path GameFs::need(std::string_view winPath) const {
    if (auto p = find(winPath)) return *p;
    throw std::runtime_error("game file not found: " + std::string(winPath) + " (under " + root_.string() + ")");
}

}  // namespace tto
