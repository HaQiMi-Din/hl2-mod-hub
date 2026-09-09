// modhub_loader_test - gma_loader（模组内置 GMA 自动加载）单元测试
//
// 验证：
//  1) 合成一个现代 v3 布局 .gma（复刻用户 new_item_via_crowbar.gma 的
//     字节结构：GMAD+单字节版本+空终止字符串头+[序号u32][路径\0][u64][u32]
//     条目表+紧贴数据区），真实 CRC32；
//  2) 放进 <临时>/mod/，ScanAndMountMods 应解包到 mod_unpacked/，
//     文件内容逐字节一致；
//  3) 二次调用命中缓存（fresh_extract=false），内容仍可读取；
//  4) 往 mod/ 添加第二个 gma，正确追加解包。
//
// 编译（与 mod_scanner_test 同法）：
//  g++ -std=c++17 modhub_loader_test.cpp gma_loader.cpp gma_parser.cpp -o test

#include "gma_loader.h"
#include "gma_parser.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (cond) {                                             \
            ++g_pass;                                           \
            std::printf("  PASS  %s\n", msg);                   \
        } else {                                                \
            ++g_fail;                                           \
            std::printf("  FAIL  %s\n", msg);                   \
        }                                                       \
    } while (0)

// ---- 现代 v3 布局 .gma 合成器（与 gma_parser 头注释一致）----
namespace {

std::uint32_t Crc32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            std::uint32_t mask = static_cast<std::uint32_t>(-(crc & 1));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

struct FakeEntry {
    std::string path;
    std::vector<std::uint8_t> data;
};

void PutU32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
}
void PutU64(std::vector<std::uint8_t>& v, std::uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
}
void PutCStr(std::vector<std::uint8_t>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}

std::vector<std::uint8_t> BuildModernGmaV3(const std::string& name,
                                           const std::string& desc,
                                           const std::string& author,
                                           const std::vector<FakeEntry>& entries) {
    std::vector<std::uint8_t> out;
    // 签名 + 单字节版本
    out.insert(out.end(), {'G', 'M', 'A', 'D'});
    out.push_back(3);
    // steamid u64 + timestamp u64
    PutU64(out, 0);
    PutU64(out, 1741071119);
    // required content: 空终止字符串列表（空串结束）
    PutCStr(out, "");
    // 名称 / 描述 / 作者
    PutCStr(out, name);
    PutCStr(out, desc);
    PutCStr(out, author);
    // addon 版本 u32
    PutU32(out, 1);
    // 条目表
    std::uint32_t idx = 1;
    for (const auto& e : entries) {
        PutU32(out, idx++);
        PutCStr(out, e.path);
        PutU64(out, e.data.size());
        PutU32(out, Crc32(e.data.data(), e.data.size()));
    }
    PutU32(out, 0);  // 终止
    // 数据区（紧贴表尾，按条目顺序）
    for (const auto& e : entries) {
        out.insert(out.end(), e.data.begin(), e.data.end());
    }
    return out;
}

void WriteFile(const fs::path& p, const std::vector<std::uint8_t>& data) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

std::vector<std::uint8_t> ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    return data;
}

}  // namespace

int main() {
    std::printf("== modhub_loader_test: 模组内置 GMA 自动加载 ==\n");

    // 临时游戏目录
    fs::path gameDir = fs::temp_directory_path() / "modhub_loader_test";
    fs::remove_all(gameDir);
    fs::create_directories(gameDir / "mod");

    // 条目 1：模型（16 字节内容）与条目 2：材质（128 字节内容）
    std::vector<FakeEntry> entries;
    entries.push_back({"models/edward/tomorin/pm/tomorin_pm.mdl",
                       std::vector<std::uint8_t>(16, 0xAB)});
    std::vector<std::uint8_t> matData(128);
    for (std::size_t i = 0; i < matData.size(); ++i) matData[i] = static_cast<std::uint8_t>(i * 7);
    entries.push_back({"materials/edward/tomorin/body.vmt", matData});

    const auto gmaBytes = BuildModernGmaV3(
        "Test Addon", "{\"type\":\"servercontent\"}", "Tester", entries);
    WriteFile(gameDir / "mod" / "test_addon.gma", gmaBytes);

    // 1) 首次扫描解包
    std::printf("-- 首次扫描 --\n");
    std::string err;
    auto results = modhub::ScanAndMountMods(gameDir.string(), "mod", &err);
    CHECK(results.size() == 1, "发现 1 个 .gma");
    CHECK(results[0].error.empty(), "无错误");
    CHECK(results[0].crc_ok, "CRC32 全部通过");
    CHECK(results[0].format == "v3-modern", "格式识别为 v3-modern");
    CHECK(results[0].entry_count == 2, "条目数 = 2");
    CHECK(results[0].fresh_extract, "本次为全新解包");

    // 2) 解包内容逐字节一致
    std::printf("-- 解包内容 --\n");
    const fs::path outRoot = gameDir / "mod_unpacked" / "test_addon";
    CHECK(fs::exists(outRoot / "models/edward/tomorin/pm/tomorin_pm.mdl"),
          "模型文件已解包");
    CHECK(fs::exists(outRoot / "materials/edward/tomorin/body.vmt"),
          "材质文件已解包");
    CHECK(ReadFile(outRoot / "models/edward/tomorin/pm/tomorin_pm.mdl") ==
              entries[0].data,
          "模型内容逐字节一致");
    CHECK(ReadFile(outRoot / "materials/edward/tomorin/body.vmt") ==
              entries[1].data,
          "材质内容逐字节一致");

    // 3) 二次调用命中缓存
    std::printf("-- 二次扫描（缓存） --\n");
    auto results2 = modhub::ScanAndMountMods(gameDir.string(), "mod", &err);
    CHECK(results2.size() == 1, "仍发现 1 个 .gma");
    CHECK(!results2[0].fresh_extract, "命中缓存，未重新解包");
    CHECK(results2[0].entry_count == 2, "缓存保留条目数");
    CHECK(fs::exists(outRoot / "models/edward/tomorin/pm/tomorin_pm.mdl"),
          "缓存命中后内容仍在");

    // 4) 追加第二个 gma
    std::printf("-- 追加第二个 gma --\n");
    std::vector<FakeEntry> entries2;
    entries2.push_back({"maps/modhub_extra.bsp", std::vector<std::uint8_t>(64, 0x77)});
    WriteFile(gameDir / "mod" / "second.gma",
              BuildModernGmaV3("Second", "d", "a", entries2));
    auto results3 = modhub::ScanAndMountMods(gameDir.string(), "mod", &err);
    CHECK(results3.size() == 2, "发现 2 个 .gma");
    {
        // 结果按 .gma 文件名排序（second.gma 在 test_addon.gma 前）
        bool secondFresh = false;
        for (const auto& r : results3) {
            if (r.gma_name == "second.gma") secondFresh = r.fresh_extract;
        }
        CHECK(secondFresh, "第二个 gma 为全新解包");
    }
    CHECK(fs::exists(gameDir / "mod_unpacked" / "second" / "maps/modhub_extra.bsp"),
          "第二个 gma 内容已解包");

    // 5) 无 mod 目录时返回空（不报错）
    std::printf("-- 边界 --\n");
    auto empty = modhub::ScanAndMountMods(
        (fs::temp_directory_path() / "no_such_mod_dir_xyz").string(), "mod", &err);
    CHECK(empty.empty(), "mod 目录不存在时返回空清单");

    fs::remove_all(gameDir);

    std::printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
