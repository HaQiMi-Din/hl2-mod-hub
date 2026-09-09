ModHubEngine 引擎集成层 - 构建说明
===================================

本目录是 HL2 Mod Hub 的"引擎内完整功能"层，包含两套集成组件：

  1. gma_loader（纯 C++17，无引擎依赖，CI 已编译测试）
     —— 模组内置 GMA 自动加载器：游戏启动时扫描
        <模组目录>/mod/*.gma，解析 + CRC 校验 + 解包到
        <模组目录>/mod_unpacked/<gma名>/，返回挂载清单。
        引擎侧把每个解包目录 AddSearchPath 进 GAME 路径后，
        模型/材质/地图立即在游戏内可用。全程无需 Termux/外部工具。

  2. engine_gma_hook（依赖 Source SDK 头文件，本仓库只交付源码）
     —— 把 gma_loader 挂进引擎启动流程 + 控制台命令 modhub_scan。

一、为什么引擎钩子需要单独编译
  起源引擎不会自动执行模组里的脚本/二进制来挂载内容；要让
  "启动时自动解包 .gma" 生效，必须有一段代码编进引擎模块
  （client.dll / server.dll 或安卓对应 .so）：
  - PC：Steam 工具里的 "Source SDK 2013 Singleplayer"（免费，需在
        Steam > 库 > 工具 安装），编译成模组的 client.dll；
  - 安卓：nillerusr 安卓起源引擎（基于 2020 泄漏源码，自行编译，
        使用与分发需自行评估法律风险；本仓库不提供也不构建引擎二进制）。

二、集成步骤（PC / Source SDK 2013 SP）
  1. 把 engine_gma_hook.cpp 与 gma_loader.cpp/.h 加入 SDK 的 game/client 工程；
  2. 把 src/ModHubCore/ 的 gma_parser.cpp/.h 一并加入（纯标准库）；
  3. 在客户端模块初始化处调用一次：
        modhub::RunModHubStartup(engine->GetGameDirectory(),
                                 engine->GetFileSystem(), nullptr);
     建议放在 CBaseClientDLL::Init 或首次 LevelInit 前，确保主菜单出现前
     内容已挂载；
  4. 注册控制台命令（文件内已有示例）：modhub_scan 随时手动重扫；
  5. 用 SDK 的 buildallprojects 编译，把 client.dll 放进 mod_hub/bin/
     （模组以 -game mod_hub 启动时由 gameinfo.txt 的 GameBin 定位）；
  6. 之后把任意 .gma 丢进 mod_hub/mod/，启动即自动挂载。

三、安卓
  - 内容层（mod_hub 文件夹 + mod/*.gma 的"预解包"结果）可直接用
    hl2/custom 挂载方式运行，无需编译（见 docs/install-android.md）；
  - 但"运行时自动解析 .gma"需要引擎内代码：安卓版需把本钩子编进
    安卓引擎工程（ANDROID 宏已处理平台差异），并以独立模组
    （-game mod_hub，若移植版支持）或引擎自定义入口方式启动；
  - 模组解析核心 gma_parser / gma_loader 为纯标准库，本仓库 CI
    已交叉编译验证 x64 / ARM64 / Windows 三目标。

四、验证
  - CI（core-build.yml）在 x64 / ARM64 / Windows 三个目标上编译并
    运行 modhub_loader_test（合成真实 v3 .gma → 自动解包 → 逐字节校验
    → 缓存命中），18 项断言全绿；
  - 引擎钩子本身必须在你的 SDK 工程中编译验证。
