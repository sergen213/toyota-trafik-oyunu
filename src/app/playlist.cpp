// Music list editor (TfrmPlaylister, DFM TFRMPLAYLISTER): folder browser with *.mp3 files on the
// left, the radio playlist on the right. The native port saves to <save dir>/playlist.m3u and
// never writes into the original install.
#include <algorithm>
#include <fstream>
#include <sstream>

#include "app/app.hpp"
#include "core/log.hpp"

namespace tto {
namespace {
constexpr glm::vec2 kForm{144, 85};  // 736 x 597 client area centred on the 1024 x 768 canvas
constexpr glm::vec3 kFormBg{0.0f, 0.25f, 0.5f};  // Color = 8404992 (BGR 0x804000)
constexpr float kItem = 16;
struct Btn {
    const char* text;
    float x, y, w, h;
};
const Btn kBtns[] = {{"Yeni", 304, 560, 57, 25},  {"Yükle", 368, 560, 57, 25}, {"Ekle", 432, 560, 57, 25}, {"Çıkar", 496, 560, 57, 25},
                     {"Kaydet", 560, 560, 57, 25}, {"Kapat", 624, 560, 57, 25}, {"^", 688, 216, 41, 49},     {"v", 688, 272, 41, 49}};
const glm::vec4 kDirBox{8, 32, 289, 233}, kFileBox{8, 272, 289, 313}, kListBox{304, 8, 377, 545};
bool in(glm::vec2 p, glm::vec4 r) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.z && p.y < r.y + r.w; }
}  // namespace

void App::PlEditor::scan() {
    dirs = {".."};
    files.clear();
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        const std::string n = e.path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        if (e.is_directory(ec)) dirs.push_back(n);
        else if (to_lower_ascii(e.path().extension().string()) == ".mp3") files.push_back(n);
    }
    std::sort(dirs.begin() + 1, dirs.end());
    std::sort(files.begin(), files.end());
    dirSel = fileSel = -1;
    dirScroll = fileScroll = 0;
}

std::optional<std::filesystem::path> App::track_path(const std::string& entry) const {
    // Absolute native paths from the editor, or install-relative Windows paths (playlist\x.mp3).
    std::error_code ec;
    if (std::filesystem::path(entry).is_absolute() && std::filesystem::exists(entry, ec)) return std::filesystem::path(entry);
    if (auto p = fs_->find(entry)) return p;
    const auto slash = entry.find_last_of("\\/");  // C:\...\playlist\x.mp3 from another machine
    if (slash != std::string::npos)
        if (auto p = fs_->find("playlist\\" + entry.substr(slash + 1))) return p;
    return std::nullopt;
}

void App::playlist_load() {
    playlist_.clear();
    std::string text;
    const auto own = userDataPath_.parent_path() / "playlist.m3u";
    try {
        if (std::filesystem::exists(own)) text = read_text(own);
        else if (auto pl = fs_->find("playlist.m3u")) text = cp1254_to_utf8(read_text(*pl));
    } catch (const std::exception& e) {
        log_warn("playlist: {}", e.what());
    }
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty() && l[0] != '#') playlist_.push_back(l);
    }
}

bool App::playlist_save() {
    const auto own = userDataPath_.parent_path() / "playlist.m3u";
    std::ofstream f(own, std::ios::binary);
    for (const auto& e : pl_.list) f << e << "\r\n";
    if (!f) return false;
    playlist_ = pl_.list;
    radioTrack_ = 0;
    return true;
}

void App::playlist_open() {
    pl_.list = playlist_;
    if (pl_.dir.empty()) {
        const char* h = SDL_GetUserFolder(SDL_FOLDER_HOME);
        pl_.dir = h ? std::filesystem::path(reinterpret_cast<const char8_t*>(h)) : std::filesystem::current_path();
        if (auto m = pl_.dir / "Music"; std::filesystem::is_directory(m)) pl_.dir = m;
    }
    pl_.scan();
    pl_.listSel = pl_.list.empty() ? -1 : 0;
    goto_screen(Screen::Playlist);
}

void App::playlist_wheel(float dy) {
    const glm::vec2 p = mouse_ - kForm;
    auto scroll = [&](float& s, std::size_t n, float h) { s = std::clamp(s - dy * 3, 0.0f, std::max(0.0f, static_cast<float>(n) - h / kItem)); };
    if (in(p, kDirBox)) scroll(pl_.dirScroll, pl_.dirs.size(), kDirBox.w);
    else if (in(p, kFileBox)) scroll(pl_.fileScroll, pl_.files.size(), kFileBox.w);
    else if (in(p, kListBox)) scroll(pl_.listScroll, pl_.list.size(), kListBox.w);
}

void App::playlist_click(glm::vec2 abs, bool) {
    const glm::vec2 p = abs - kForm;
    const bool dbl = clock_ - pl_.lastClick < 0.4 && glm::length(abs - pl_.lastPos) < 4;
    pl_.lastClick = clock_;
    pl_.lastPos = abs;
    auto row = [&](glm::vec4 box, float scroll) { return static_cast<int>((p.y - box.y - 2) / kItem + scroll); };
    auto add = [&](int i) {
        if (i < 0 || i >= static_cast<int>(pl_.files.size())) return;
        pl_.list.push_back((pl_.dir / pl_.files[static_cast<std::size_t>(i)]).string());
        pl_.listSel = static_cast<int>(pl_.list.size()) - 1;
    };
    if (in(p, kDirBox)) {
        const int i = row(kDirBox, pl_.dirScroll);
        if (i >= 0 && i < static_cast<int>(pl_.dirs.size())) {
            if (dbl && i == pl_.dirSel) {  // TDirectoryListBox: double click opens
                pl_.dir = i == 0 ? pl_.dir.parent_path() : pl_.dir / pl_.dirs[static_cast<std::size_t>(i)];
                pl_.scan();
            } else {
                pl_.dirSel = i;
            }
        }
        return;
    }
    if (in(p, kFileBox)) {
        const int i = row(kFileBox, pl_.fileScroll);
        if (dbl && i == pl_.fileSel) add(i);  // flb1DblClick: add the file with its full path
        else if (i >= 0 && i < static_cast<int>(pl_.files.size())) pl_.fileSel = i;
        return;
    }
    if (in(p, kListBox)) {
        const int i = row(kListBox, pl_.listScroll);
        if (i >= 0 && i < static_cast<int>(pl_.list.size())) pl_.listSel = i;
        return;
    }
    for (int b = 0; b < 8; ++b) {
        const Btn& k = kBtns[b];
        if (!in(p, {k.x, k.y, k.w, k.h})) continue;
        const int sel = pl_.listSel;
        switch (b) {
            case 0: pl_.list.clear(); pl_.listSel = -1; break;  // Yeni
            case 1: playlist_load(); pl_.list = playlist_; pl_.listSel = -1; break;  // Yükle
            case 2: add(pl_.fileSel); break;  // Ekle
            case 3:  // Çıkar
                if (sel >= 0 && sel < static_cast<int>(pl_.list.size())) {
                    pl_.list.erase(pl_.list.begin() + sel);
                    pl_.listSel = std::min(sel, static_cast<int>(pl_.list.size()) - 1);
                }
                break;
            case 4:  // Kaydet (only when the list is not empty, as SpeedButton4Click)
                if (!pl_.list.empty())
                    message_box(playlist_save() ? "Müzik listesi kaydedildi." : "Müzik listesi kaydedilemedi.", "Bilgi");
                break;
            case 5: goto_screen(Screen::Main); break;  // Kapat
            case 6:  // move up (SpeedButton8Click)
                if (sel > 0) {
                    std::swap(pl_.list[static_cast<std::size_t>(sel)], pl_.list[static_cast<std::size_t>(sel - 1)]);
                    pl_.listSel = sel - 1;
                }
                break;
            case 7:  // move down (SpeedButton9Click)
                if (sel >= 0 && sel + 1 < static_cast<int>(pl_.list.size())) {
                    std::swap(pl_.list[static_cast<std::size_t>(sel)], pl_.list[static_cast<std::size_t>(sel + 1)]);
                    pl_.listSel = sel + 1;
                }
                break;
        }
        return;
    }
}

void App::playlist_draw() {
    using F = TextRenderer::Font;
    const glm::vec2 o = kForm;
    r_->quad(o.x - 3, o.y - 22, 742, 622, nullptr, {0.3f, 0.3f, 0.3f, 1});
    r_->quad(o.x - 2, o.y - 21, 740, 20, nullptr, {0.04f, 0.14f, 0.42f, 1});
    text_.draw("Müzik Listesi Hazırlama", o.x + 4, o.y - 19, F::UiBold, {1, 1, 1, 1});
    r_->quad(o.x, o.y, 736, 597, nullptr, glm::vec4(kFormBg, 1));
    auto box = [&](glm::vec4 b, const std::vector<std::string>& items, int sel, float scroll, bool paths) {
        r_->quad(o.x + b.x, o.y + b.y, b.z, b.w, nullptr, {0.5f, 0.5f, 0.5f, 1});
        r_->quad(o.x + b.x + 1, o.y + b.y + 1, b.z - 2, b.w - 2, nullptr, {1, 1, 1, 1});
        const int first = static_cast<int>(scroll), rows = static_cast<int>((b.w - 4) / kItem);
        for (int i = first; i < static_cast<int>(items.size()) && i < first + rows; ++i) {
            const float y = o.y + b.y + 2 + kItem * static_cast<float>(i - first);
            if (i == sel) r_->quad(o.x + b.x + 2, y, b.z - 4, kItem, nullptr, {0, 0, 0.5f, 1});
            std::string t = items[static_cast<std::size_t>(i)];
            if (paths) {
                while (text_.width(t, F::Ui) > b.z - 10 && t.size() > 4) t.erase(0, 1);
            }
            text_.draw(t, o.x + b.x + 4, y, F::Ui, i == sel ? glm::vec4(1) : glm::vec4(0, 0, 0, 1));
        }
    };
    std::string cur = pl_.dir.string();
    while (text_.width(cur, F::Ui) > 280 && cur.size() > 4) cur.erase(0, 1);
    r_->quad(o.x + 8, o.y + 8, 289, 19, nullptr, {1, 1, 1, 1});  // dcb1 (drive combo): current folder
    text_.draw(cur, o.x + 11, o.y + 9, F::Ui, {0, 0, 0, 1});
    std::vector<std::string> dirs = pl_.dirs;
    for (std::size_t i = 1; i < dirs.size(); ++i) dirs[i] = "[" + dirs[i] + "]";
    box(kDirBox, dirs, pl_.dirSel, pl_.dirScroll, false);
    box(kFileBox, pl_.files, pl_.fileSel, pl_.fileScroll, false);
    box(kListBox, pl_.list, pl_.listSel, pl_.listScroll, true);
    for (const auto& b : kBtns) {
        const bool hover = in(mouse_ - o, {b.x, b.y, b.w, b.h});
        r_->quad(o.x + b.x, o.y + b.y, b.w, b.h, nullptr, {0.4f, 0.4f, 0.4f, 1});
        r_->quad(o.x + b.x + 1, o.y + b.y + 1, b.w - 2, b.h - 2, nullptr, hover ? glm::vec4(1, 1, 0.85f, 1) : glm::vec4(0.9f, 0.9f, 0.88f, 1));
        text_.draw(b.text, o.x + b.x + b.w / 2, o.y + b.y + b.h / 2 - 8, F::Ui, {0, 0, 0, 1}, 2);
    }
}

}  // namespace tto
