#!/bin/sh
# 把纯逻辑源码（gma_parser / gma_loader）与 AML 模组头同步进 jni/ 供 ndk-build 编译。
# 原因：ndk-build 的 LOCAL_SRC_FILES 不支持 ".." 路径。
# 用法：先跑本脚本，再跑 ndk-build。
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# 纯逻辑源码
cp "$ROOT/src/ModHubCore/gma_parser.h" "$SCRIPT_DIR/jni/"
cp "$ROOT/src/ModHubCore/gma_parser.cpp" "$SCRIPT_DIR/jni/"
cp "$ROOT/src/ModHubEngine/gma_loader.h" "$SCRIPT_DIR/jni/"
cp "$ROOT/src/ModHubEngine/gma_loader.cpp" "$SCRIPT_DIR/jni/"

# AML 模组头（mod/ 目录，含 logger.cpp / config.cpp 编译单元）
rm -rf "$SCRIPT_DIR/jni/mod"
cp -r "$ROOT/src/AMLMod/include/mod" "$SCRIPT_DIR/jni/mod"

echo "synced sources into $SCRIPT_DIR/jni/"
