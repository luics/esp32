# telegraph_ha

保留 ESP-IDF 电报机和 HTTPS 历史同步，通过 ESP-MQTT 1.1.x 与 Home Assistant MQTT Discovery 接入。
启用 `CONFIG_TELEGRAPH_HA_ENABLED` 后调用 `telegraph_ha_start()` 一次；默认关闭，Broker 为空时不启动客户端。
本组件不初始化 Wi-Fi，复用 `telegraph_sync_start()` 启动的连接；SSID/密码仍在 Telegraph API synchronization 中配置。
`CONFIG_TELEGRAPH_SYNC_BASE_URL` 可设为空，仅维护 Wi-Fi、关闭 HTTPS 请求和历史出站缓存。

## 实体和消息

设备 ID 为 `telegraph_<完整 STA MAC 的十二位小写十六进制>`，与每次启动随机的三字符标签无关。
实体共用同一个 device.identifiers，归属“摩斯码电报机”。多台 Wokwi 实例需使用不同 macAddress，避免 ID 和 MQTT client ID 冲突。

| HA 实体 | 内容 |
|---|---|
| binary_sensor / 人体运动 | PIR 实际高低电平，motion device class |
| select / 输入模式 | S、D；可控制，只在原状态机实际切换后更新状态 |
| sensor / 间隔单位 | 当前字符锁定单位（ms）；空闲时是旋钮选中值 |
| sensor / 最新字符 | 最近一次静默满三单位确认的字符；上电为空 |
| sensor / 最新摩斯码 | 对应点划或 INVALID；上电为空 |

- Discovery：`homeassistant/<domain>/<device_id>_<motion|mode|unit|character|code>/config`。
- 状态：`luics/<device_id>/state`，JSON 字段 `motion`（ON/OFF）、`mode`（S/D）、`unit_ms`、`character`、`code`。
- 控制：`luics/<device_id>/mode/set`，只接受非 retained 的精确单字节 `S` 或 `D`。
- 在线：`luics/<device_id>/availability`，online/offline；30 秒 keepalive，异常退出由 Broker 发布 LWT offline，检测有延迟。

Discovery、状态、在线与遗嘱均 QoS 1、retain。每次重连重新订阅、发布 Discovery 和最新真实状态；收到
`homeassistant/status=online` 只重发配置和状态，避免重新订阅 retained birth 引发循环。
主动发布的消息成功进入 MQTT outbox 时以 INFO 输出 `MQTT queued: msg_id=..., topic=..., payload=...`，覆盖 Discovery、状态和 online；该日志表示入队，不表示 Broker 已确认，offline 遗嘱由 Broker 发布。
HA select 的 retain=false，设备也拒绝订阅时投递的 retained 控制。请勿手动向控制主题发送 retained 消息；
MQTT 3.1.1 对已在线订阅者的实时投递可能不保留该标志，无法据此判断发布者原始 retain。

`telegraph_ha_update()` 只覆盖单槽快照；后台每 200 ms 检查变更，不把全量历史传到 HA。
断网期间仅保留最新状态，不逐条补报离线期间每个字符/运动边沿；本地完整历史和 HTTPS 同步语义保持原样。
命令通过另一单槽队列交给输入任务，短时间多个远程模式设值取最后一个；重复设值不切换。
发送中切换、取消缓存及跨模式松开保护复用 keyer，MQTT 回调不访问状态机或控制外设。
网络故障继续本地输入；MQTT outbox 限制为 8 KiB，入队失败重试最新状态/配置，并记录警告。

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
2. 串口确认取得 IP、`telegraph_ha: MQTT connected; device=...`，HA MQTT 设备列表出现五个实体。
3. 点击 PIR 的 Simulate Motion，确认 HA 运动实体跟随，同时实际观察 GPIO5 的分档灯光。
4. 点击 M，确认 HA 模式跟随；从 HA 设置 S/D，验证发送中的划完整结束后才切换，S 下划键无效。
5. 输入字符，确认声光、屏幕与三单位确认不受联网影响，HA 最新字符/摩斯码一致；旋钮确认单位锁定。
6. 重启 HA，验证实体恢复；断开仿真，等待 Broker LWT 确认不可用；重新运行后恢复在线和当前状态。
7. 向 mode/set 非 retained 发送重复 D，确认不反复切换；断开设备后设置 retained S，再重连，确认旧命令被忽略。
   测完用 retained 空消息清除该控制主题，避免后续测试污染。

验证层次需分别记录：主机逻辑测试、固件编译、Broker/HA Discovery、Wokwi 实际灯光/声音/屏幕及双向联调。
仅检查启动日志或手工发布 JSON 均不能替代完整仿真验证。

参考：[HA MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)、
[MQTT Select](https://www.home-assistant.io/integrations/select.mqtt/)、
[Wokwi Wi-Fi 与网关](https://docs.wokwi.com/guides/esp32-wifi)、
[ESP-MQTT 1.1.0](https://components.espressif.com/components/espressif/mqtt/versions/1.1.0/readme)。

## 验证记录（2026-10-26）

环境：ESP-IDF v6.1（EIM stable-mcp），esp32c6、8 MB Flash。

- `python3 tools/test_host.py` 全部通过；新增 MQTT 有界分片、retained 控制过滤及真实应用任务的远程延迟切换测试。
- `eim run "idf.py reconfigure build" stable-mcp` 与 `eim run "bash tools/build_wokwi.sh reconfigure build" stable-mcp` 均通过。
  启用 MQTT 的 Wokwi 固件为 0x13a940 字节，1500 KiB 应用分区剩约 16%；无分区/OTA 修改。
- 本机 Mosquitto 已按上述 Compose 启动，HA Container 已添加 MQTT 集成并自动发现本设备的五个实体。
  本地连接信息保存在忽略的 `tools/ha/local/credentials.json`，固件凭据只进入本地生成配置。
- Wokwi M 键的 S/D 变化同步到 HA；HA select 设置 S 后，OLED 实际显示 `S 100MS`。
  PIR Simulate motion 的 Detected/Clear 均在 HA 活动中出现；单键长按确认 T，HA 收到字符 T 和摩斯码 `-`。
- 实际向 Broker 发布 HA birth `homeassistant/status=online` 后捕获恰好五条新的 Discovery，未出现重复订阅循环。
  仿真后台/暂停时曾发生 Broker 心跳超时，HA 显示不可用；恢复运行后自动重新上线，保留同一设备与实体。
- 双向/PIR/字符联调曾在本地仅 MQTT 配置下隔离验证；随后已恢复原 HTTPS 地址并重新构建，MQTT 再次上线。
  HTTPS 与 MQTT 的长时间并发运行、所有物理接线及真实音频听辨仍需进一步验证；未烧录实板。

仿真联调期间保持 Wokwi 面板在前台、速度接近 100%。Wokwi 的仿真时钟会因后台暂停/降速落后于真实 Broker 时钟，
这种情况下可能触发 LWT；实板在线稳定性应单独验证。
