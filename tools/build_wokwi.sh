#!/usr/bin/env bash
set -euo pipefail
# 使用独立生成配置和构建目录，防止仿真 GRB 设置影响实板 RGB 固件。
cd "$(dirname "$0")/.."
if [ "$#" -eq 0 ]; then
    set -- build
fi
exec idf.py -B build-wokwi "-DSDKCONFIG=$PWD/build-wokwi/sdkconfig" \
    '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wokwi' "$@"
