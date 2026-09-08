// ModHubPanel - 引擎内模组启动面板（VGUI2）
//
// 这是 HL2 Mod Hub 的"引擎内完整功能"层：在游戏里按下一个键，
// 弹出面板列出模组文件夹中的所有适用模组，选中后一键用对应引擎启动。
//
// 重要：本文件依赖 Source SDK 的头文件与库（vgui2 / vguimatsurface /
// filesystem / engine 接口），必须放进你的 Source SDK 客户端工程中编译
// （PC 版用 Steam 的 Source SDK 2013；安卓版需用为安卓构建的引擎源码）。
// 它不能脱离 SDK 独立编译——模组解析核心(ModHubCore)才可以。
//
// 集成步骤见同目录 README-engine.txt

#include "ModHubPanel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#endif

#include <cstdio>
#include <filesystem>
#include <string>

// ---- 以下头文件来自 Source SDK ----
#include "vgui/ISurface.h"
#include "vgui_controls/Frame.h"
#include "vgui_controls/ListPanel.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/PropertySheet.h"
#include "vgui/IVGui.h"
#include "tier1/KeyValues.h"
#include "filesystem.h"
#include "engine/igameengine.h"

using namespace vgui;

namespace {

// 模组根目录：PC 上通常是 <Steam>/steamapps/sourcemods，
// 安卓 nillerusr 引擎通常是内部存储的 srceng/。
// 可用控制台变量 modhub_root 覆盖。
ConVar modhub_root("modhub_root", "", FCVAR_ARCHIVE,
                   "Mod Hub 扫描的模组根目录，留空则自动探测");

std::string GetModRoot() {
    const char* cfg = modhub_root.GetString();
    if (cfg && cfg[0] != '\0') {
        return std::string(cfg);
    }
#ifdef ANDROID
    // 安卓起源引擎：内部存储 srceng 目录（用户可改）
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/srceng" : "/sdcard/srceng";
#else
    // PC/Linux：优先取当前模组所在目录的 steamapps/sourcemods
    //（可替换为 Steam 路径探测逻辑）
    std::filesystem::path exe;
    char buf[4096];
#ifdef _WIN32
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf)) > 0) {
        exe = std::filesystem::path(buf).parent_path();
    }
#else
    const char* p = getenv("STEAMAPPS");
    if (p && p[0]) return std::string(p) + "/sourcemods";
#endif
    if (!exe.empty()) {
        auto cand = exe.parent_path() / "steamapps" / "sourcemods";
        if (std::filesystem::is_directory(cand)) {
            return cand.string();
        }
    }
    return "sourcemods";
#endif
}

void LaunchWithEngine(const std::string& engineExe, const std::string& args) {
    // 起源引擎无法在运行中加载另一个模组进程：
    // 启动对应引擎的新进程来运行目标模组，本进程稍后退出。
#ifdef _WIN32
    std::string cmd = "\"" + engineExe + "\" " + args;
    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    if (CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#elif defined(ANDROID)
    // 安卓上无法从游戏 DLL 直接拉起新进程；提示在引擎界面选择模组
    Msg("[ModHub] 安卓请在引擎主界面选择要运行的模组。\n");
#else
    pid_t pid;
    std::string cmd = engineExe + " " + args;
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');
    char* argv[] = {buf.data(), nullptr};
    extern char** environ;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv, environ) == 0) {
        waitpid(pid, nullptr, 0);
    }
#endif
}

}  // namespace

ModHubPanel::ModHubPanel(Panel* parent, const char* name)
    : BaseClass(parent, name) {
    SetTitle("HL2 Mod Hub - 模组中心", true);
    SetSize(520, 420);
    SetMinimumSize(440, 320);
    SetDeleteSelfOnClose(true);
    SetMoveable(true);
    SetSizeable(true);

    m_pList = new ListPanel(this, "ModList");
    m_pList->AddColumnHeader(0, "name", "模组名", 150, ListPanel::COLUMN_RESIZEWITHWINDOW);
    m_pList->AddColumnHeader(1, "engine", "引擎", 90, ListPanel::COLUMN_RESIZEWITHWINDOW);
    m_pList->AddColumnHeader(2, "status", "状态", 70, ListPanel::COLUMN_FIXEDSIZE);
    m_pList->AddColumnHeader(3, "path", "路径", 180, ListPanel::COLUMN_RESIZEWITHWINDOW);

    m_pLaunch = new Button(this, "Launch", "启动所选模组");
    m_pClose = new Button(this, "Close", "关闭");
    m_pStatus = new Label(this, "Status", "");

    m_pLaunch->AddActionSignalTarget(this);
    m_pClose->AddActionSignalTarget(this);

    LoadControlSettings("resource/ModHubPanel.res");
    RefreshList();
}

void ModHubPanel::RefreshList() {
    m_pList->DeleteAllItems();
    const std::string root = GetModRoot();
    const auto mods = modhub::ScanMods(root);
    int row = 0;
    for (const auto& m : mods) {
        KeyValues* kv = new KeyValues("mod");
        kv->SetString("name", m.name.c_str());
        kv->SetString("engine", m.engine.c_str());
        kv->SetString("status", m.valid ? "OK" : "无效");
        kv->SetString("path", m.path.c_str());
        m_pList->AddItem(kv, m.valid);
        kv->deleteThis();
        ++row;
    }
    m_pStatus->SetText(
        (mods.empty() ? "未在模组根目录发现任何模组（可用控制台变量 modhub_root 指定路径）"
                      : "共发现 %d 个模组").c_str(), static_cast<int>(mods.size()));
}

void ModHubPanel::OnCommand(const char* command) {
    if (!V_stricmp(command, "Launch")) {
        int row = m_pList->GetSelectedItem();
        if (row == -1) {
            m_pStatus->SetText("请先在列表中选择一个模组");
            return;
        }
        KeyValues* kv = m_pList->GetItem(row);
        if (!kv) return;
        modhub::ModEntry mod;
        mod.name = kv->GetString("name");
        mod.engine = kv->GetString("engine");
        std::string engineExe;
#ifdef _WIN32
        engineExe = "hl2.exe";  // 同一安装内引擎；可改为按引擎选择
#else
        engineExe = "hl2_linux";
#endif
        LaunchWithEngine(engineExe, modhub::BuildLaunchArgs(mod));
        m_pStatus->SetText("已调用引擎启动 %s，请在引擎中选择继续运行", mod.name.c_str());
    } else if (!V_stricmp(command, "Close")) {
        Close();
    } else {
        BaseClass::OnCommand(command);
    }
}
