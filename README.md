# HL2 Mod Hub

**一个真正的《半条命2》起源引擎模组**：在引擎内解析、登记并运行模组文件夹中的所有适用模组（HL2 / GMod / CS:S），内容层可在 **Windows / Linux / 安卓起源引擎**上直接运行。

```
mod_hub/                     ← 可部署的模组文件夹（核心交付物）
├── gameinfo.txt            引擎入口（挂载 HL2 内容，Steam 与 -game 均可识别）
├── cfg/autoexec.cfg        启动即执行的引擎内引导
├── cfg/modhub_mods.cfg     模组登记表（引擎内控制台管理其他模组）
├── mod/                    [GMod 附加组件存放处] 把任意 .gma 丢进来
├── mod_unpacked/           启动时自动解包输出（引擎自动挂载进游戏）
├── maps/mod_hub.vmf        中心大厅地图源文件（Hammer 编译为 .bsp）
└── README-MOD.txt          放进游戏后的使用说明
src/
├── ModHubCore/             C++ 模组解析核心（纯标准库，CI 三目标编译+测试）
│   ├── modhub_core.{h,cpp} 模组目录 / gameinfo.txt 解析
│   ├── gma_parser.{h,cpp}  GMod 附加组件 (.gma) 容器解析与内容提取（经典 v1/v2 + 现代 v3 双布局，逐条目 CRC32 校验）
│   ├── lua_vm.{h,cpp}      Lua 5.1 虚拟机（GLua 兼容层 + 沙箱 + autorun/include）
│   └── lua/                官方 Lua 5.1.5 运行时源码（MIT，含 COPYRIGHT）
├── ModHubEngine/           模组内置 GMA 自动加载器（gma_loader，纯 C++）+ 引擎钩子（engine_gma_hook，需授权 SDK 编译）
├── AMLMod/                 安卓注入模组（路线 C）：AndroidModLoader .so，不用重编引擎，CI 云编译 arm64-v8a + armeabi-v7a
docs/                       Windows / Linux / Android 安装指南
```

## 安卓三路线（详见 docs/install-android.md 与 docs/install-android-aml.md）

| 路线 | 门槛 | 自动解包 .gma？ |
| --- | --- | --- |
| A. custom 内容挂载 | 零编译（实测可用） | ❌ 只挂载已解包内容 |
| B. 编译引擎 + 内置加载器 | 引擎源码 + NDK 重编 APK | ✅ |
| C. AML 注入模组（本仓库云编译 .so） | 一次性给 APK 集成 AML，然后丢 .so | ✅ **不用重编引擎** |

## 它如何工作（三层架构）

| 层 | 内容 | 运行方式 |
| --- | --- | --- |
| **内容层** | gameinfo.txt + cfg + 地图 | 任何 HL2 引擎构建直接运行，**含安卓起源引擎**（`-game mod_hub` 或 `hl2/custom` 挂载） |
| **模组内置加载器** | `gma_loader`（纯 C++）| 编进 client 模块后，启动时自动扫描 `mod/*.gma` → 解包 → 挂载进游戏，**无需 Termux/外部工具** |
| **引擎层** | 引擎钩子 `engine_gma_hook` + VGUI2 面板 | 编译进 client 模块后，游戏内自动挂载 + 按键弹出模组列表 |

引擎内"解析"由 `src/ModHubCore/` 实现，支持三类输入：
- **模组目录**：扫描 sourcemods 类目录，解析 gameinfo.txt，识别引擎（HL2/GMod/CS:S/TF2/Portal），校验 SteamAppId；
- **GMod 附加组件 (.gma)**：解析 GMA 容器头部与条目表，把地图 / 模型 / 材质 / 脚本**提取**出来供 HL2 等其他 Source 游戏使用（与 gmad / SharpGMad 等社区工具同类能力，纯 C++ 实现）。自动识别两种布局并逐条目校验 CRC32：
  - **经典 v1/v2**（Valve Wiki 文档格式：定长头部 + `[名字长度 u32][名字][大小 u64][CRC u32][偏移 u64]` 条目表）；
  - **现代 v3**（当前 gmad 写入格式：单字节版本 + 空终止字符串头部 + `[序号 u32][路径\0][大小 u64][CRC u32]` 条目表、无偏移字段、数据区紧贴表尾）。解析结果带 `format`（`v1-classic`/`v2-classic`/`v3-modern`）与 `crc_ok` 字段，CRC 未过时仍返回结构并标注。
- **Lua 脚本 (.lua)**：内置 Lua 5.1 虚拟机，可执行 .gma 解包出的 `lua/autorun/*.lua` 与 `include()` 脚本（见下节）。

它是纯 C++17 标准库 + 官方 Lua 5.1.5（MIT），不依赖引擎头文件，因此可以在任意平台独立编译与测试。

## 模组内置 GMA 自动加载器（不需要 Termux）

`src/ModHubEngine/gma_loader.{h,cpp}`（纯 C++17，CI 三目标编译+18 项测试）把"解析 .gma"直接做进模组本体：

1. 把任意 GMod 附加组件 `.gma` 丢进模组文件夹的 **`mod/`** 目录；
2. 游戏启动时（引擎钩子 `engine_gma_hook`，编进 client 模块后自动调用，也可控制台 `modhub_scan` 手动触发）扫描 `mod/*.gma`；
3. 逐个解析 + CRC32 校验，解包到 `mod_unpacked/<gma名>/`，再把解包目录 `AddSearchPath` 挂进 GAME 路径——模型 / 材质 / 地图**立即在游戏内可用**；
4. 幂等缓存：文件未变则二次启动直接命中缓存跳过解包（manifest 记录大小 + mtime）。

引擎钩子需在 Source SDK 2013 工程中编译（见 `src/ModHubEngine/README-engine.txt`）；解析与解包逻辑本身已在本仓库 CI 的 x64 / ARM64 / Windows 上编译并测试通过。

**安卓免重编引擎版本（路线 C）**：`src/AMLMod/` 把同一套加载逻辑打包成 AndroidModLoader 注入模组（`.so`），CI 用 Android NDK 云编译 `arm64-v8a` + `armeabi-v7a` 两个架构（Release 附件 `modhub_aml`）。给起源引擎 APK 集成一次 AML 后，把 `libmodhub_aml.so` 放进 AML 的 mods 目录，即可让 `hl2/custom/*/mod/*.gma` 与 `hl2/mod/*.gma` 在启动时自动解包挂载（详见 `docs/install-android-aml.md`）。

## Lua 虚拟机

`src/ModHubCore/lua_vm.{h,cpp}` 提供一个可直接嵌入的 Lua 5.1 运行时——与 GMod 的 GLua 同一语言核心（GMod 实际使用 LuaJIT 2.x，语义兼容）：

- **GLua 兼容层**：`print` / `Msg` / `MsgN` / `Color(r,g,b,a)` / `include(relpath)` / `SysTime`；
- **autorun 惯例**：`RunAutorun(addonRoot)` 按文件名排序执行 `lua/autorun/*.lua`（GMod 启动加载附加组件脚本的路径约定）；
- **GLua 引擎 API 存根（登记式，不模拟引擎行为）**：`player_manager.AddValidModel/AddValidHands`、`list.Set/Get/Add`、`hook.Add/Remove`、`util.PrecacheModel/PrecacheSound`、`AddCSLuaFile`、`Vector/Angle`。存根把脚本声明的模型 / 列表登记进全局表 `modhub_registry`（分区：`playermodels` / `hands` / `lists`），宿主通过 `RegistryCount` / `RegistryField` / `RegistryListField` / `RegistryReset` 读取——即"解析附加组件声明了什么"，而不假装能跑 GMod 逻辑；
- **沙箱**：移除 `os.execute` / `os.exit` / `os.remove` / `os.rename` / `io` / `loadfile` / `dofile` —— 脚本只能通过 `include()` 访问模组目录内文件，不能读写任意路径；
- **嵌入 API**：`RunString` / `RunFile` / 全局变量读写，宿主（引擎内面板或独立工具）可把 VM 挂到自己的事件循环上。

**真实附加组件实证**（`mod_scanner_test.cpp` 含回归测试；另用真实 Workshop .gma 验证过）：一份 33 条目、含 PM/NPC 模型 + VTF 材质 + `lua/autorun` 脚本的现代 v3 附加组件（约 24MB），被完整解析（33/33 CRC32 匹配）、脚本在 VM 中运行并登记出 `playermodels[1]=Tomorin`、`hands[1]`、`NPC[eddie_tomorin_friendly]=npc_citizen`、`NPC[eddie_tomorin_enemy]=npc_combine_s`。

**如实声明的边界**：本 VM 提供 Lua 5.1 语言运行时 + GLua 基础函数 + 引擎 API 存根（登记式）。GMod 的引擎行为（`ents` / `hook` 调度 / `net` / `player` 等）依赖 GMod 引擎本身，不在本 VM 内。需要完整 GLua 行为的附加组件仍必须由 GMod 运行；本 VM 面向**纯逻辑脚本**（配置、计算、数据脚本）、"解包 → 运行其 autorun → 登记其声明（模型/NPC 等）"的自动化，以及把登记结果交给引擎层做后续处理。

## 快速开始

- **Windows**：复制 `mod_hub/` 到 `steamapps/sourcemods/` → 重启 Steam → 运行 HL2 Mod Hub（详见 `docs/install-windows.md`）
- **Linux**：复制到 `~/.steam/steam/steamapps/sourcemods/` → `hl2_linux -game mod_hub`（详见 `docs/install-linux.md`）
- **Android（实测可用）**：复制 `mod_hub/` 到 `/sdcard/srceng/hl2/custom/mod_hub/`，用**原版 HL2** 启动即自动挂载内容（该移植版不靠 `-game` 切模组，详见 `docs/install-android.md`）

## 技术边界（务必阅读）

1. **起源引擎的每个模组是独立程序**：GMod 附加组件只能由 Garry's Mod 加载，CS:S 内容只能由 CS:S 加载。引擎运行中**无法**直接切换到另一个模组进程——C++ 层实现的是"列出 + 用对应引擎启动新进程"，这是架构上可行的完整功能。
2. **安卓引擎现状**：安卓起源引擎（nillerusr 移植版）基于 2020 泄漏的 Source 2017 源码，可运行本模组的内容层；本仓库**不提供、不构建**该引擎二进制，引擎内 C++ 面板需自行在安卓引擎工程中编译（`ANDROID` 宏已处理平台差异）。
3. **中心大厅地图**：`maps/mod_hub.vmf` 由仓库内的 `tools/gen_vmf.py` 生成，需在 Hammer (Source SDK 2013) 中编译为 `.bsp` 后使用。

## 如何获取编译产物

| 想要什么 | 去哪里拿 | 说明 |
| --- | --- | --- |
| **模组内容层** `mod_hub/` | Release 里的 `mod_hub-content.zip`（或直接克隆仓库） | **不需要编译**：解压放进 PC `steamapps/sourcemods/` 或安卓 `/sdcard/srceng/` 即可运行 |
| **modhub_cli 工具** | Release 里的产物，或 Actions → core-build → 最新运行 → Artifacts | 静态链接单文件，无需安装依赖 |
| 源码 | GitHub 仓库 | `src/` 全部 C++ 源码 + 官方 Lua 5.1.5 |

三个平台产物（每次 CI 自动生成，打 `v*` tag 自动发布 Release）：

- `modhub_cli-linux-x64` — Linux x64（静态）
- `modhub_cli-linux-arm64` — **ARM64 静态链接，可在安卓 Termux / 树莓派等 ARM Linux 直接运行**（Termux 内 `chmod +x modhub_cli-linux-arm64 && ./modhub_cli`）
- `modhub_cli-windows-x64.exe` — Windows x64（静态单文件）

> 引擎内 VGUI2 面板（client.dll）需在你的授权 Source SDK 2013 工程中编译，仓库只提供源码。

## 构建与验证（GitHub Actions 云编译）

- `.github/workflows/core-build.yml`：在 **Linux x64（编译+运行测试）/ Linux ARM64（交叉编译）/ Windows x64（MinGW 交叉编译）** 三个目标上构建并验证模组解析核心，上传 `modhub_cli` 产物、打包内容层，打 `v*` tag 时自动发布 Release；
- `.github/workflows/validate-mod.yml`：校验模组文件夹结构与 gameinfo.txt 格式。

本地快速验证（Lua 运行时用 gcc 按 C 编译，其余按 C++ 链接）：

```bash
mkdir -p /tmp/luaobjs
for f in src/ModHubCore/lua/*.c; do
  case "$f" in *lua.c|*luac.c|*print.c) continue ;; esac
  gcc -std=c99 -O2 -w -c "$f" -o "/tmp/luaobjs/$(basename "${f%.c}").o"
done
g++ -std=c++17 -O2 -Wall -Wextra \
    src/ModHubCore/mod_scanner_test.cpp src/ModHubCore/modhub_core.cpp \
    src/ModHubCore/gma_parser.cpp src/ModHubCore/lua_vm.cpp \
    /tmp/luaobjs/*.o -lm -o modhub_test && ./modhub_test
```

## 许可证

MIT License（含官方 Lua 5.1.5，版权归 Lua.org / PUC-Rio，见 `src/ModHubCore/lua/COPYRIGHT`）。与 Valve 无关的独立项目；Half-Life 2 / Garry's Mod / Counter-Strike 均为其各自权利人的商标。安卓引擎为第三方基于泄漏源码的移植，使用与分发请自行评估法律风险。
