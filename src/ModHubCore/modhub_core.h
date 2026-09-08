// HL2 Mod Hub - 模组解析核心（纯 C++17 标准库，无引擎依赖）
//
// 本模块实现"解析模组文件夹中的起源引擎模组"的核心逻辑：
//   - 扫描目录下所有含 gameinfo.txt 的子目录
//   - 解析 gameinfo.txt，识别引擎类型(HL2/GMod/CS:S/TF2/Portal...)
//   - 提取 SteamAppId，判定有效性
//   - 生成引擎启动参数
// 它不依赖任何引擎头文件，可在任意平台（含 ARM64/Windows 交叉编译）
// 独立编译与单元测试。引擎集成见 ../ModHubEngine/。

#pragma once

#include <string>
#include <vector>

namespace modhub {

struct ModEntry {
    std::string name;                 // 模组目录名
    std::string path;                 // 模组目录绝对路径
    std::string engine;               // hl2 / gmod / css / tf2 / portal / unknown
    bool valid = true;
    std::vector<std::string> issues;  // 校验发现的问题
};

// 扫描 root 下所有含 gameinfo.txt 的子目录，返回按名称排序的模组列表
std::vector<ModEntry> ScanMods(const std::string& root);

// 从 gameinfo.txt 文本识别引擎类型（关键词匹配，兼容大小写）
std::string DetectEngine(const std::string& gameinfoText);

// 提取 SteamAppId（兼容 "SteamAppId" "220" 与 SteamAppId 220 两种写法）
std::string ParseSteamAppId(const std::string& gameinfoText);

// 判断某目录是否为有效模组目录（存在 gameinfo.txt）
bool HasGameInfo(const std::string& dirPath);

// 生成引擎启动参数，例如：-game "my_mod"
std::string BuildLaunchArgs(const ModEntry& mod);

}  // namespace modhub
