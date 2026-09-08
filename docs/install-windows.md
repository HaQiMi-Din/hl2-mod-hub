# Windows 安装

## 方式一：作为 Steam 模组（推荐）

1. 找到你的 Steam 安装目录，进入 `steamapps/sourcemods/`
   （例如 `C:/Program Files (x86)/Steam/steamapps/sourcemods/`）；
2. 把 `mod_hub/` 整个文件夹复制进去，得到 `.../sourcemods/mod_hub/`；
3. 重启 Steam —— 库中会出现 "HL2 Mod Hub"；
4. 直接运行它，或用命令行启动：
   ```bat
   "...\Half-Life 2\hl2.exe" -game mod_hub
   ```
5. 启动后控制台会自动执行 `cfg/autoexec.cfg`，显示使用说明。

## 方式二：非 sourcemods 目录

也可以放到任意位置，用绝对路径启动：
```bat
hl2.exe -game "D:\MyMods\mod_hub"
```

## 引擎内使用

- 控制台输入 `modhub_help` / `modhub_list` 查看说明；
- 把想管理的模组登记进 `mod_hub/cfg/modhub_mods.cfg`；
- 进入地图 `mod_hub`（用 Hammer 把 `maps/mod_hub.vmf` 编译成 .bsp 后放入 `maps/`）。

## 引擎内图形菜单（完整功能）

需要编译 C++ 层，见 `src/ModHubEngine/README-engine.txt`。
