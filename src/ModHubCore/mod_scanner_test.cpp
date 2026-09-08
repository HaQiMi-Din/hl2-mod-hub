// HL2 Mod Hub - 模组解析核心单元测试
// 在临时目录构造模拟模组文件夹并验证解析逻辑。
// 编译运行：
//   g++ -std=c++17 -O2 -Wall -Wextra src/ModHubCore/mod_scanner_test.cpp
//       src/ModHubCore/modhub_core.cpp -o modhub_test && ./modhub_test

#include "modhub_core.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

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

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
