// GMA 解析器 - Garry's Mod Addon (.gma) 容器格式
//
// .gma 是 GMod 附加组件的打包容器（Facepunch 格式），内含地图/模型/
// 材质/音效/Lua 脚本。本解析器读取 .gma 头部与条目表，并支持把
// 内容安全地提取到目标目录 —— 提取出的地图与素材可在 HL2 等其他
// Source 游戏中使用（这就是"解析 GMod 模组并在 HL2 上运行其内容"）。
//
// 纯 C++17 标准库，无引擎依赖，可在任意平台编译与测试。
//
// 支持的两种真实布局（自动检测，CRC 校验决定选型）：
//
// 1) 现代布局 v2/v3（当前 gmad / SharpGMad 读取器兼容，实测 workshop 文件）：
//    "GMAD" + 单字节版本 + steamid u64 + 时间戳 u64
//    + (v>=2) required content: 空终止字符串列表(空串结束)
//    + 名称/描述/作者: 空终止字符串
//    + addon 版本 u32
//    + 条目表: 循环 { 序号 u32(=0 结束), 路径\0, 大小 u64, crc u32 }
//    + 数据区: 紧随条目表，按条目顺序排列（偏移 = 表尾 + 累计）
//
// 2) 经典布局 v1/v2（Valve Wiki 文档格式）：
//    "GMAD" + 版本 u32 + steamid u64 + 时间戳 u64
//    + (v>=2) required u64
//    + 名称 char[256] + 描述 char[65536] + 作者 char[128]
//    + addon 版本 u32
//    + 条目表: 循环 { 名称长度 u32(=0 结束), 路径, 大小 u64, crc u32, 偏移 u64 }
//    + 数据区: 按条目存储的绝对偏移定位
//
// 解析成功后逐条目做 CRC32 校验（IEEE 802.3 / zlib 兼容），
// 两种布局中校验通过者被采用；全部失败时报错。

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
    std::uint32_t crc = 0;        // CRC32 校验和
    std::uint64_t offset = 0;     // 数据在 .gma 文件中的绝对偏移
};

struct GmaFile {
    GmaHeader header;
    std::vector<GmaEntry> entries;
    std::string format;           // "v3-modern" / "v2-modern" / "v2-classic" / "v1-classic"
    bool crc_ok = false;          // 全部条目 CRC32 校验是否通过
};

// 解析 .gma 容器。成功返回 true；失败返回 false 并填充 error。
// 布局由 CRC32 校验自动选择；若结构可读但 CRC 未通过，
// 仍返回 true 但 crc_ok=false（调用方应视为"可能损坏"）。
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
