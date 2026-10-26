#include "data/import.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/fs.hpp"

namespace tto {
namespace fs = std::filesystem;
namespace {

// "…/program files/ToyotaSA/Toyota Trafik Oyunu[ v2]" prefix of a setup-media path, if any.
std::optional<fs::path> game_root(const fs::path& rel) {
    const std::vector<fs::path> c(rel.begin(), rel.end());
    for (size_t i = 0; i + 2 < c.size(); ++i)
        if (to_lower_ascii(c[i].string()) == "program files" && to_lower_ascii(c[i + 1].string()) == "toyotasa" &&
            to_lower_ascii(c[i + 2].string()).starts_with("toyota trafik oyunu")) {
            fs::path r;
            for (size_t j = 0; j <= i + 2; ++j) r /= c[j];
            return r;
        }
    return std::nullopt;
}

// v2 (Kurulum2, "Toyota Trafik Oyunu v2") wins over v1.
fs::path pick(const std::set<fs::path>& roots, const fs::path& source) {
    if (roots.empty()) throw std::runtime_error("no 'program files/ToyotaSA/Toyota Trafik Oyunu' folder in " + source.string());
    for (const auto& r : roots)
        if (to_lower_ascii(r.filename().string()).ends_with("v2")) return r;
    return *roots.begin();
}

using Archive = std::unique_ptr<archive, decltype(&archive_read_free)>;

Archive open_archive(const fs::path& file) {
    Archive a(archive_read_new(), &archive_read_free);
    archive_read_support_format_all(a.get());
    archive_read_support_filter_all(a.get());
#ifdef _WIN32
    const int rc = archive_read_open_filename_w(a.get(), file.c_str(), 1 << 16);
#else
    const int rc = archive_read_open_filename(a.get(), file.c_str(), 1 << 16);
#endif
    if (rc != ARCHIVE_OK) throw std::runtime_error(file.string() + ": " + archive_error_string(a.get()));
    return a;
}

fs::path entry_path(archive_entry* e) {
    const char* p = archive_entry_pathname_utf8(e);
    if (!p) p = archive_entry_pathname(e);
    return fs::path(reinterpret_cast<const char8_t*>(p ? p : "")).relative_path();
}

void extract_archive(const fs::path& file, const fs::path& out) {
    std::set<fs::path> roots;
    {
        Archive a = open_archive(file);
        for (archive_entry* e; archive_read_next_header(a.get(), &e) == ARCHIVE_OK;)
            if (auto r = game_root(entry_path(e))) roots.insert(*r);
    }
    const fs::path root = pick(roots, file);
    Archive a = open_archive(file);
    std::vector<char> buf(1 << 16);
    for (archive_entry* e; archive_read_next_header(a.get(), &e) == ARCHIVE_OK;) {
        if (archive_entry_filetype(e) != AE_IFREG) continue;
        const fs::path rel = entry_path(e).lexically_normal().lexically_relative(root);
        if (rel.empty() || *rel.begin() == ".." || rel.is_absolute()) continue;  // outside the game tree (or a traversal)
        const fs::path target = out / rel;
        fs::create_directories(target.parent_path());
        std::ofstream os(target, std::ios::binary);
        for (la_ssize_t n; (n = archive_read_data(a.get(), buf.data(), buf.size())) != 0;) {
            if (n < 0) throw std::runtime_error(file.string() + ": " + archive_error_string(a.get()));
            os.write(buf.data(), n);
        }
        if (!os) throw std::runtime_error("cannot write " + target.string());
    }
}

void copy_folder(const fs::path& dir, const fs::path& out) {
    std::set<fs::path> roots;
    // The tree sits at most at <CD>/Kurulum2/program files/ToyotaSA/<game>; don't walk a whole disk.
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied); it != fs::end(it); ++it) {
        if (it.depth() >= 3) it.disable_recursion_pending();
        if (it->is_directory())
            if (auto r = game_root(it->path().lexically_relative(dir))) roots.insert(*r);
    }
    fs::copy(dir / pick(roots, dir), out, fs::copy_options::recursive);
}

}  // namespace

void import_game(const fs::path& source, const fs::path& dest) {
    const fs::path tmp = fs::path(dest).concat(".import");
    fs::remove_all(tmp);
    fs::create_directories(tmp.parent_path());
    if (fs::is_directory(source)) copy_folder(source, tmp);
    else extract_archive(source, tmp);
    fs::remove_all(dest);
    fs::rename(tmp, dest);
}

}  // namespace tto
