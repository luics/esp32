# telegraph_ha

保留 ESP-IDF 电报机和 HTTPS 历史同步，通过 ESP-MQTT 1.1.x 与 Home Assistant MQTT Discovery 接入。
启用 `CONFIG_TELEGRAPH_HA_ENABLED` 后调用 `telegraph_ha_start()` 一次；默认关闭，Broker 为空时不启动客户端。
本组件不初始化 Wi-Fi，复用 `telegraph_sync_start()` 启动的连接；SSID/密码仍在 Telegraph API synchronization 中配置。
`CONFIG_TELEGRAPH_SYNC_BASE_URL` 可设为空，仅维护 Wi-Fi、关闭 HTTPS 请求和历史出站缓存。

## 实体和消息

设备 ID 为 `ha_test_<完整 STA MAC 的十二位小写十六进制>`，与每次启动随机的三字符标签无关。
HA 设备显示为“HA-Test”，只暴露一个 `light` 实体 GPIO5 LED。使用独立设备 ID、MQTT client ID 与主题，与原项目 `telegraph_<MAC>` 的摩斯码电报机同时接入 HA；本项目不删除原项目的实体。

- Discovery：`homeassistant/light/<device_id>_led/config`。
- 状态：`luics/<device_id>/state`，`ON` / `OFF`，表示 HA 请求的开关状态。
- 控制：`luics/<device_id>/led/set`，只接受非 retained 的 `ON` / `OFF`。
- 在线：`luics/<device_id>/availability`，online/offline；异常退出由 Broker 发布 LWT offline。

ON 时空闲持续闪烁红光，周期 150 ms（亮灭各 75 ms），OFF 时停止 HA 闪光。模式黄光、点划颜色和 PIR 闪光依次优先于 HA 红光快闪；这些本地提示结束后恢复 HA 请求状态。状态表示请求值，不反映提示期间的瞬时颜色。上电默认 OFF，断线保留当前请求；重启不恢复旧命令。

Discovery、状态和在线消息均 QoS 1、retain；控制消息不保留。重连重新订阅并发布配置与最新状态；收到 HA birth 后重发配置和状态。MQTT 回调通过单槽队列交给输入任务控制灯，网络故障不影响本地操作。

## 本地配置

取得重新配置授权后，通过 menuconfig 修改本地生成配置，真实账号密码不加入 defaults 或版本库：

```sh
eim run "idf.py menuconfig" stable-mcp
# Component config → Telegraph API synchronization：Wi-Fi SSID / password
# Component config → Telegraph Home Assistant：Enable、Broker URI、username / password
# 仅 MQTT：将 Telegraph HTTPS base URL 清空

eim run "idf.py reconfigure build" stable-mcp
# Wokwi 配置与实板独立，需要在下列 menuconfig 中单独启用 MQTT 并设置 Broker。
eim run "bash tools/build_wokwi.sh menuconfig" stable-mcp
eim run "bash tools/build_wokwi.sh reconfigure build" stable-mcp
```

支持 `mqtt://` 和 `mqtts://`。TLS 保留主机名验证，使用 ESP CA bundle，仅适用于其信任的公共 CA；
私有 CA Broker 目前需另加可信 CA 支持，不可关闭证书校验。TLS 需要正确时间，现有网络任务会启动 SNTP。
认证在独立 username/password 字段配置，URI 中不放凭据。默认关闭时不启动 MQTT，未使用代码由链接器移除，HA 选项不启用 CA bundle。
新增依赖由 Component Manager 生成 lock；构建后检查应用仍在当前 1500 KiB 单应用分区内，无 OTA。

## 本机 Mosquitto 和 HA Container

仓库提供 [tools/ha](../../tools/ha) 的可选本地 Broker 配置。现有 HA 不需要重建，也无需 ESPHome。

```sh
mkdir -p tools/ha/local
# 交互式输入新建的 Broker 密码；密码只写入被 gitignore 忽略的本地文件。
docker run --rm -it -v "$PWD/tools/ha/local:/mosquitto/secrets" eclipse-mosquitto:2 \
  mosquitto_passwd -c /mosquitto/secrets/passwords telegraph
docker compose -f tools/ha/compose.yaml up -d
```

此示例只将 Broker 映射到本机 127.0.0.1:1883，适用于本机 Docker Desktop + Wokwi VS Code 网关。
在 HA「设置 → 设备与服务 → 添加集成 → MQTT」中配置 `host.docker.internal`、1883 和上述账号密码。
若已有 MQTT 集成，使用原 Broker；不要建立重复集成或覆盖现有凭据。
真实开发板连接此 Broker 时，需要自行将端口绑定到可达的局域网接口，并配置防火墙与认证；127.0.0.1 不可供实板访问。

## Wokwi 联调

Wokwi 支持 MQTT。VS Code 扩展自带的网络网关能访问宿主机服务；Broker URI 使用
`mqtt://host.wokwi.internal:1883`，Wi-Fi 使用 Wokwi-GUEST、空密码。
网页 Wokwi 默认 Public Gateway 只能访问公网，连接本地/局域网 Broker 要启用 Private Gateway；
`host.wokwi.internal` 仅在相应本地网关环境使用。不要把本机 `localhost` 写成仿真 ESP32 的 Broker。
公网 Broker 需使用自己的认证/TLS 配置，不把真实数据发送到公共匿名测试 Broker。

1. 启用并构建 Wokwi 配置；VS Code 执行 Wokwi: Start Simulator。
2. 确认 HA-Test 设备只有 GPIO5 LED 实体，原摩斯码电报机及五个实体仍保留。
3. 从 HA 打开和关闭 LED，观察空闲红光快闪；触发模式、点划、PIR，确认提示优先且结束后恢复请求状态。
4. 重启 HA、断开与重连仿真，确认 Discovery、状态及在线状态恢复。

验证需分别记录主机逻辑、固件编译、Broker/HA Discovery 及实际仿真灯光；仅检查日志不能替代联调。

参考：[HA MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)、
[MQTT Light](https://www.home-assistant.io/integrations/light.mqtt/)、
[Wokwi Wi-Fi 与网关](https://docs.wokwi.com/guides/esp32-wifi)、
[ESP-MQTT 1.1.0](https://components.espressif.com/components/espressif/mqtt/versions/1.1.0/readme)。

## 验证记录（2026-10-27）

- Wokwi 本地生成配置已启用 HA，复用已有 Mosquitto 和 HA MQTT 集成；凭据仅存本地忽略文件。
- `eim run "bash tools/build_wokwi.sh reconfigure build" stable-mcp` 通过，ESP-IDF v6.1、esp32c6；最终红光快闪固件 0x13a550 字节，应用分区剩余 16%。
- Wokwi 成功连接本机 Broker；HA 显示 HA-Test 和一个 GPIO5 LED 实体，原项目五个 Discovery 与本项目互不影响。
- 早期白光版本已实际验证 HA 开灯、关灯与 ON/OFF 回报；最终红光快闪版本已通过主机测试和构建，尚未完成实际仿真灯效验证。
- 仿真后台可能降速并触发 Broker 心跳超时，联调时保持模拟器前台。未烧录实板。

两项目并存：原摩斯码电报机继续使用 `telegraph_<MAC>` 和原五个实体；HA-Test 使用 `ha_test_<MAC>` 和一个灯实体。已在 HA 核对为两个设备、六个实体。原设备未运行时显示不可用，启动原项目固件后自动恢复在线。

HA ON 灯效已改为持续红光快闪（150 ms 周期），主机测试覆盖连续多周期、点划优先和 OFF 停止。实板与 Wokwi 构建通过。
