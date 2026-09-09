# Android 安装（安卓起源引擎）

## 背景

安卓上的起源引擎是 nillerusr 的社区移植版（Source Engine on Android），
支持在安卓上运行 HL2/Portal/CS 等，并以**内容文件夹**的形式安装模组内容。

参考文档：
- Valve 开发者社区 Wiki：https://developer.valvesoftware.com/wiki/Source_on_Android
- 项目文档：https://github.com/EternalCringe/source-engine-docs

## 安装本模组（实测可用：custom 内容挂载法）

本移植版**每个游戏是独立启动器 APK，不靠 `-game` 切换模组**；
但原版 `hl2/gameinfo.txt` 自带 `game+mod hl2/custom/*`，即
`srceng/hl2/custom/` 是官方认可的**模组内容自动挂载目录**（装 shader 也用它）。

1. 把 `mod_hub/` 文件夹解压/复制到：
   `/storage/emulated/0/srceng/hl2/custom/mod_hub/`
   （即内部存储的 `srceng/hl2/custom/` 下，得到 `custom/mod_hub/`）；
2. 打开起源引擎 App，**命令行参数不需要 `-game`**，保持默认（原版 HL2）启动；
3. 模组里的 `materials/`、`models/`、`maps/`（编译后的 bsp）会被自动挂载进原版 HL2。

说明：
- custom 模式下 `gameinfo.txt` 不生效（由原版 hl2/gameinfo.txt 统一管理），内容挂载不受影响；
- 想启用控制台辅助命令（modhub_help / modhub_list），在游戏内控制台手动执行：
  `exec modhub_mods.cfg`
- 若你的引擎数据根目录不是 `/storage/emulated/0/srceng`，按实际路径放置。

## 引擎内图形菜单 / 运行时自动解析（需要编译）

- **运行时自动解析 .gma（"把 .gma 丢进 mod/ 就自动挂载"）**需要引擎内代码：
  把 `src/ModHubEngine/gma_loader.cpp` + `engine_gma_hook.cpp` + `src/ModHubCore/gma_parser.cpp`
  编进安卓引擎工程（nillerusr 源码），初始化时调用
  `modhub::RunModHubStartup(gameDir, filesystem, nullptr)`；
  之后 `mod_hub/mod/*.gma` 会在启动时自动解包到 `mod_unpacked/` 并挂载。
  说明：`hl2/custom` 内容挂载方式**不会**加载模组自定义模块，因此
  custom 模式下不能自动解包——需要以 `-game mod_hub` 独立模组方式
  启动（若你的移植版支持），或在安卓引擎工程中直接集成钩子。
- 模组解析核心 `src/ModHubCore/` + `src/ModHubEngine/gma_loader.cpp` 是纯标准库，
  可交叉编译到 arm64（本仓库 CI 已验证）。
- 备用方案（不需要编译）：用 Release 里的 `modhub_cli-linux-arm64` 在 Termux 中
  手动解包 .gma，把解出的 models/ materials/ 放进 `hl2/custom/mod_hub/` 对应目录。

## 法律与技术提示

- 本仓库**不提供、不构建**安卓引擎二进制（该引擎基于泄漏源码，使用与分发需自行评估
  Valve 版权与法律风险）；
- 本模组内容层（gameinfo.txt / cfg / maps / materials）与具体引擎构建无关，
  在 PC / Linux / 安卓引擎上通用。
