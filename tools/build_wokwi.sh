#!/usr/bin/env bash
set -euo pipefail
# 使用独立生成配置和构建目录，隔离仿真 GRB/SH1107 与实板 RGB/SSD1327 固件。
cd "$(dirname "$0")/.."
if [ "$#" -eq 0 ]; then
    set -- build
fi
exec idf.py -B build-wokwi "-DSDKCONFIG=$PWD/build-wokwi/sdkconfig" \
    '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wokwi' "$@"
