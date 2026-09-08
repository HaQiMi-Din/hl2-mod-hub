# HL2 Mod Hub

**一个真正的《半条命2》起源引擎模组**：在引擎内解析、登记并运行模组文件夹中的所有适用模组（HL2 / GMod / CS:S），内容层可在 **Windows / Linux / 安卓起源引擎**上直接运行。

```
mod_hub/                     ← 可部署的模组文件夹（核心交付物）
├── gameinfo.txt            引擎入口（挂载 HL2 内容，Steam 与 -game 均可识别）
├── cfg/autoexec.cfg        启动即执行的引擎内引导
├── cfg/modhub_mods.cfg     模组登记表（引擎内控制台管理其他模组）
├── maps/mod_hub.vmf        中心大厅地图源文件（Hammer 编译为 .bsp）
└── README-MOD.txt          放进游戏后的使用说明
src/
├── ModHubCore/             C++ 模组解析核心（纯标准库，CI 三目标编译+测试）
└── ModHubEngine/           VGUI2 引擎内启动面板（需在授权 SDK 工程中编译）
docs/                       Windows / Linux / Android 安装指南
```

## 它如何工作（两层架构）

| 层 | 内容 | 运行方式 |
| --- | --- | --- |
| **内容层** | gameinfo.txt + cfg + 地图 | 任何 HL2 引擎构建直接运行，**含安卓起源引擎**（`-game mod_hub` 或放入模组目录） |
| **引擎层** | C++ VGUI2 面板（`src/ModHubEngine/`） | 编译进 client 模块后，游戏内按键弹出模组列表，选中即用对应引擎启动 |

引擎内"解析"由 `src/ModHubCore/` 实现：扫描模组目录、解析 gameinfo.txt、识别引擎（HL2/GMod/CS:S/TF2/Portal）、校验 SteamAppId。它是纯 C++17 标准库，不依赖引擎头文件，因此可以在任意平台独立编译与测试。

## 快速开始

- **Windows**：复制 `mod_hub/` 到 `steamapps/sourcemods/` → 重启 Steam → 运行 HL2 Mod Hub（详见 `docs/install-windows.md`）
- **Linux**：复制到 `~/.steam/steam/steamapps/sourcemods/` → `hl2_linux -game mod_hub`（详见 `docs/install-linux.md`）
- **Android**：复制 `mod_hub/` 到安卓起源引擎（nillerusr 移植版）的内部存储模组目录（默认 `/sdcard/srceng/`），在主界面选择模组（详见 `docs/install-android.md`）

## 技术边界（务必阅读）

1. **起源引擎的每个模组是独立程序**：GMod 附加组件只能由 Garry's Mod 加载，CS:S 内容只能由 CS:S 加载。引擎运行中**无法**直接切换到另一个模组进程——C++ 层实现的是"列出 + 用对应引擎启动新进程"，这是架构上可行的完整功能。
2. **安卓引擎现状**：安卓起源引擎（nillerusr 移植版）基于 2020 泄漏的 Source 2017 源码，可运行本模组的内容层；本仓库**不提供、不构建**该引擎二进制，引擎内 C++ 面板需自行在安卓引擎工程中编译（`ANDROID` 宏已处理平台差异）。
3. **中心大厅地图**：`maps/mod_hub.vmf` 由仓库内的 `tools/gen_vmf.py` 生成，需在 Hammer (Source SDK 2013) 中编译为 `.bsp` 后使用。

## 构建与验证（GitHub Actions 云编译）

- `.github/workflows/core-build.yml`：在 **Linux x64（编译+运行测试）/ Linux ARM64（交叉编译）/ Windows x64（MinGW 交叉编译）** 三个目标上构建并验证模组解析核心；
- `.github/workflows/validate-mod.yml`：校验模组文件夹结构与 gameinfo.txt 格式。

本地快速验证：

```bash
g++ -std=c++17 -O2 -Wall -Wextra \
    src/ModHubCore/mod_scanner_test.cpp src/ModHubCore/modhub_core.cpp \
    -o modhub_test && ./modhub_test
```

## 许可证

MIT License。与 Valve 无关的独立项目；Half-Life 2 / Garry's Mod / Counter-Strike 均为其各自权利人的商标。安卓引擎为第三方基于泄漏源码的移植，使用与分发请自行评估法律风险。
