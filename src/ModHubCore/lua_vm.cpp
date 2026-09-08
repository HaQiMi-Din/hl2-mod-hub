#include "lua_vm.h"

// Lua 5.1 的 lua.h 无 extern "C" 守卫，C++ 侧必须手动包裹
extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace modhub {
namespace {

// ---- GLua 兼容 shim ----

// print / Msg / MsgN：输出到 stdout（游戏内为控制台）
static int l_Print(lua_State* L) {
    const int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += " ";
        const char* s = lua_tostring(L, i);
        if (s) {
            out += s;
        } else {
            out += "<";
            out += luaL_typename(L, i);
            out += ">";
        }
    }
    out += "\n";
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

static int l_Msg(lua_State* L) {
    std::string out;
    for (int i = 1; i <= lua_gettop(L); ++i) {
        const char* s = lua_tostring(L, i);
        if (s) out += s;
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

// Color(r, g, b, a) -> {r=, g=, b=, a=}
static int l_Color(lua_State* L) {
    lua_createtable(L, 0, 4);
    const char* fields[4] = {"r", "g", "b", "a"};
    for (int i = 0; i < 4; ++i) {
        lua_pushinteger(L, luaL_checkinteger(L, i + 1));
        lua_setfield(L, -2, fields[i]);
    }
    return 1;
}

// include(relpath)：加载并执行 addon 目录内脚本（GLua 惯例）
static int l_Include(lua_State* L) {
    LuaVm* vm = static_cast<LuaVm*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* rel = luaL_checkstring(L, 1);
    const std::filesystem::path p =
        std::filesystem::path(vm->RootDir()) / rel;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        return luaL_error(L, "include: 无法找到文件 %s", rel);
    }
    if (luaL_loadfile(L, p.string().c_str()) != 0) {
        return luaL_error(L, "include: %s", lua_tostring(L, -1));
    }
    if (lua_pcall(L, 0, 0, 0) != 0) {
        return luaL_error(L, "include: %s", lua_tostring(L, -1));
    }
    return 0;
}

// 沙箱化 os：只保留 time/clock/date 等无害函数
void SandboxOS(lua_State* L) {
    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        const char* drop[] = {"execute", "exit",   "remove", "rename",
                              "getenv",  "tmpname", "setlocale", nullptr};
        for (int i = 0; drop[i]; ++i) {
            lua_pushnil(L);
            lua_setfield(L, -2, drop[i]);
        }
    }
    lua_pop(L, 1);
}

// 移除任意文件访问入口
void SandboxFiles(lua_State* L) {
    const char* nil[] = {"io", "loadfile", "dofile", "loadlib", nullptr};
    for (int i = 0; nil[i]; ++i) {
        lua_pushnil(L);
        lua_setglobal(L, nil[i]);
    }
}

std::string PopErr(lua_State* L) {
    std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1)
                                          : "未知 Lua 错误";
    lua_pop(L, 1);
    return msg;
}

}  // namespace

LuaVm::LuaVm() : L_(nullptr), root_(".") {
    L_ = luaL_newstate();
    if (!L_) {
        return;
    }
    luaL_openlibs(L_);

    // print / Msg / MsgN（共享同一个闭包）
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_Print, 1);
    lua_setglobal(L_, "print");
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_Print, 1);
    lua_setglobal(L_, "MsgN");
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_Msg, 1);
    lua_setglobal(L_, "Msg");

    // Color
    lua_pushcfunction(L_, l_Color);
    lua_setglobal(L_, "Color");

    // include（以 VM 为 upvalue，取基准目录）
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_Include, 1);
    lua_setglobal(L_, "include");

    // SysTime：宿主时间（秒）
    lua_pushcfunction(
        L_, [](lua_State* L) {
            lua_pushnumber(L, static_cast<lua_Number>(
                                  std::clock()) / CLOCKS_PER_SEC);
            return 1;
        });
    lua_setglobal(L_, "SysTime");

    SandboxOS(L_);
    SandboxFiles(L_);
}

LuaVm::~LuaVm() {
    if (L_) {
        lua_close(L_);
    }
}

void LuaVm::SetRootDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::path p(dir);
    if (std::filesystem::is_directory(p, ec)) {
        root_ = std::filesystem::absolute(p, ec).string();
        if (ec) root_ = dir;
    } else {
        root_ = dir;
    }
}

bool LuaVm::RunString(const std::string& code, const std::string& chunkname,
                      std::string& err) {
    if (!L_) {
        err = "Lua 状态创建失败";
        return false;
    }
    if (luaL_loadbuffer(L_, code.data(), code.size(),
                        chunkname.empty() ? "=(modhub)" : chunkname.c_str()) !=
        0) {
        err = PopErr(L_);
        return false;
    }
    if (lua_pcall(L_, 0, 0, 0) != 0) {
        err = PopErr(L_);
        return false;
    }
    return true;
}

bool LuaVm::RunFile(const std::string& path, std::string& err) {
    if (!L_) {
        err = "Lua 状态创建失败";
        return false;
    }
    if (luaL_loadfile(L_, path.c_str()) != 0) {
        err = PopErr(L_);
        return false;
    }
    if (lua_pcall(L_, 0, 0, 0) != 0) {
        err = PopErr(L_);
        return false;
    }
    return true;
}

std::int64_t LuaVm::GetGlobalInt(const char* name) const {
    lua_getglobal(L_, name);
    const std::int64_t v = static_cast<std::int64_t>(lua_tointeger(L_, -1));
    lua_pop(L_, 1);
    return v;
}

void LuaVm::SetGlobalInt(const char* name, std::int64_t v) {
    lua_pushinteger(L_, static_cast<lua_Integer>(v));
    lua_setglobal(L_, name);
}

std::string LuaVm::GetGlobalString(const char* name) const {
    lua_getglobal(L_, name);
    const char* s = lua_tostring(L_, -1);
    std::string out = s ? s : "";
    lua_pop(L_, 1);
    return out;
}

std::int64_t LuaVm::GetGlobalFieldInt(const char* table,
                                      const char* field) const {
    lua_getglobal(L_, table);
    std::int64_t v = 0;
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, field);
        v = static_cast<std::int64_t>(lua_tointeger(L_, -1));
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);
    return v;
}

std::vector<std::string> RunAutorun(LuaVm& vm, const std::string& addonRoot,
                                    std::string& err) {
    std::vector<std::string> ran;
    const std::filesystem::path dir =
        std::filesystem::path(addonRoot) / "lua" / "autorun";
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    std::filesystem::directory_iterator it(dir, ec);
    const std::filesystem::directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".lua") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::string e;
        if (vm.RunFile(f.string(), e)) {
            ran.push_back(f.filename().string());
        } else {
            err = f.filename().string() + ": " + e;
            break;
        }
    }
    return ran;
}

std::string LuaVmVersion() {
    return "Lua " LUA_RELEASE " + GLua 兼容层 v1 (modhub)";
}

}  // namespace modhub
