# Android 路线 C：注入式加载器（AndroidModLoader）——不需要重编引擎

本路线把"模组内置 GMA 自动加载器"做成一个安卓原生 .so 模组，
通过 [AndroidModLoader (AML)](https://github.com/RusJJ/AndroidModLoader)
注入进已运行的起源引擎进程。**不用改/重编引擎源码**，但需要：
1. 一个已经集成 AML 的起源引擎 APK（一次性准备工作）；
2. 把本仓库云编译的 `libmodhub_aml.so` 放进 AML 的 mods 目录。

之后 `hl2/custom/<任意模组>/mod/*.gma` 与 `hl2/mod/*.gma` 会在游戏
启动时被自动解析、CRC 校验、解包并挂载进引擎文件系统——**全程
不需要 Termux、不需要电脑**。

---

## 一、产物

Release（v1.2.0+）里的 `modhub_aml` 附件包含两个文件：

| 文件 | 架构 |
| --- | --- |
| `arm64-v8a/libmodhub_aml.so` | 64 位（你的 HEY-W09 是 arm64，用这个） |
| `armeabi-v7a/libmodhub_aml.so` | 32 位 |

源码：`src/AMLMod/`（`jni/main.cpp` 是模组本体，`include/mod/` 是 AML
官方模组头，MIT 许可）。

## 二、安装步骤

### 1) 准备一个已集成 AML 的起源引擎 APK（一次性）

AML 的注入方式是修改目标 APK（smali 注入），这一步属于 AML 的使用范畴：

- 到 AML Releases（https://github.com/RusJJ/AndroidModLoader/releases）
  下载对应的注入工具 / 参考其文档对 `com.valvesoftware.source` APK 打补丁；
- 或使用社区已集成 AML 的起源引擎整合包（自行评估来源可信度与法律风险）；
- 装好后的 App 包名仍是 `com.valvesoftware.source`，且引擎启动日志
  （engine.log）中能看到 AML 加载。

> 本仓库不提供、不构建任何引擎 APK（引擎基于泄漏源码）。
> 集成 AML 是对 APK 的修改，请仅用于你自己拥有的文件。

### 2) 安装 modhub_aml.so

把 `arm64-v8a/libmodhub_aml.so` 复制到手机，放到 AML 的模组目录：

```
/sdcard/Android/data/com.valvesoftware.source/mods/libmodhub_aml.so
```

（若你的 AML 版本使用 `/sdcard/AMLMods/<game>/` 布局，按其文档放置。）

### 3) 放 .gma 并启动

1. 保持 `hl2/custom/mod_hub/` 的部署不变（路线 A）；
2. 在 `hl2/custom/mod_hub/` 下新建 `mod/` 文件夹，把任意
   GMod 附加组件 `.gma` 丢进去；
3. 正常用原版 HL2 启动 —— AML 会在引擎库加载后注入本模组：
   - 自动扫描 `hl2/custom/*/mod/*.gma` 与 `hl2/mod/*.gma`；
   - 解包到各自的 `mod_unpacked/`；
   - `AddSearchPath` 挂载进 GAME 路径；
   - 模型 / 材质 / 地图立即在游戏内可用。

## 三、看日志确认

启动后在 logcat 里过滤 `ModHub`：

```bash
adb logcat -s ModHub
# 期望看到:
# BaseDir: /storage/emulated/0/srceng
# [xxx.gma] mounted /storage/emulated/0/srceng/hl2/custom/mod_hub/mod_unpacked/xxx (33 entries, CRC OK, extracted)
# ModHub auto-mounter finished.
```

## 四、技术说明与已知边界

- 挂载实现：从 `libfilesystem_stdio.so` 导出符号 `CreateInterface`
  取 `VFileSystem017`（Source 2013 接口名）→ 调 `IBaseFileSystem`
  vtable 的 `AddSearchPath(path, "GAME", PATH_ADD_TO_HEAD)`；
- 数据根目录：读引擎启动时 setenv 的 `VALVE_GAME_PATH`
  （= `/storage/emulated/0/srceng`），取不到时回退默认值；
- 幂等缓存：文件未变化时二次启动直接命中 manifest，跳过解包；
- 内容型边界同路线 A：GMod 的 Lua（player_manager 等）在 HL2 引擎内
  不会运行；解包出的模型/材质/地图作为 HL2 资源使用；
- AML 为第三方工具，其与目标 APK 的兼容性以 AML 文档为准；
  本模组仅依赖标准接口（CreateInterface / VFileSystem017），
  不依赖引擎内部偏移。

## 五、自己编译

```bash
# 需要 Android NDK（任意 r21+，含 ndk-build）
bash src/AMLMod/prepare.sh
$NDK/ndk-build NDK_PROJECT_PATH=$PWD/src/AMLMod \
    APP_BUILD_SCRIPT=$PWD/src/AMLMod/jni/Android.mk \
    NDK_APPLICATION_MK=$PWD/src/AMLMod/jni/Application.mk \
    NDK_OUT=/tmp/amlobj NDK_LIBS_OUT=/tmp/amllibs
# 产物: /tmp/amllibs/{arm64-v8a,armeabi-v7a}/libmodhub_aml.so
```
