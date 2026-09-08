ModHubEngine 引擎集成层 - 构建说明
===================================

本目录是 HL2 Mod Hub 的"引擎内完整功能"层：一个 VGUI2 面板，
在游戏里弹出后列出模组文件夹中的所有模组，选中即可用对应引擎启动。

一、为什么它需要单独编译
  起源引擎的自定义 UI/逻辑必须编译成 client.dll（或安卓的对应模块）。
  这意味着需要一份完整、可编译的 Source 引擎源码工程：
  - PC：Steam 上的 "Source SDK 2013 Singleplayer"（通过 Steam 工具安装）
  - 安卓：nillerusr 的安卓起源引擎（基于 2020 泄漏源码，自行编译，
        需自行评估法律风险；本仓库不提供也不构建引擎二进制）

二、集成步骤（PC / Source SDK 2013 SP）
  1. 把本目录的 ModHubPanel.cpp/.h 加入 SDK 的 game/client 工程；
  2. 把 src/ModHubCore/ 的 modhub_core.cpp/.h 一并加入（纯标准库，直接编译）；
  3. 在客户端主代码（如 CHL2_Client::... 或输入系统）注册呼出命令：
        concommand modhub_open([]() {
            static ModHubPanel* pPanel = nullptr;
            if (!pPanel) pPanel = new ModHubPanel(nullptr, "ModHubPanel");
            pPanel->Activate();
        });
  4. 可选：创建 resource/ModHubPanel.res 布局文件；
  5. 用 SDK 的 createallprojects/buildallprojects 编译，得到
     client.dll（+ server.dll），放入 mod_hub/bin/ 与 mod_hub/ 对应目录。

三、安卓
  模组内容层（mod_hub 文件夹 + gameinfo.txt）可以直接放入安卓引擎的
  模组目录（内部存储 srceng/）并运行，无需编译。
  引擎内动态启动面板需要在安卓版引擎工程中编译本面板：
  - 本代码已用 ANDROID 宏保护进程拉起逻辑（安卓上提示改用引擎主界面选择）；
  - 模组解析核心(ModHubCore)为纯标准库，可 cross-compile 到 arm64。

四、验证
  本仓库 CI（.github/workflows/core-build.yml）在 x64 / ARM64 / Windows
  三个目标上编译并测试 ModHubCore（模组解析逻辑）。
  引擎面板本身必须在你的 SDK 工程中编译验证。

- 模组解析核心已提供 LuaVm（src/ModHubCore/lua_vm.h）——面板如需执行附加组件脚本，可嵌入该 VM 并挂到游戏事件循环。
