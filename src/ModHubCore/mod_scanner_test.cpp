// HL2 Mod Hub - 模组解析核心单元测试
// 在临时目录构造模拟模组文件夹并验证解析逻辑（含 .gma 容器解析）。
// 编译运行：
//   g++ -std=c++17 -O2 -Wall -Wextra src/ModHubCore/mod_scanner_test.cpp
//       src/ModHubCore/modhub_core.cpp src/ModHubCore/gma_parser.cpp
//       -o modhub_test && ./modhub_test

#include "modhub_core.h"
#include "gma_parser.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static void WriteFile(const fs::path& p, const std::string& content) {
    std::ofstream f(p);
    f << content;
}

int main() {
    const fs::path root = fs::temp_directory_path() / "modhub_test";
    fs::remove_all(root);

    // 1) 有效的 HL2 模组（不带引号的键值写法）
    fs::create_directories(root / "my_hl2_mod");
    WriteFile(root / "my_hl2_mod" / "gameinfo.txt",
              "\"GameInfo\"\n{\n\tFileSystem\n\t{\n\t\tSteamAppId 220\n\t\t"
              "SearchPaths { Game |gameinfo_path|. Game hl2 }\n\t}\n}\n");

    // 2) 有效的 GMod 基础模组（带引号写法）
    fs::create_directories(root / "gmod_base");
    WriteFile(root / "gmod_base" / "gameinfo.txt",
              "\"GameInfo\" { FileSystem { \"SteamAppId\" \"4000\" "
              "SearchPaths { Game |gameinfo_path|. Game garrysmod } } }");

    // 3) 有效的 CS:S 基础模组
    fs::create_directories(root / "css_base");
    WriteFile(root / "css_base" / "gameinfo.txt",
              "\"GameInfo\" { FileSystem { \"SteamAppId\" \"240\" "
              "SearchPaths { Game |gameinfo_path|. Game cstrike } } }");

    // 4) 无效目录：没有 gameinfo.txt
    fs::create_directories(root / "broken_mod");
    WriteFile(root / "broken_mod" / "readme.txt", "not a mod");

    // ---- HasGameInfo ----
    CHECK(modhub::HasGameInfo((root / "my_hl2_mod").string()));
    CHECK(!modhub::HasGameInfo((root / "broken_mod").string()));
    CHECK(!modhub::HasGameInfo((root / "not_exist_dir").string()));

    // ---- DetectEngine ----
    CHECK(modhub::DetectEngine("SearchPaths { Game hl2 }") == "hl2");
    CHECK(modhub::DetectEngine("Game garrysmod") == "gmod");
    CHECK(modhub::DetectEngine("Game cstrike") == "css");
    CHECK(modhub::DetectEngine("Team Fortress") == "tf2");
    CHECK(modhub::DetectEngine("nothing relevant here") == "unknown");

    // ---- ParseSteamAppId ----
    CHECK(modhub::ParseSteamAppId("SteamAppId\t\t220") == "220");
    CHECK(modhub::ParseSteamAppId("\"SteamAppId\" \"240\"") == "240");
    CHECK(modhub::ParseSteamAppId("no appid at all") == "");

    // ---- ScanMods ----
    const auto mods = modhub::ScanMods(root.string());
    CHECK(mods.size() == 4);  // 3 有效 + 1 无效

    std::string engines_found;
    for (const auto& m : mods) {
        engines_found += m.name + "=" + m.engine + ";";
        if (m.name == "my_hl2_mod") {
            CHECK(m.valid);
            CHECK(m.engine == "hl2");
            CHECK(modhub::BuildLaunchArgs(m) == "-game \"my_hl2_mod\"");
        } else if (m.name == "gmod_base") {
            CHECK(m.valid);
            CHECK(m.engine == "gmod");
        } else if (m.name == "css_base") {
            CHECK(m.valid);
            CHECK(m.engine == "css");
        } else if (m.name == "broken_mod") {
            CHECK(!m.valid);
            CHECK(!m.issues.empty());
        }
    }
    std::printf("scan -> %s\n", engines_found.c_str());

    fs::remove_all(root);

    // ============================================================
    //  GMA (.gma 容器) 解析测试
    // ============================================================
    const fs::path gmaRoot = fs::temp_directory_path() / "modhub_gma_test";
    fs::remove_all(gmaRoot);
    fs::create_directories(gmaRoot);

    // 按 GMA 规范手工构造一个测试用 .gma
    const fs::path gmaPath = gmaRoot / "cool_addon.gma";
    {
        std::ofstream f(gmaPath, std::ios::binary);
        auto w32 = [&](std::uint32_t v) {
            const char b[4] = {static_cast<char>(v),
                               static_cast<char>(v >> 8),
                               static_cast<char>(v >> 16),
                               static_cast<char>(v >> 24)};
            f.write(b, 4);
        };
        auto w64 = [&](std::uint64_t v) {
            char b[8];
            for (int i = 0; i < 8; ++i) b[i] = static_cast<char>(v >> (8 * i));
            f.write(b, 8);
        };
        auto wFixed = [&](const char* s, std::size_t n) {
            std::string buf(n, '\0');
            std::memcpy(buf.data(), s, std::min(n, std::strlen(s)));
            f.write(buf.data(), static_cast<std::streamsize>(n));
        };
        auto wStr = [&](const std::string& s) {
            w32(static_cast<std::uint32_t>(s.size()));
            f.write(s.data(), static_cast<std::streamsize>(s.size()));
        };

        f.write("GMAD", 4);
        w32(2);                                   // version
        w64(76561198000000000ULL);                // steamid
        w64(1700000000ULL);                       // timestamp
        w64(0ULL);                                // required content
        wFixed("Cool Addon", 256);
        wFixed("maps + lua demo", 65536);
        wFixed("modhub", 128);
        w32(1);                                   // addon version

        struct Item { std::string name; std::string data; };
        const std::vector<Item> items = {
            {"maps/test.bsp", "BSPDATA123"},
            {"lua/autorun/init.lua", "print('hello modhub')"},
        };

        std::vector<std::streamoff> offsetFields;
        for (const auto& it : items) {
            wStr(it.name);
            w64(it.data.size());
            w32(0x12345678u);                     // crc 占位
            offsetFields.push_back(f.tellp());
            w64(0ULL);                            // 偏移占位
        }
        w32(0);                                   // 条目表终止

        const std::streamoff dataStart = f.tellp();
        std::uint64_t cur = static_cast<std::uint64_t>(dataStart);
        for (std::size_t i = 0; i < items.size(); ++i) {
            f.seekp(offsetFields[i]);
            w64(cur);
            cur += items[i].data.size();
        }
        f.seekp(dataStart);
        for (const auto& it : items) {
            f.write(it.data.data(),
                    static_cast<std::streamsize>(it.data.size()));
        }
    }

    // ---- ParseGma ----
    modhub::GmaFile gma;
    std::string gmaErr;
    CHECK(modhub::ParseGma(gmaPath.string(), gma, gmaErr));
    CHECK(gmaErr.empty());
    CHECK(gma.header.version == 2);
    CHECK(gma.header.name == "Cool Addon");
    CHECK(gma.header.steam_id == 76561198000000000ULL);
    CHECK(gma.entries.size() == 2);
    CHECK(gma.entries[0].name == "maps/test.bsp");
    CHECK(gma.entries[0].size == 10);  // "BSPDATA123"
    CHECK(gma.entries[1].name == "lua/autorun/init.lua");
    CHECK(gma.entries[1].size == 21);  // "print('hello modhub')"

    // ---- 非法文件 ----
    const fs::path badGma = gmaRoot / "bad.gma";
    WriteFile(badGma, "this is not a gma file at all");
    modhub::GmaFile bad;
    std::string badErr;
    CHECK(!modhub::ParseGma(badGma.string(), bad, badErr));
    CHECK(!badErr.empty());

    // ---- 提取 ----
    const fs::path outDir = gmaRoot / "extracted";
    std::string extractErr;
    const std::size_t extracted = modhub::ExtractGmaAll(
        gmaPath.string(), gma, outDir.string(), extractErr);
    CHECK(extracted == 2);
    CHECK(fs::is_regular_file(outDir / "maps" / "test.bsp"));
    CHECK(fs::is_regular_file(outDir / "lua" / "autorun" / "init.lua"));
    CHECK(fs::file_size(outDir / "maps" / "test.bsp") == 10);

    // ---- 路径穿越防护 ----
    modhub::GmaEntry evil;
    evil.name = "../escape.txt";
    evil.size = 4;
    evil.offset = 0;
    std::string evilErr;
    CHECK(!modhub::ExtractGmaEntry(gmaPath.string(), gma, evil,
                                   outDir.string(), evilErr));

    // ---- ScanGmaFiles ----
    const auto gmaFiles = modhub::ScanGmaFiles(gmaRoot.string());
    CHECK(gmaFiles.size() == 2);  // cool_addon.gma + bad.gma
    for (const auto& g : gmaFiles) {
        CHECK(g.size() > 4 && g.substr(g.size() - 4) == ".gma");
    }

    fs::remove_all(gmaRoot);

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
