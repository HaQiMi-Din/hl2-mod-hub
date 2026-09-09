// gma_loader - 模组内置 GMA 自动加载器（纯逻辑，无引擎依赖）
//
// 功能：游戏启动时（由引擎内代码调用）扫描  <游戏目录>/mod/*.gma，
// 逐个用 gma_parser 解析、校验 CRC，并把内容解包到
// <游戏目录>/mod_unpacked/<gma文件名>/ 下，返回挂载清单；
// 引擎侧拿到清单后把每个解包目录 AddSearchPath 进 GAME 路径，
// 模组内容（模型/材质/地图）即可在游戏内直接使用 —— 全程无需
// Termux、无需外部解包工具。
//
// 目录约定（用户指定）：
//   <游戏目录>/mod/*.gma                    放 GMod 附加组件
//   <游戏目录>/mod_unpacked/<gma名>/        自动解包输出（引擎自动挂载）
//   <游戏目录>/mod_unpacked/.manifest.json  解包缓存清单（幂等，二次启动跳过）
//
// 纯 C++17 标准库：可独立编译测试（CI 已验证），引擎钩子见
// engine_gma_hook.cpp（依赖 Source SDK）。

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace modhub {

struct GmaMountResult {
    std::string gma_name;          // mod/ 下的 .gma 文件名
    std::string out_dir;           // 解包输出目录（相对 gameDir，如 mod_unpacked/xxx）
    std::string format;            // 检测到的 GMA 布局（v3-modern 等）
    std::size_t entry_count = 0;   // 条目数
    bool crc_ok = false;           // CRC32 是否全部通过
    bool fresh_extract = false;    // 本次是否真正解包（false=命中缓存）
    std::string error;             // 该 gma 的错误信息（空=成功）
};

// 扫描并解包 <gameDir>/<modSubdir>/*.gma，返回每个附加组件的挂载信息。
// gameDir 为引擎游戏目录（模组根目录）；modSubdir 默认 "mod"。
// 幂等：已在 mod_unpacked/.manifest.json 中登记且文件大小+mtime 未变的
// 附加组件会跳过解包（fresh_extract=false），加快二次启动。
std::vector<GmaMountResult> ScanAndMountMods(
    const std::string& gameDir,
    const std::string& modSubdir = "mod",
    std::string* error = nullptr);

}  // namespace modhub
