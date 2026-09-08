#include "gma_parser.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

namespace modhub {
namespace {

bool ReadBytes(std::ifstream& f, void* dst, std::size_t n) {
    f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(f.gcount()) == n;
}

std::uint32_t LE32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t LE64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | p[i];
    }
    return v;
}

std::string FixedString(const unsigned char* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != '\0') {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(p), len);
}

}  // namespace

bool ParseGma(const std::string& path, GmaFile& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "无法打开文件: " + path;
        return false;
    }

    constexpr std::size_t kHeaderSize = 4 + 4 + 8 + 8 + 8 + 256 + 65536 + 128 + 4;
    std::vector<unsigned char> hdr(kHeaderSize);
    if (!ReadBytes(f, hdr.data(), kHeaderSize)) {
        error = "文件过短，头部不完整（不是有效的 .gma）";
        return false;
    }
    if (std::memcmp(hdr.data(), "GMAD", 4) != 0) {
        error = "缺少 GMAD 签名，不是 GMA 文件";
        return false;
    }

    std::size_t o = 4;
    out.header.version = LE32(hdr.data() + o); o += 4;
    out.header.steam_id = LE64(hdr.data() + o); o += 8;
    out.header.timestamp = LE64(hdr.data() + o); o += 8;
    out.header.required_content = LE64(hdr.data() + o); o += 8;
    out.header.name = FixedString(hdr.data() + o, 256); o += 256;
    out.header.description = FixedString(hdr.data() + o, 65536); o += 65536;
    out.header.author = FixedString(hdr.data() + o, 128); o += 128;
    out.header.addon_version = LE32(hdr.data() + o); o += 4;

    out.entries.clear();
    for (;;) {
        unsigned char lenBuf[4];
        if (!ReadBytes(f, lenBuf, 4)) {
            error = "条目表读取失败（可能在条目名长度处截断）";
            return false;
        }
        const std::uint32_t nameLen = LE32(lenBuf);
        if (nameLen == 0) {
            break;  // 条目表终止符
        }
        if (nameLen > (1u << 20)) {
            error = "条目名长度异常: " + std::to_string(nameLen);
            return false;
        }
        std::string name(nameLen, '\0');
        if (!ReadBytes(f, name.data(), nameLen)) {
            error = "条目名读取失败";
            return false;
        }
        unsigned char ent[8 + 4 + 8];
        if (!ReadBytes(f, ent, sizeof(ent))) {
            error = "条目信息读取失败";
            return false;
        }
        GmaEntry e;
        e.name = std::move(name);
        e.size = LE64(ent);
        e.crc = LE32(ent + 8);
        e.offset = LE64(ent + 12);
        out.entries.push_back(std::move(e));
    }
    return true;
}

bool ExtractGmaEntry(const std::string& gmaPath, const GmaFile& gma,
                     const GmaEntry& entry, const std::string& outDir,
                     std::string& error) {
    (void)gma;  // 预留：后续可做 CRC 校验
    // 安全：拒绝绝对路径与路径穿越
    const std::string& rel = entry.name;
    if (rel.empty() || rel[0] == '/' || rel[0] == '\\' ||
        rel.find("..") != std::string::npos) {
        error = "非法条目路径: " + rel;
        return false;
    }

    std::filesystem::path dest = std::filesystem::path(outDir) / rel;
    std::error_code ec;
    if (!std::filesystem::create_directories(dest.parent_path(), ec) && ec) {
        error = "无法创建目录: " + ec.message();
        return false;
    }

    std::ifstream f(gmaPath, std::ios::binary);
    if (!f) {
        error = "无法打开 .gma: " + gmaPath;
        return false;
    }
    f.seekg(static_cast<std::streamoff>(entry.offset));
    if (!f) {
        error = "无法定位条目数据: " + rel;
        return false;
    }

    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        error = "无法写入文件: " + dest.string();
        return false;
    }

    std::vector<char> buf(1 << 16);
    std::uint64_t remaining = entry.size;
    while (remaining > 0) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buf.size()));
        if (!ReadBytes(f, buf.data(), chunk)) {
            error = "读取条目数据失败: " + rel;
            return false;
        }
        out.write(buf.data(), static_cast<std::streamsize>(chunk));
        remaining -= chunk;
    }
    return true;
}

std::size_t ExtractGmaAll(const std::string& gmaPath, const GmaFile& gma,
                          const std::string& outDir, std::string& error) {
    std::size_t ok = 0;
    for (const auto& entry : gma.entries) {
        std::string e;
        if (ExtractGmaEntry(gmaPath, gma, entry, outDir, e)) {
            ++ok;
        } else if (error.empty()) {
            error = e;
        }
    }
    return ok;
}

std::vector<std::string> ScanGmaFiles(const std::string& root) {
    std::vector<std::string> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(root, ec);
    const std::filesystem::directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        const auto& entry = *it;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".gma") {
            out.push_back(entry.path().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace modhub
