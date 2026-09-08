// GMA 解析器 - Garry's Mod Addon (.gma) 容器格式
//
// .gma 是 GMod 附加组件的打包容器（Facepunch 格式），内含地图/模型/
// 材质/音效/Lua 脚本。本解析器读取 .gma 头部与条目表，并支持把
// 内容安全地提取到目标目录 —— 提取出的地图与素材可在 HL2 等其他
// Source 游戏中使用（这就是"解析 GMod 模组并在 HL2 上运行其内容"）。
//
// 纯 C++17 标准库，无引擎依赖，可在任意平台编译与测试。
//
// 格式参考：
//   https://developer.valvesoftware.com/wiki/GMA_(file_format)
// 头部: "GMAD" + u32版本 + u64 steamid + u64 时间戳 + u64 required +
//       char[256]名称 + char[65536]描述 + char[128]作者 + u32 版本号
// 条目表: 循环 { u32 名称长度(=0 结束), 名称, u64 大小, u32 crc, u64 偏移 }
// 条目表结束后为文件数据区（偏移为文件内绝对偏移）。

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace modhub {

struct GmaHeader {
    std::uint32_t version = 0;
    std::uint64_t steam_id = 0;
    std::uint64_t timestamp = 0;
    std::uint64_t required_content = 0;
    std::string name;
    std::string description;
    std::string author;
    std::uint32_t addon_version = 0;
};

struct GmaEntry {
    std::string name;             // 相对路径（如 maps/test.bsp）
    std::uint64_t size = 0;       // 数据大小
    std::uint32_t crc = 0;        // 校验和（占位/校验）
    std::uint64_t offset = 0;     // 数据在 .gma 文件中的绝对偏移
};

struct GmaFile {
    GmaHeader header;
    std::vector<GmaEntry> entries;
};

// 解析 .gma 容器。成功返回 true；失败返回 false 并填充 error。
bool ParseGma(const std::string& path, GmaFile& out, std::string& error);

// 提取单个条目到 outDir（安全：拒绝绝对路径与 ".." 穿越）。
bool ExtractGmaEntry(const std::string& gmaPath, const GmaFile& gma,
                     const GmaEntry& entry, const std::string& outDir,
                     std::string& error);

// 提取全部条目，返回成功提取的条数。
std::size_t ExtractGmaAll(const std::string& gmaPath, const GmaFile& gma,
                          const std::string& outDir, std::string& error);

// 扫描目录下所有 .gma 文件（按名称排序）。
std::vector<std::string> ScanGmaFiles(const std::string& root);

}  // namespace modhub
