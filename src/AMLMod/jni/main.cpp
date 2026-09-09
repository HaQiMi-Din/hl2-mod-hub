// modhub_aml - HL2 Mod Hub 安卓注入模组（AndroidModLoader / AML）
//
// 路线 C：不改安卓引擎源码，用 AML 把我们的"模组内置 GMA 自动加载器"
// 注入进已运行的起源引擎进程。
//
// 工作方式：
//   1. AML 在游戏主库加载后注入本 .so（MYMOD 宏声明，ON_MOD_LOAD 入口）；
//   2. 从 libfilesystem_stdio.so 的 CreateInterface("VFileSystem017")
//      拿到引擎 IFileSystem*（Source 2013 标准接口名）；
//   3. 数据根目录取环境变量 VALVE_GAME_PATH（引擎启动时已 setenv，
//      通常 = /storage/emulated/0/srceng）；
//   4. 扫描 hl2/custom/*/mod/*.gma 与 hl2/mod/*.gma：
//      用 gma_parser + gma_loader 解析、CRC 校验、解包到
//      <同目录>/mod_unpacked/<gma名>/；
//   5. 把每个解包目录 AddSearchPath("GAME", PATH_ADD_TO_HEAD) 挂进引擎
//      文件系统 —— 模型/材质/地图立即在游戏内可用，全程无需 Termux。
//
// 依赖：
//   - AndroidModLoader 官方模组头（include/mod/，MIT，见 LICENSE-AML.txt）
//   - gma_parser / gma_loader（纯 C++17，由 prepare.sh 同步进 jni/）
//
// 编译（本仓库 CI 已做，也可本地）：
//   ./src/AMLMod/prepare.sh
//   $NDK/ndk-build NDK_PROJECT_PATH=src/AMLMod APP_BUILD_SCRIPT=src/AMLMod/jni/Android.mk \
//                 NDK_APPLICATION_MK=src/AMLMod/jni/Application.mk

#include <mod/amlmod.h>
#include <mod/logger.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "gma_loader.h"

MYMOD(net.haqimi.modhub, HL2 Mod Hub Auto-Mounter, 1.2.0, HaQiMi-Din)

// Source 2013 的 SearchPathAdd_t
enum { kPathAddToHead = 0, kPathAddToTail = 1 };

// IFileSystem 接口名（Source 2013）
static const char* kFileSystemInterface = "VFileSystem017";

typedef void* (*CreateInterfaceFn)(const char* name, int* returnCode);

// IBaseFileSystem vtable 槽位（Source 2013 filesystem.h，自 Source SDK 2013）：
//   0: AddSearchPath(const char*, const char*, SearchPathAdd_t)
//   3: GetSearchPath(const char*, bool, char*, int)
typedef void (*AddSearchPathFn)(void* self, const char* pPath,
                                const char* pathID, int addType);
typedef void (*GetSearchPathFn)(void* self, const char* pathID,
                                int bGetPackFiles, char* pPath, int nMaxLen);

static void* g_fs = nullptr;
static AddSearchPathFn g_addSearchPath = nullptr;
static GetSearchPathFn g_getSearchPath = nullptr;

// 拿引擎文件系统接口（libfilesystem_stdio.so 的 CreateInterface）。
static bool AcquireFileSystem()
{
    void* handle = aml->GetLibHandle("libfilesystem_stdio.so");
    if (!handle)
    {
        logger->Error("libfilesystem_stdio.so not found");
        return false;
    }
    uintptr_t ci = aml->GetSym(handle, "CreateInterface");
    if (!ci)
    {
        logger->Error("CreateInterface symbol not found");
        return false;
    }
    auto createInterface = reinterpret_cast<CreateInterfaceFn>(ci);
    g_fs = createInterface(kFileSystemInterface, nullptr);
    if (!g_fs)
    {
        logger->Error("CreateInterface(%s) returned NULL", kFileSystemInterface);
        return false;
    }
    void** vt = *reinterpret_cast<void***>(g_fs);
    g_addSearchPath = reinterpret_cast<AddSearchPathFn>(vt[0]);
    g_getSearchPath = reinterpret_cast<GetSearchPathFn>(vt[3]);
    return true;
}

// 自检 vtable 布局：GetSearchPath 应返回含 GAME 路径的字符串。
static bool VerifyFileSystemVtable()
{
    if (!g_getSearchPath) return false;
    char buf[1024] = {0};
    g_getSearchPath(g_fs, "GAME", 0, buf, (int)sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    logger->Info("GAME search path now: %s", buf[0] ? buf : "(empty)");
    return buf[0] != 0;
}

static void MountDir(const std::string& gameDir, const std::string& resultOutDir,
                     const modhub::GmaMountResult& r)
{
    if (!r.error.empty())
    {
        logger->Error("[%s] %s", r.gma_name.c_str(), r.error.c_str());
        return;
    }
    std::filesystem::path absOut =
        std::filesystem::path(gameDir) / resultOutDir;
    if (!std::filesystem::exists(absOut))
    {
        logger->Error("[%s] unpack dir missing: %s", r.gma_name.c_str(),
                      absOut.string().c_str());
        return;
    }
    if (g_addSearchPath)
    {
        g_addSearchPath(g_fs, absOut.string().c_str(), "GAME", kPathAddToHead);
        logger->Info("[%s] mounted %s (%zu entries, CRC %s, %s)", r.gma_name.c_str(),
                     absOut.string().c_str(), r.entry_count,
                     r.crc_ok ? "OK" : "FAILED",
                     r.fresh_extract ? "extracted" : "cached");
    }
}

// 扫描并挂载一个游戏目录下的 mod/*.gma。
static void ScanAndMountDir(const std::string& gameDir)
{
    if (gameDir.empty()) return;
    std::string err;
    auto results = modhub::ScanAndMountMods(gameDir, "mod", &err);
    for (const auto& r : results)
    {
        // r.out_dir 相对 gameDir（mod_unpacked/xxx）
        MountDir(gameDir, r.out_dir, r);
    }
}

extern "C" void OnModLoad()
{
    logger->SetTag("ModHub");

    if (!AcquireFileSystem())
    {
        logger->Error("ModHub: cannot get IFileSystem, abort");
        return;
    }
    VerifyFileSystemVtable();

    const char* base = getenv("VALVE_GAME_PATH");
    if (!base || !*base)
    {
        base = "/storage/emulated/0/srceng";
        logger->Print(LogP_Warn, "VALVE_GAME_PATH unset, fallback %s", base);
    }
    logger->Info("BaseDir: %s", base);

    // 用户部署方式：hl2/custom/<mod>/mod/*.gma（custom 内容挂载）
    std::filesystem::path customRoot =
        std::filesystem::path(base) / "hl2" / "custom";
    if (std::filesystem::exists(customRoot))
    {
        for (const auto& entry :
             std::filesystem::directory_iterator(customRoot))
        {
            if (!entry.is_directory()) continue;
            ScanAndMountDir(entry.path().string());
        }
    }

    // 独立模组方式：hl2/mod/*.gma
    ScanAndMountDir((std::filesystem::path(base) / "hl2").string());

    logger->Info("ModHub auto-mounter finished.");
}
