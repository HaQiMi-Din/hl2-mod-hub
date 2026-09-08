#include "gma_parser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

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

// 读取一个空终止字符串（含终止符），上限 64MB
bool ReadNullTerm(std::ifstream& f, std::string& out, std::string& err) {
    out.clear();
    char c;
    while (f.get(c)) {
        if (c == '\0') {
            return true;
        }
        out += c;
        if (out.size() > (1u << 26)) {
            err = "字符串过长（超过 64MB），文件可能损坏";
            return false;
        }
    }
    err = "字符串未找到终止符";
    return false;
}

// CRC32 (IEEE 802.3, 与 zlib crc32 一致)
std::uint32_t Crc32(const unsigned char* data, std::size_t len) {
    static const std::array<std::uint32_t, 256> kTable = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c = kTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

// 逐条目校验 CRC32（crc==0 视为未知，跳过）
bool VerifyCrc(const std::string& path, GmaFile& gma) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    std::vector<unsigned char> data;
    for (const auto& e : gma.entries) {
        if (e.crc == 0 || e.size == 0) {
            continue;
        }
        f.clear();
        f.seekg(static_cast<std::streamoff>(e.offset));
        if (!f) {
            return false;
        }
        data.resize(static_cast<std::size_t>(e.size));
        if (!ReadBytes(f, data.data(), data.size())) {
            return false;
        }
        if (Crc32(data.data(), data.size()) != e.crc) {
            return false;
        }
    }
    return true;
}

// ---- 现代布局（当前 gmad 写入格式，SharpGMad 读取器兼容）----
bool ParseModern(const std::string& path, GmaFile& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "无法打开文件: " + path;
        return false;
    }

    unsigned char sig[5];
    if (!ReadBytes(f, sig, sizeof(sig))) {
        err = "文件过短，头部不完整";
        return false;
    }
    if (std::memcmp(sig, "GMAD", 4) != 0) {
        err = "缺少 GMAD 签名，不是 GMA 文件";
        return false;
    }
    const std::uint32_t version = sig[4];
    if (version == 0 || version > 3) {
        err = "不支持的 GMA 版本: " + std::to_string(version);
        return false;
    }

    unsigned char b[16];
    if (!ReadBytes(f, b, sizeof(b))) {
        err = "头部字段读取失败";
        return false;
    }
    out.header.version = version;
    out.header.steam_id = LE64(b);
    out.header.timestamp = LE64(b + 8);

    if (version >= 2) {
        // required content: 空终止字符串列表，空串结束
        std::string s;
        do {
            if (!ReadNullTerm(f, s, err)) {
                return false;
            }
        } while (!s.empty());
    }

    if (!ReadNullTerm(f, out.header.name, err)) return false;
    if (!ReadNullTerm(f, out.header.description, err)) return false;
    if (!ReadNullTerm(f, out.header.author, err)) return false;

    unsigned char ver4[4];
    if (!ReadBytes(f, ver4, sizeof(ver4))) {
        err = "addon 版本读取失败";
        return false;
    }
    out.header.addon_version = LE32(ver4);

    // 条目表: [序号 u32][路径\0][大小 u64][crc u32] ... 序号 0 结束
    out.entries.clear();
    std::uint64_t prevSeq = 0;
    std::uint64_t relOffset = 0;
    std::vector<std::uint64_t> rels;
    for (;;) {
        unsigned char seqb[4];
        if (!ReadBytes(f, seqb, sizeof(seqb))) {
            err = "条目表读取失败（条目序号处截断）";
            return false;
        }
        const std::uint64_t seq = LE32(seqb);
        if (seq == 0) {
            break;
        }
        if (seq <= prevSeq) {
            err = "条目序号不递增（不是现代布局或文件损坏）";
            return false;
        }
        prevSeq = seq;

        std::string name;
        if (!ReadNullTerm(f, name, err)) {
            return false;
        }
        unsigned char ent[12];
        if (!ReadBytes(f, ent, sizeof(ent))) {
            err = "条目信息读取失败";
            return false;
        }
        GmaEntry e;
        e.name = std::move(name);
        e.size = LE64(ent);
        e.crc = LE32(ent + 8);
        e.offset = relOffset;  // 相对表尾，稍后修正
        rels.push_back(relOffset);
        relOffset += e.size;
        out.entries.push_back(std::move(e));
    }

    if (out.entries.empty()) {
        err = "条目表为空";
        return false;
    }

    const std::uint64_t tableEnd = static_cast<std::uint64_t>(f.tellg());
    for (std::size_t i = 0; i < out.entries.size(); ++i) {
        out.entries[i].offset = tableEnd + rels[i];
    }

    // 数据区边界检查（允许尾部 ≤64 字节冗余）
    std::error_code ec;
    const std::uint64_t fileSize =
        std::filesystem::file_size(std::filesystem::path(path), ec);
    const std::uint64_t dataEnd = tableEnd + relOffset;
    if (!ec && (dataEnd > fileSize || fileSize - dataEnd > 64)) {
        err = "数据区与文件大小不吻合（文件损坏或非现代布局）";
        return false;
    }

    out.format = (version >= 3 ? "v3-modern" : "v2-modern");
    return true;
}

// ---- 经典布局（Valve Wiki 文档格式）----
bool ParseClassic(const std::string& path, GmaFile& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "无法打开文件: " + path;
        return false;
    }

    constexpr std::size_t kHeaderSize =
        4 + 4 + 8 + 8 + 8 + 256 + 65536 + 128 + 4;
    std::vector<unsigned char> hdr(kHeaderSize);
    if (!ReadBytes(f, hdr.data(), kHeaderSize)) {
        err = "文件过短，头部不完整（经典布局）";
        return false;
    }
    if (std::memcmp(hdr.data(), "GMAD", 4) != 0) {
        err = "缺少 GMAD 签名，不是 GMA 文件";
        return false;
    }

    std::size_t o = 4;
    const std::uint32_t version = LE32(hdr.data() + o); o += 4;
    if (version == 0 || version > 3) {
        err = "不支持的 GMA 版本: " + std::to_string(version);
        return false;
    }
    out.header.version = version;
    out.header.steam_id = LE64(hdr.data() + o); o += 8;
    out.header.timestamp = LE64(hdr.data() + o); o += 8;
    if (version >= 2) {
        out.header.required_content = LE64(hdr.data() + o); o += 8;
    }
    out.header.name = FixedString(hdr.data() + o, 256); o += 256;
    out.header.description = FixedString(hdr.data() + o, 65536); o += 65536;
    out.header.author = FixedString(hdr.data() + o, 128); o += 128;
    out.header.addon_version = LE32(hdr.data() + o); o += 4;

    out.entries.clear();
    for (;;) {
        unsigned char lenBuf[4];
        if (!ReadBytes(f, lenBuf, 4)) {
            err = "条目表读取失败（可能在条目名长度处截断）";
            return false;
        }
        const std::uint32_t nameLen = LE32(lenBuf);
        if (nameLen == 0) {
            break;
        }
        if (nameLen > (1u << 20)) {
            err = "条目名长度异常: " + std::to_string(nameLen);
            return false;
        }
        std::string name(nameLen, '\0');
        if (!ReadBytes(f, name.data(), nameLen)) {
            err = "条目名读取失败";
            return false;
        }
        unsigned char ent[8 + 4 + 8];
        if (!ReadBytes(f, ent, sizeof(ent))) {
            err = "条目信息读取失败";
            return false;
        }
        GmaEntry e;
        e.name = std::move(name);
        e.size = LE64(ent);
        e.crc = LE32(ent + 8);
        e.offset = LE64(ent + 12);
        out.entries.push_back(std::move(e));
    }

    if (out.entries.empty()) {
        err = "条目表为空";
        return false;
    }

    // 偏移边界检查
    std::error_code ec;
    const std::uint64_t fileSize =
        std::filesystem::file_size(std::filesystem::path(path), ec);
    if (!ec) {
        for (const auto& e : out.entries) {
            if (e.offset + e.size > fileSize) {
                err = "条目偏移越界（文件损坏或非经典布局）";
                return false;
            }
        }
    }

    out.format = (version >= 3 ? "v3-classic" : version == 2 ? "v2-classic"
                                                             : "v1-classic");
    return true;
}

}  // namespace

bool ParseGma(const std::string& path, GmaFile& out, std::string& error) {
    GmaFile modern;
    std::string modernErr;
    const bool modernOk = ParseModern(path, modern, modernErr);
    if (modernOk && VerifyCrc(path, modern)) {
        out = std::move(modern);
        out.crc_ok = true;
        return true;
    }

    GmaFile classic;
    std::string classicErr;
    const bool classicOk = ParseClassic(path, classic, classicErr);
    if (classicOk && VerifyCrc(path, classic)) {
        out = std::move(classic);
        out.crc_ok = true;
        return true;
    }

    // 两种布局均未能通过 CRC 校验：优先返回结构可读的一方并标注
    if (modernOk) {
        out = std::move(modern);
        out.crc_ok = false;
        error = "CRC 校验未通过（文件可能损坏）: " + modernErr;
        return true;
    }
    if (classicOk) {
        out = std::move(classic);
        out.crc_ok = false;
        error = "CRC 校验未通过（文件可能损坏）: " + classicErr;
        return true;
    }
    error = "无法解析为任何已知 GMA 布局（现代: " + modernErr +
            "; 经典: " + classicErr + "）";
    return false;
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
