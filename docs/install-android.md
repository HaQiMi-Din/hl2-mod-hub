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

## 引擎内图形菜单（需要编译）

安卓版引擎的自定义 UI 需要把 `src/ModHubEngine/` 的面板代码编进为安卓构建的引擎模块
（`ANDROID` 宏已处理平台差异）。模组解析核心 `src/ModHubCore/` 是纯标准库，
可交叉编译到 arm64（本仓库 CI 已验证），并可直接用 `modhub_cli` 在 Termux 中
解析/解包 .gma 附加组件。

## 法律与技术提示

- 本仓库**不提供、不构建**安卓引擎二进制（该引擎基于泄漏源码，使用与分发需自行评估
  Valve 版权与法律风险）；
- 本模组内容层（gameinfo.txt / cfg / maps / materials）与具体引擎构建无关，
  在 PC / Linux / 安卓引擎上通用。
