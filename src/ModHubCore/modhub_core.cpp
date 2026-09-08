#include "modhub_core.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace modhub {
namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ContainsAny(const std::string& haystack, const std::vector<const char*>& needles) {
    for (const char* n : needles) {
        if (haystack.find(n) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string ReadText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

bool HasGameInfo(const std::string& dirPath) {
    return std::filesystem::is_regular_file(
        std::filesystem::path(dirPath) / "gameinfo.txt");
}

std::string DetectEngine(const std::string& text) {
    const std::string low = ToLower(text);
    struct Marker {
        const char* engine;
        std::vector<const char*> needles;
    };
    static const Marker kMarkers[] = {
        {"gmod",     {"garrysmod", "gmod"}},
        {"css",      {"cstrike", "counter-strike source"}},
        {"tf2",      {"team fortress", "tf\\"}},
        {"portal",   {"portal"}},
        {"ep2",      {"ep2", "episode two"}},
        {"episodic", {"episodic", "episode one"}},
        {"hl2",      {"hl2", "half-life 2"}},
    };
    for (const auto& m : kMarkers) {
        if (ContainsAny(low, m.needles)) {
            return m.engine;
        }
    }
    return "unknown";
}

std::string ParseSteamAppId(const std::string& text) {
    const std::string low = ToLower(text);
    std::size_t pos = 0;
    while ((pos = low.find("steamappid", pos)) != std::string::npos) {
        std::size_t tail = pos + 10;  // len("steamappid")
        while (tail < text.size() && (text[tail] == '"' ||
                                      text[tail] == ' ' || text[tail] == '\t')) {
            ++tail;
        }
        std::size_t start = tail;
        while (tail < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[tail]))) {
            ++tail;
        }
        if (tail > start) {
            return text.substr(start, tail - start);
        }
        pos = tail;
    }
    return "";
}

std::vector<ModEntry> ScanMods(const std::string& root) {
    std::vector<ModEntry> out;
    std::error_code ec;
    const std::filesystem::path rootPath(root);
    if (!std::filesystem::is_directory(rootPath, ec)) {
        return out;
    }

    std::filesystem::directory_iterator it(rootPath, ec);
    const std::filesystem::directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        const auto& entry = *it;
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string dirPath = entry.path().string();
        const std::filesystem::path giPath = entry.path() / "gameinfo.txt";

        ModEntry mod;
        mod.name = entry.path().filename().string();
        mod.path = dirPath;

        if (!std::filesystem::is_regular_file(giPath, ec)) {
            mod.engine = "unknown";
            mod.valid = false;
            mod.issues.emplace_back("缺少 gameinfo.txt，不是有效的起源引擎模组");
            out.push_back(std::move(mod));
            continue;
        }

        const std::string text = ReadText(giPath);
        mod.engine = DetectEngine(text);
        const std::string appid = ParseSteamAppId(text);
        if (appid.empty()) {
            mod.valid = false;
            mod.issues.emplace_back("gameinfo.txt 中未找到 SteamAppId");
        }
        if (mod.engine == "unknown") {
            mod.issues.emplace_back("未能识别引擎类型，将按 HL2 处理");
        }
        out.push_back(std::move(mod));
    }

    std::sort(out.begin(), out.end(),
              [](const ModEntry& a, const ModEntry& b) { return a.name < b.name; });
    return out;
}

std::string BuildLaunchArgs(const ModEntry& mod) {
    // 起源引擎 sourcemods 模组统一用 -game <目录名> 启动
    return "-game \"" + mod.name + "\"";
}

}  // namespace modhub
