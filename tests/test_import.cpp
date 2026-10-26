// Import of the game folder from synthetic setup media (folder and archive).
#include <gtest/gtest.h>

#include <archive.h>
#include <archive_entry.h>

#include <fstream>
#include <string>

#include "data/import.hpp"

namespace fs = std::filesystem;

namespace {
fs::path scratch(const char* name) {
    const fs::path p = fs::temp_directory_path() / "tto_import_test" / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string get(const fs::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void add(archive* a, const std::string& name, const std::string& data) {
    archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, name.c_str());
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
    archive_write_header(a, e);
    archive_write_data(a, data.data(), data.size());
    archive_entry_free(e);
}
}  // namespace

TEST(Import, SetupFolderPrefersV2Tree) {
    const fs::path cd = scratch("cd"), out = scratch("out1") / "game";
    put(cd / "Kurulum1/program files/ToyotaSA/Toyota Trafik Oyunu/TrafikOyunu.exe", "v1");
    put(cd / "Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2/TrafikOyunu.exe", "v2");
    put(cd / "Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2/Data/SoruBank.dat", "q");
    put(cd / "kur.exe", "installer");
    tto::import_game(cd, out);
    EXPECT_EQ(get(out / "TrafikOyunu.exe"), "v2");
    EXPECT_EQ(get(out / "Data/SoruBank.dat"), "q");
    EXPECT_FALSE(fs::exists(out / "kur.exe"));

    // Re-import from a single Kurulum folder replaces the previous import entirely.
    tto::import_game(cd / "Kurulum1", out);
    EXPECT_EQ(get(out / "TrafikOyunu.exe"), "v1");
    EXPECT_FALSE(fs::exists(out / "Data"));
}

TEST(Import, ArchiveExtractsOnlyTheGameTree) {
    const fs::path dir = scratch("arc"), file = dir / "setup.tar", out = dir / "game";
    archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, file.string().c_str());
    add(a, "CD/Kurulum1/program files/ToyotaSA/Toyota Trafik Oyunu/TrafikOyunu.exe", "v1");
    add(a, "CD/Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2/TrafikOyunu.exe", "v2");
    add(a, "CD/Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2/Bolumler/a.3DS", "mesh");
    add(a, "CD/Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2/Data/../../../../../../evil", "x");
    archive_write_close(a);
    archive_write_free(a);

    tto::import_game(file, out);
    EXPECT_EQ(get(out / "TrafikOyunu.exe"), "v2");
    EXPECT_EQ(get(out / "Bolumler/a.3DS"), "mesh");
    EXPECT_FALSE(fs::exists(dir / "evil"));
    EXPECT_FALSE(fs::exists(out / "evil"));
}

TEST(Import, MediaWithoutGameTreeFailsAndKeepsPreviousImport) {
    const fs::path empty = scratch("empty"), out = scratch("out3") / "game";
    put(out / "TrafikOyunu.exe", "old");
    EXPECT_THROW(tto::import_game(empty, out), std::runtime_error);
    EXPECT_EQ(get(out / "TrafikOyunu.exe"), "old");
}
