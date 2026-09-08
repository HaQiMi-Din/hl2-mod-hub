# Android 安装（安卓起源引擎）

## 背景

安卓上的起源引擎是 nillerusr 的社区移植版（Source Engine on Android），
脱胎于英伟达神盾版《半条命2》/《传送门》，基于 2020 泄漏的 Source 2017 源码。
它支持在安卓上运行 HL2/Portal/CS 等，并**以模组文件夹的形式安装模组**。

参考文档：
- Valve 开发者社区 Wiki：https://developer.valvesoftware.com/wiki/Source_on_Android
- 项目文档：https://github.com/EternalCringe/source-engine-docs

## 安装本模组（内容层，无需编译）

1. 把 `mod_hub/` 文件夹复制到安卓设备内部存储的引擎模组目录
   （nillerusr 引擎默认使用内部存储的 `srceng/`，即
   `/sdcard/srceng/`，如路径不同请以引擎文档为准），得到
   `/sdcard/srceng/mod_hub/`；
2. 打开安卓起源引擎，在主界面选择 HL2 Mod Hub 模组；
3. 启动后控制台自动执行 `cfg/autoexec.cfg`，可查看/登记其他模组。

## 引擎内图形菜单（需要编译）

安卓版引擎的自定义 UI 需要把 `src/ModHubEngine/` 的面板代码编进
为安卓构建的引擎模块（`ANDROID` 宏已处理平台差异：安卓上点击"启动"
会提示改在引擎主界面选择模组，因为安卓游戏进程无法直接拉起另一个进程）。
模组解析核心 `src/ModHubCore/` 是纯标准库，可交叉编译到 arm64
（本仓库 CI 已验证）。

## 法律与技术提示

- 本仓库**不提供、不构建**安卓引擎二进制（该引擎基于泄漏源码，使用与
  分发需自行评估 Valve 版权与法律风险）；
- 本模组内容层（gameinfo.txt / cfg / maps）与具体引擎构建无关，
  在 PC / Linux / 安卓引擎上通用。

## 安卓版 gameinfo.txt 说明

本移植版不解析 Steam 专用的 `|all_source_engine_paths|` 宏，且 `Game hl2` 会解析到
App 私有目录（不存在）。mod_hub/gameinfo.txt 已包含多种路径写法兜底
（PC 宏 / 相对路径 / 安卓绝对路径），其中：

    Game    |gameinfo_path|../hl2
    Game    /storage/emulated/0/srceng/hl2

即为安卓版挂载原版 hl2 内容的关键行；不存在的路径会被引擎自动跳过。
如你的引擎数据根目录不是 /storage/emulated/0/srceng，请把绝对路径行改为实际路径。
