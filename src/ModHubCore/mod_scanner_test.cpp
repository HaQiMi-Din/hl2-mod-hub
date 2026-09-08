// HL2 Mod Hub - 模组解析核心单元测试
// 在临时目录构造模拟模组文件夹并验证解析逻辑（含 .gma 容器解析与 Lua VM）。
// 编译运行：
//   g++ -std=c++17 -O2 -Wall -Wextra src/ModHubCore/mod_scanner_test.cpp
//       src/ModHubCore/modhub_core.cpp src/ModHubCore/gma_parser.cpp
//       src/ModHubCore/lua_vm.cpp [lua/*.o 由 gcc 编译] -lm
//       -o modhub_test && ./modhub_test

#include "modhub_core.h"
#include "gma_parser.h"
#include "lua_vm.h"

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

// CRC32 (IEEE 802.3, zlib 兼容) —— 测试构造 GMA 时用于计算真实校验和
static std::uint32_t Crc32(const std::string& data) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < data.size(); ++i) {
        c = table[(c ^ static_cast<unsigned char>(data[i])) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
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

    // =======
    //  GMA (.gma 容器) 解析测试
    // =======
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
        std::vector<std::streamoff> crcFields;
        for (const auto& it : items) {
            wStr(it.name);
            w64(it.data.size());
            crcFields.push_back(f.tellp());
            w32(0u);                              // crc 占位
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
        // 回填真实 CRC32
        for (std::size_t i = 0; i < items.size(); ++i) {
            f.seekp(crcFields[i]);
            w32(Crc32(items[i].data));
        }
    }

    // ---- ParseGma（经典 v2 布局）----
    modhub::GmaFile gma;
    std::string gmaErr;
    CHECK(modhub::ParseGma(gmaPath.string(), gma, gmaErr));
    CHECK(gmaErr.empty());
    CHECK(gma.format == "v2-classic");
    CHECK(gma.crc_ok);  // 全部条目 CRC32 校验通过
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

    // ---- 现代 v3 布局（复刻实测 workshop .gma 结构）----
    const fs::path gmaV3Path = gmaRoot / "workshop_addon.gma";
    {
        std::ofstream f(gmaV3Path, std::ios::binary);
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

        f.write("GMAD", 4);
        f.put(3);                      // 单字节版本（之后直接跟 steamid，无填充）
        w64(0ULL);                     // steamid（实测文件为 0）
        w64(1741071119ULL);            // timestamp
        f.put(0);                      // required content：空串
        const char* name = "[Test] Cool Addon (PM, NPC)";
        f.write(name, std::strlen(name));
        f.put(0);
        const char* desc = "{\"description\":\"demo\",\"type\":\"servercontent\"}";
        f.write(desc, std::strlen(desc));
        f.put(0);
        const char* author = "modhub";
        f.write(author, std::strlen(author));
        f.put(0);
        w32(1);                        // addon version

        struct Item { std::string name; std::string data; };
        const std::vector<Item> items = {
            {"lua/autorun/init.lua",
             "player_manager.AddValidModel(\"Test\", \"models/t.mdl\")\n"},
            {"models/t.mdl", std::string("\x01\x02\x03\x04\x05\x06", 6)},
            {"materials/t.vmt", "test material"},
        };
        std::vector<std::streamoff> crcFields;
        std::uint32_t seq = 0;
        for (const auto& it : items) {
            ++seq;
            w32(seq);                  // 条目序号（1 起递增）
            f.write(it.name.data(),
                    static_cast<std::streamsize>(it.name.size()));
            f.put(0);                  // 路径空终止
            w64(it.data.size());
            crcFields.push_back(f.tellp());
            w32(0u);                   // crc 占位
        }
        w32(0);                        // 表终止
        for (const auto& it : items) {
            f.write(it.data.data(),
                    static_cast<std::streamsize>(it.data.size()));
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            f.seekp(crcFields[i]);
            w32(Crc32(items[i].data));
        }
    }

    modhub::GmaFile gmaV3;
    std::string v3err;
    CHECK(modhub::ParseGma(gmaV3Path.string(), gmaV3, v3err));
    CHECK(v3err.empty());
    CHECK(gmaV3.format == "v3-modern");
    CHECK(gmaV3.crc_ok);
    CHECK(gmaV3.header.version == 3);
    CHECK(gmaV3.header.name == "[Test] Cool Addon (PM, NPC)");
    CHECK(gmaV3.header.steam_id == 0);
    CHECK(gmaV3.entries.size() == 3);
    CHECK(gmaV3.entries[0].name == "lua/autorun/init.lua");
    CHECK(gmaV3.entries[1].name == "models/t.mdl");
    CHECK(gmaV3.entries[1].size == 6);

    const fs::path v3out = gmaRoot / "v3extract";
    std::string v3xerr;
    CHECK(modhub::ExtractGmaAll(gmaV3Path.string(), gmaV3,
                                v3out.string(), v3xerr) == 3);
    CHECK(fs::file_size(v3out / "models" / "t.mdl") == 6);
    CHECK(fs::is_regular_file(v3out / "lua" / "autorun" / "init.lua"));

    fs::remove_all(gmaRoot);

    // =======
    //  Lua VM 测试
    // =======
    const fs::path luaRoot = fs::temp_directory_path() / "modhub_lua_test";
    fs::remove_all(luaRoot);
    fs::create_directories(luaRoot / "lua" / "autorun");

    {
        modhub::LuaVm vm;
        vm.SetRootDir(luaRoot.string());
        std::string lerr;

        // 基础执行 + 全局读写
        CHECK(vm.RunString("answer = 21 * 2", "t1", lerr));
        CHECK(lerr.empty());
        CHECK(vm.GetGlobalInt("answer") == 42);

        // GLua shim: Color
        CHECK(vm.RunString("c = Color(255, 0, 128, 200)", "t2", lerr));
        CHECK(lerr.empty());
        CHECK(vm.GetGlobalFieldInt("c", "r") == 255);
        CHECK(vm.GetGlobalFieldInt("c", "b") == 128);

        // GLua shim: print / MsgN / Msg 不报错
        CHECK(vm.RunString(
            "print('hello from lua vm'); MsgN('msg ok'); Msg('no newline')",
            "t3", lerr));

        // 沙箱：os.execute / io / loadfile 必须不可用
        std::string serr;
        CHECK(!vm.RunString("os.execute('echo hi')", "s1", serr));
        CHECK(!serr.empty());
        serr.clear();
        CHECK(!vm.RunString("io.open('/etc/passwd')", "s2", serr));
        CHECK(!serr.empty());
        serr.clear();
        CHECK(!vm.RunString("loadfile('/etc/passwd')", "s3", serr));
        CHECK(!serr.empty());

        // include：加载 addon 根目录内脚本（可多次执行）
        WriteFile(luaRoot / "sh_core.lua",
                  "shared_var = shared_var or 0; shared_var = shared_var + 1\n");
        CHECK(vm.RunString("include('sh_core.lua'); include('sh_core.lua')",
                           "inc", lerr));
        CHECK(vm.GetGlobalInt("shared_var") == 2);

        // 版本信息
        CHECK(modhub::LuaVmVersion().find("Lua 5.1") != std::string::npos);
    }

    // GLua 引擎 API 存根：加载附加组件声明并登记（不模拟引擎行为）
    {
        modhub::LuaVm vm;
        vm.SetRootDir(luaRoot.string());
        std::string e;
        // 与实测附加组件同构的脚本（玩家模型 + NPC 登记）
        const std::string addonScript = R"GLUA(
player_manager.AddValidModel( "Tomorin", "models/edward/tomorin/pm/tomorin_pm.mdl" )
player_manager.AddValidHands( "Tomorin", "models/edward/tomorin/arms/tomorin_arms.mdl", 0, "00000000" )
local NPC = { Name = "Tomorin(Friendly)", Class = "npc_citizen", Health = "150", Model = "models/edward/tomorin/npc/tomorin_npc.mdl" }
list.Set( "NPC", "eddie_tomorin_friendly", NPC )
util.PrecacheModel( "models/edward/tomorin/pm/tomorin_pm.mdl" )
hook.Add( "Think", "test", function() end )
AddCSLuaFile( "cl_init.lua" )
)GLUA";
        CHECK(vm.RunString(addonScript, "addon", e));
        CHECK(e.empty());
        CHECK(vm.RegistryCount("playermodels") == 1);
        CHECK(vm.RegistryField("playermodels", 1, "name") == "Tomorin");
        CHECK(vm.RegistryField("playermodels", 1, "model") ==
              "models/edward/tomorin/pm/tomorin_pm.mdl");
        CHECK(vm.RegistryCount("hands") == 1);
        CHECK(vm.RegistryField("hands", 1, "body") == "0");
        CHECK(vm.RegistryField("hands", 1, "skin") == "00000000");
        CHECK(vm.RegistryListField("NPC", "eddie_tomorin_friendly",
                                   "Class") == "npc_citizen");
        CHECK(vm.RegistryListField("NPC", "eddie_tomorin_friendly",
                                   "Health") == "150");
        // 沙箱仍有效：os.execute 依旧不可用
        std::string s2;
        CHECK(!vm.RunString("os.execute('echo hi')", "s", s2));
    }

    // autorun：GMod 惯例 lua/autorun/*.lua，按文件名排序执行
    {
        WriteFile(luaRoot / "lua" / "autorun" / "a_setup.lua",
                  "from_autorun = 'loaded'\n");
        WriteFile(luaRoot / "lua" / "autorun" / "b_second.lua",
                  "autorun_count = (autorun_count or 0) + 1\n");
        modhub::LuaVm vm;
        vm.SetRootDir(luaRoot.string());
        std::string aerr;
        const auto ran = modhub::RunAutorun(vm, luaRoot.string(), aerr);
        CHECK(aerr.empty());
        CHECK(ran.size() == 2);
        CHECK(ran[0] == "a_setup.lua");
        CHECK(ran[1] == "b_second.lua");
        CHECK(vm.GetGlobalString("from_autorun") == "loaded");
        CHECK(vm.GetGlobalInt("autorun_count") == 1);
    }

    fs::remove_all(luaRoot);

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
