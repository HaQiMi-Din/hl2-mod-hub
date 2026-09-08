// Lua 虚拟机封装 - 在模组解析核心内运行 .gma 附加组件解包出的 Lua 脚本
//
// 与 GMod 的 GLua 同语言核心（Lua 5.1），提供两层 GLua 兼容能力：
//
// 1) 基础兼容层（可实际执行）：
//    print / Msg / MsgN / Color / include / SysTime / Vector / Angle
//
// 2) 引擎 API 存根层（加载不报错、登记不模拟）：
//    player_manager.AddValidModel/AddValidHands
//    list.Set/Get/Add
//    hook.Add/Remove
//    util.PrecacheModel/PrecacheSound
//    AddCSLuaFile
//    存根把调用登记进全局表 modhub_registry（playermodels / hands /
//    lists），宿主可通过 Registry* 接口读取 —— 用于"解析附加组件声明
//    了什么"，而不是模拟 GMod 的引擎行为。
//
// 沙箱化：移除 os.execute / os.exit / os.remove / os.rename / io /
//         loadfile / dofile —— 脚本只能通过 include() 访问模组目录内文件。
//
// 边界说明（如实声明）：
//   - 本 VM 提供 Lua 5.1 语言运行时、GLua 基础函数与常见引擎 API 存根；
//   - GMod 引擎的真实行为（实体生成、网络、钩子调度）只存在于 GMod
//     引擎内 —— 依赖完整 GLua API 的附加组件仍须由 GMod 运行；
//     本 VM 的目标是让"纯逻辑脚本"能跑、让"附加组件声明"能被解析登记。
//   - GMod 实际使用 LuaJIT 2.x；本 VM 为官方 Lua 5.1（语义兼容核心）。
//
// 纯 C++17 + 官方 Lua 5.1.5（MIT），无引擎依赖，三目标 CI 编译。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace modhub {

class LuaVm {
public:
    LuaVm();
    ~LuaVm();

    LuaVm(const LuaVm&) = delete;
    LuaVm& operator=(const LuaVm&) = delete;

    // 设置 include() 的基准目录（通常为解包后的 addon 目录）
    void SetRootDir(const std::string& dir);
    const std::string& RootDir() const { return root_; }

    // 执行一段 Lua 代码（chunkname 仅用于错误信息）
    bool RunString(const std::string& code, const std::string& chunkname,
                   std::string& err);
    // 加载并执行一个 Lua 文件
    bool RunFile(const std::string& path, std::string& err);

    // 读取/写入全局变量（供宿主与测试使用）
    std::int64_t GetGlobalInt(const char* name) const;
    void SetGlobalInt(const char* name, std::int64_t v);
    std::string GetGlobalString(const char* name) const;
    std::int64_t GetGlobalFieldInt(const char* table, const char* field) const;

    // ---- GLua 存根登记查询 ----
    // section: "playermodels" | "hands"（数组，1 起）
    std::size_t RegistryCount(const char* section) const;
    std::string RegistryField(const char* section, std::size_t index,
                              const char* field) const;
    // lists 区: list.Set(listname, key, value) 登记的条目
    std::string RegistryListField(const char* listname, const char* key,
                                  const char* field) const;
    // 清空登记表（重新加载前调用）
    void RegistryReset();

    lua_State* State() { return L_; }

private:
    void InitRegistry();
    lua_State* L_;
    std::string root_;
};

// 按 GMod 惯例执行 addon 目录下 lua/autorun/*.lua（按文件名排序）。
// 返回成功执行的脚本文件名；遇错时在 err 中说明并停止。
std::vector<std::string> RunAutorun(LuaVm& vm, const std::string& addonRoot,
                                    std::string& err);

// VM 版本信息（Lua 版本 + GLua 兼容层版本）
std::string LuaVmVersion();

}  // namespace modhub
