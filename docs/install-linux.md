# Linux 安装

1. 安装 Half-Life 2（Steam，支持 Linux 原生客户端）；
2. 把 `mod_hub/` 放到 Steam 的 `steamapps/sourcemods/`：
   ```bash
   STEAM=~/.steam/steam
   cp -r mod_hub "$STEAM/steamapps/sourcemods/"
   ```
3. 启动：
   ```bash
   "$STEAM/steamapps/common/Half-Life 2/hl2_linux" -game mod_hub
   ```
4. 控制台自动执行 `cfg/autoexec.cfg`。

说明：Linux 版引擎与 Windows 版加载的模组格式完全一致
（内容层 gameinfo.txt/maps/cfg 通用）；C++ 图形菜单层需按
`src/ModHubEngine/README-engine.txt` 在 Linux SDK 工程中编译。
