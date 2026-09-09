// engine_gma_hook - 把 gma_loader 挂进起源引擎（依赖 Source SDK）
//
// 【编译前提】本文件需要 Source SDK 2013 头文件与引擎接口，不能像
// ModHubCore 那样独立编译；源码交付，按 README-engine.txt 在 Steam
// "Source SDK 2013" 工具工程中编译进模组的 client 模块。
//
// 【集成步骤（概念）】
// 1) 在模组 client 扩展的初始化（CBaseClientDLL::Init 或游戏启动早期）
//    调用 modhub::RunModHubStartup(engine->GetGameDirectory())；
// 2) RunModHubStartup 会扫描 <游戏目录>/mod/*.gma → 解包到
//    <游戏目录>/mod_unpacked/ → 把每个解包目录 AddSearchPath 进
//    "GAME" 路径头部（优先级高于原版内容），游戏内立即可用；
// 3) 注册控制台命令 modhub_scan 可随时手动重新扫描（如往 mod/ 里
//    新丢入 .gma 后，在游戏内执行 modhub_scan 即时挂载）。

#include "gma_loader.h"

// ---- Source SDK 头（在你的 SDK 工程中包含）----
// #include "cbase.h"
// #include "filesystem.h"
// #include "convar.h"
// #include "engine/igameengine.h"
// #include "tier1/utlvector.h"

namespace modhub {

// 引擎侧挂载：把每个解包目录加进 GAME 搜索路径（头部优先）。
// fs 为 IFileSystem*（通过 engine->GetFileSystem() 获取）。
static void MountUnpackedDirs(void* fsInterface,
                              const std::vector<GmaMountResult>& results) {
    // 真实代码（取消注释并在 SDK 工程中编译）：
    // IFileSystem* fs = static_cast<IFileSystem*>(fsInterface);
    // for (const auto& r : results) {
    //     if (r.error.empty() && !r.out_dir.empty()) {
    //         fs->AddSearchPath(r.out_dir.c_str(), "GAME", PATH_ADD_TO_HEAD);
    //         Msg("[ModHub] 已挂载 %s (%zu 条目, CRC %s)\n",
    //             r.gma_name.c_str(), r.entry_count,
    //             r.crc_ok ? "OK" : "FAILED");
    //     }
    // }
    (void)fsInterface;
    (void)results;
}

// 启动入口：扫描 mod/、解包、挂载。返回挂载结果供日志输出。
// 在模组 client 模块 Init 阶段调用一次；返回前已完成全部挂载。
std::vector<GmaMountResult> RunModHubStartup(const std::string& gameDir,
                                             void* fsInterface,
                                             std::string* error) {
    auto results = ScanAndMountMods(gameDir, "mod", error);
    MountUnpackedDirs(fsInterface, results);

    // 真实代码（日志输出示例，SDK 的 Msg()）：
    // for (const auto& r : results) {
    //     if (!r.error.empty()) {
    //         Warning("[ModHub] %s 失败: %s\n", r.gma_name.c_str(), r.error.c_str());
    //     } else {
    //         Msg("[ModHub] %s -> %s (%zu 条目, %s)\n",
    //             r.gma_name.c_str(), r.out_dir.c_str(), r.entry_count,
    //             r.fresh_extract ? "本次解包" : "缓存命中");
    //     }
    // }
    return results;
}

// 控制台命令 modhub_scan：手动重新扫描挂载（往 mod/ 丢新 .gma 后调用）。
// 真实代码（SDK 工程中）：
//   static void ModHubScanCmd(const CCommand&) {
//       modhub::RunModHubStartup(engine->GetGameDirectory(),
//                                engine->GetFileSystem(), nullptr);
//   }
//   static ConCommand modhub_scan("modhub_scan", ModHubScanCmd,
//       "重新扫描并挂载 mod/ 下的 .gma 附加组件", FCVAR_NONE);

}  // namespace modhub
