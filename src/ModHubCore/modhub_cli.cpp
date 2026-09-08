// modhub_cli - 模组解析核心命令行工具
//
// 用法:
//   modhub_cli scan <目录>
//       扫描模组文件夹，解析 gameinfo.txt，识别引擎
//   modhub_cli gma <文件.gma> [--extract <目录>] [--run-lua]
//       解析 GMod 附加组件 (.gma) 容器
//       --extract <目录>  提取全部内容到目录
//       --run-lua         提取后按 GMod 惯例执行 lua/autorun/*.lua
//
// 编译（与单元测试同法，见 README）：
//   gcc 编 lua/*.c → g++ 链接本文件 + modhub_core + gma_parser + lua_vm

#include "gma_parser.h"
#include "lua_vm.h"
#include "modhub_core.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void PrintUsage() {
    std::printf(
        "modhub_cli - HL2 Mod Hub 模组解析工具\n"
        "用法:\n"
        "  modhub_cli scan <目录>                   扫描模组文件夹\n"
        "  modhub_cli gma <文件.gma> [--extract <目录>] [--run-lua]\n"
        "     解析 .gma; --extract 提取内容; --run-lua 执行 autorun 脚本\n");
}

static int CmdScan(const std::vector<std::string>& args) {
    if (args.size() < 1) {
        PrintUsage();
        return 1;
    }
    const auto mods = modhub::ScanMods(args[0]);
    std::printf("扫描 %s: 发现 %zu 个模组\n", args[0].c_str(), mods.size());
    for (const auto& m : mods) {
        std::printf("  %-24s 引擎=%s  %s\n", m.name.c_str(), m.engine.c_str(),
                    m.valid ? "有效" : "无效");
        if (!m.valid) {
            for (const auto& i : m.issues) {
                std::printf("      - %s\n", i.c_str());
            }
        } else {
            std::printf("      启动参数: %s\n",
                        modhub::BuildLaunchArgs(m).c_str());
        }
    }
    return 0;
}

static int CmdGma(const std::vector<std::string>& args) {
    if (args.size() < 1) {
        PrintUsage();
        return 1;
    }
    const std::string path = args[0];
    std::string extractDir, err;
    bool runLua = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--extract" && i + 1 < args.size()) {
            extractDir = args[++i];
        } else if (args[i] == "--run-lua") {
            runLua = true;
        }
    }

    modhub::GmaFile gma;
    if (!modhub::ParseGma(path, gma, err)) {
        std::printf("解析失败: %s\n", err.c_str());
        return 1;
    }

    std::printf("=== GMA 头部 ===\n");
    std::printf("  名称: %s\n", gma.header.name.c_str());
    std::printf("  作者: %s\n", gma.header.author.c_str());
    std::printf("  描述: %s\n", gma.header.description.c_str());
    std::printf("  版本: %d (GMA %u)  SteamID: %llu\n",
                gma.header.addon_version, gma.header.version,
                static_cast<unsigned long long>(gma.header.steam_id));

    std::printf("=== 条目: %zu 个 ===\n", gma.entries.size());
    int nMap = 0, nModel = 0, nMat = 0, nLua = 0, nSound = 0, nOther = 0;
    for (const auto& e : gma.entries) {
        if (e.name.rfind("maps/", 0) == 0) nMap++;
        else if (e.name.rfind("models/", 0) == 0) nModel++;
        else if (e.name.rfind("materials/", 0) == 0) nMat++;
        else if (e.name.rfind("lua/", 0) == 0) nLua++;
        else if (e.name.rfind("sound/", 0) == 0) nSound++;
        else nOther++;
    }
    std::printf("  地图 %d | 模型 %d | 材质 %d | Lua %d | 音效 %d | 其他 %d\n",
                nMap, nModel, nMat, nLua, nSound, nOther);

    bool needExtract = runLua || !extractDir.empty();
    std::string dest = extractDir;
    if (needExtract) {
        if (dest.empty()) {
            dest = (fs::temp_directory_path() /
                    ("modhub_extract_" + std::to_string(gma.header.timestamp)))
                       .string();
            std::printf("=== 提取到临时目录: %s ===\n", dest.c_str());
        }
        err.clear();
        const std::size_t ok = modhub::ExtractGmaAll(path, gma, dest, err);
        std::printf("提取 %zu/%zu 个条目%s%s\n", ok, gma.entries.size(),
                    err.empty() ? "" : "，部分失败: ",
                    err.empty() ? "" : err.c_str());
        if (ok == 0) return 1;
    }

    if (runLua) {
        std::printf("=== 执行 Lua (autorun 惯例) ===\n");
        modhub::LuaVm vm;
        vm.SetRootDir(dest);
        std::printf("  VM: %s\n", modhub::LuaVmVersion().c_str());
        err.clear();
        const auto ran = modhub::RunAutorun(vm, dest, err);
        std::printf("  成功执行 %zu 个 autorun 脚本\n", ran.size());
        for (const auto& s : ran) {
            std::printf("    [OK] %s\n", s.c_str());
        }
        if (!err.empty()) {
            std::printf("  [失败] %s\n", err.c_str());
        }
        // 尝试执行全部 lua/**/*.lua 并统计成败
        std::size_t total = 0, passed = 0, failed = 0;
        std::error_code ec;
        const fs::path luaDir = fs::path(dest) / "lua";
        if (fs::is_directory(luaDir, ec)) {
            for (const auto& f :
                 fs::recursive_directory_iterator(luaDir, ec)) {
                if (!f.is_regular_file(ec) || f.path().extension() != ".lua")
                    continue;
                ++total;
                std::string e;
                if (vm.RunFile(f.path().string(), e)) {
                    ++passed;
                } else {
                    ++failed;
                    if (failed <= 5) {
                        std::printf("    [Lua错误] %s: %s\n",
                                    f.path().filename().string().c_str(),
                                    e.c_str());
                    }
                }
            }
        }
        std::printf("  全部 lua 脚本: %zu 成功 / %zu 失败 / 共 %zu\n",
                    passed, failed, total);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    const std::vector<std::string> args(argv + 2, argv + argc);
    const std::string cmd = argv[1];
    if (cmd == "scan") return CmdScan(args);
    if (cmd == "gma") return CmdGma(args);
    PrintUsage();
    return 1;
}
