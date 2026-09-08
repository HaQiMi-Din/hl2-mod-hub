HL2 Mod Hub 模组安装与使用说明（放进游戏后的说明）
====================================================

一、本模组是什么
  这是一个真正的起源引擎(Source Engine)模组文件夹：
    - gameinfo.txt  - 引擎入口定义（挂载 HL2 内容，引擎可识别）
    - cfg/          - 启动配置与模组登记表
    - maps/         - 中心大厅地图（mod_hub.vmf 源文件，需用 Hammer 编译）
  PC/Linux 上可用任意 HL2 引擎以 -game mod_hub 启动；
  安卓起源引擎（nillerusr 移植版）可直接把本文件夹放入模组目录。

二、引擎内功能
  1. 启动后自动执行 cfg/autoexec.cfg，控制台显示引导信息；
  2. 控制台命令：modhub_help / modhub_list / modhub_run <名字>；
  3. 把其他模组登记到 cfg/modhub_mods.cfg，即可在控制台统一管理。

三、技术边界（重要）
  起源引擎的每个模组是独立程序，游戏运行中无法直接启动另一个模组。
  - 内容/地图型模组：可被本模组通过 SearchPaths 解析与挂载内容；
  - 独立模组(GMod/CS:S等)：需要退出后用对应引擎启动。
  引擎内动态扫描并一键启动其他模组（图形菜单）需要编译 C++ 层，
  见仓库 src/ModHubEngine/ 与 README。

四、中心大厅地图
  maps/mod_hub.vmf 是中心大厅地图源文件：
  在 Hammer(Source SDK 2013) 中打开 -> 编译(mod) -> 把生成的
  mod_hub.bsp 放到本文件夹 maps/ 下，即可在游戏内进入。

五、安卓安装（实测可用方法）
  安卓移植版不靠 -game 切换模组；官方支持 custom 内容挂载：
  把本文件夹放到 /storage/emulated/0/srceng/hl2/custom/mod_hub/
  后，用原版 HL2 启动，materials/ models/ maps/ 自动挂载。
  控制台辅助命令需手动执行: exec modhub_mods.cfg
