// gma_loader.cpp - 模组内置 GMA 自动加载器实现
//
// 依赖 gma_parser（解析/CRC/解包）+ 标准库。无引擎依赖。
// 细节：
//  - manifest 为行式 JSON 数组（手工拼写，避免第三方依赖）：
//    [{"gma":"a.gma","size":24919268,"mtime":1700000000,"count":33,"crc":true,"dir":"a"}]
//  - 缓存命中条件：文件名存在且 size 与 mtime 均相同 → 跳过解包。
//  - 解包目录名取 gma 去掉扩展名（冲突时追加短哈希）。

#include "gma_loader.h"

#include "gma_parser.h"

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace modhub {
namespace {

// 简单 JSON 字符串转义（够 manifest 用即可）。
std::string JsonEsc(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

struct ManifestEntry {
    std::string gma;
    std::uintmax_t size = 0;
    std::intmax_t mtime = 0;
    std::size_t count = 0;
    bool crc = false;
    std::string dir;
};

std::string ManifestPath(const std::string& gameDir, const std::string& sub) {
    return (fs::path(gameDir) / sub / ".manifest.json").string();
}

std::vector<ManifestEntry> LoadManifest(const std::string& path) {
    std::vector<ManifestEntry> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        // 行格式: gma|size|mtime|count|crc|dir
        std::istringstream ss(line);
        ManifestEntry e;
        std::string crcStr;
        if (std::getline(ss, e.gma, '|') &&
            (ss >> e.size) &&
            ss.get() == '|' &&
            (ss >> e.mtime) &&
            ss.get() == '|' &&
            (ss >> e.count) &&
            ss.get() == '|' &&
            std::getline(ss, crcStr, '|') &&
            std::getline(ss, e.dir)) {
            e.crc = (crcStr == "1");
            out.push_back(std::move(e));
        }
    }
    return out;
}

void SaveManifest(const std::string& path,
                  const std::vector<ManifestEntry>& entries) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (const auto& e : entries) {
        f << JsonEsc(e.gma) << '|' << e.size << '|' << e.mtime << '|'
          << e.count << '|' << (e.crc ? 1 : 0) << '|' << JsonEsc(e.dir) << '\n';
    }
}

std::intmax_t FileMtime(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return static_cast<std::intmax_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            t.time_since_epoch()).count());
}

std::string SafeDirName(const std::string& gmaName) {
    std::string stem = fs::path(gmaName).stem().string();
    std::string out;
    for (char c : stem) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out += c;
        } else {
            out += '_';
        }
    }
    if (out.empty()) out = "addon";
    return out;
}

}  // namespace

std::vector<GmaMountResult> ScanAndMountMods(const std::string& gameDir,
                                             const std::string& modSubdir,
                                             std::string* error) {
    std::vector<GmaMountResult> results;
    if (error) error->clear();

    const fs::path modDir = fs::path(gameDir) / modSubdir;
    const fs::path unpackRoot = fs::path(gameDir) / "mod_unpacked";
    std::error_code ec;

    if (!fs::exists(modDir, ec)) {
        if (error) *error = "mod 目录不存在: " + modDir.string();
        return results;  // 没有 mod/ 目录不是错误，返回空清单
    }

    fs::create_directories(unpackRoot, ec);

    const auto manifestPath = ManifestPath(gameDir, "mod_unpacked");
    auto manifest = LoadManifest(manifestPath);

    std::vector<std::string> gmaFiles = ScanGmaFiles(modDir.string());

    for (const auto& gmaPath : gmaFiles) {
        GmaMountResult r;
        r.gma_name = fs::path(gmaPath).filename().string();
        const std::string dirName = SafeDirName(r.gma_name);
        r.out_dir = (fs::path("mod_unpacked") / dirName).string();

        const std::uintmax_t size = fs::file_size(gmaPath, ec);
        if (ec) {
            r.error = "无法读取文件大小";
            results.push_back(std::move(r));
            continue;
        }
        const std::intmax_t mtime = FileMtime(gmaPath);

        // 缓存命中判断
        bool cached = false;
        for (const auto& m : manifest) {
            if (m.gma == r.gma_name && m.size == size && m.mtime == mtime) {
                r.entry_count = m.count;
                r.crc_ok = m.crc;
                r.fresh_extract = false;
                cached = true;
                break;
            }
        }

        if (!cached) {
            GmaFile gma;
            std::string perr;
            if (!ParseGma(gmaPath, gma, perr)) {
                r.error = "GMA 解析失败: " + perr;
                results.push_back(std::move(r));
                continue;
            }
            r.format = gma.format;
            r.crc_ok = gma.crc_ok;
            r.entry_count = gma.entries.size();

            const fs::path outDir = unpackRoot / dirName;
            fs::remove_all(outDir, ec);
            fs::create_directories(outDir, ec);

            std::string xerr;
            const std::size_t extracted = ExtractGmaAll(gmaPath, gma, outDir.string(), xerr);
            if (extracted < gma.entries.size()) {
                r.error = "解包不完整: " + xerr + " (" +
                          std::to_string(extracted) + "/" +
                          std::to_string(gma.entries.size()) + ")";
            } else if (!xerr.empty()) {
                r.error = xerr;
            }
            r.fresh_extract = true;

            // 更新 manifest（移除同名单旧条目）
            manifest.erase(
                std::remove_if(manifest.begin(), manifest.end(),
                               [&](const ManifestEntry& m) {
                                   return m.gma == r.gma_name;
                               }),
                manifest.end());
            ManifestEntry me;
            me.gma = r.gma_name;
            me.size = size;
            me.mtime = mtime;
            me.count = r.entry_count;
            me.crc = r.crc_ok;
            me.dir = dirName;
            manifest.push_back(std::move(me));
            SaveManifest(manifestPath, manifest);
        }

        results.push_back(std::move(r));
    }
    return results;
}

}  // namespace modhub
