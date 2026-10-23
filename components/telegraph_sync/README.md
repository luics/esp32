# telegraph_sync

把已确认的摩斯码历史异步同步到 `https://telegraph.bryanxu.top`。

`telegraph_sync_start(label)` 在本次启动生成标签后调用一次。
`telegraph_sync_history(morse)` 在历史增加或词边界变化时调用，只复制已确认历史，不执行网络 I/O。
返回内存错误时应用明确报错；输入中的点划不会上传。

## 记录与请求

按词维护一条远端记录：首个已确认字符创建记录，后续同词字符更新同一记录，7 单位静默后新输入属于下一词。

- `POST /api/telegraph`：要求 HTTP 201，并从相对或同源绝对 `Location` 提取 UUID。
- `PATCH /api/telegraph/:id`：更新已创建记录，接受 2xx。
- 两种请求均发送完整 JSON，包含 `morse`、`content`、`label`。

例如输入 H、I：先提交 `{"morse":"....","content":"H","label":"A09"}`，随后对同一个 UUID 更新为
`{"morse":".... ..","content":"HI","label":"A09"}`。
七单位词确认仅关闭本地词分组，不改变 `morse`、`content`、`label`，因此不单独发送 PATCH；重复快照也不提交，下一词单独 POST。
单字符词只发送一次 POST；预热或断网积压时先发送最新完整内容，POST 完成后只有新增字符导致内容变化才补 PATCH。
引号会正确转义；未知字符上传 `?`，无效点划上传 `INVALID`。

后台任务使用 `tskIDLE_PRIORITY`（0）处理 Wi-Fi、SNTP、HTTPS，输入任务（3）与显示任务（2）保持原有优先级。
ESP-IDF 当前启用同优先级时间片调度，TLS 大整数计算期间 IDLE 也可运行并喂狗；看门狗仍保持 5 秒超时。
HTTP 客户端由同步任务持续持有，同源 POST/PATCH 复用 HTTPS 长连接，避免每次更新都重新握手。
每轮更新 URL、方法和请求体，完成后清空回调状态及请求体引用。
Wi-Fi 与 SNTP 就绪后立即用 `HEAD /api/telegraph` 预连接，不创建历史；空闲时每 25 秒 HEAD，业务响应也会重置空闲计时。
每次业务请求前先维护连接：超过 40 秒未确认活跃则主动断开旧连接并保留票据，先完成安全 HEAD 再发送业务。HEAD 失败每 5 秒重试，不把历史记录标成不明结果；POST 已发出后仍保留原有防重复策略。
`HTTPS preconnect: HTTP ..., ... ms` 日志可观察启动预热和空闲维护；TCP keepalive 本身不能延长服务器的 HTTP 空闲超时。
通过证书包配置回调调用 `mbedtls_ssl_conf_groups`，仅为此客户端允许 P-256（secp256r1，IANA 23），避免服务器选择 X25519；不改变其他客户端和证书签名算法配置。
实板与 Wokwi 配置均已启用 `CONFIG_MBEDTLS_HARDWARE_ECC`，P-256 点乘走 C6 硬件 ECC 路径；仿真性能仍需实测。
保留 CA 证书包与主机名校验，`HTTP xxx, n ms` 日志可比较首次和后续请求耗时。
`CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS=y` 配合 `save_client_session` 启用会话恢复；断网或传输失败时只关闭套接字，保留客户端与 RAM 中的票据。
票据是否可用取决于服务器；被拒绝或过期时仍需完整握手。会话恢复减少后续重连开销，首次冷握手的改善幅度需实测。
主机 TLS 1.2 实测服务器第二次连接的 `session_reused=True`，确认服务端支持会话恢复；此结果不代表 Wokwi/实板耗时。
待发送内容动态存 RAM，同一词尚未发送的版本合并为最新快照；网络请求期间新增字符不会被旧响应确认覆盖。
不同词独立保存远端 UUID。断网前未发出的请求与 PATCH 临时失败每 5 秒重试。
POST 已发出后若超时、失败或 201 缺少有效 Location，则记录错误并阻止该条自动重发，避免重复创建；
HTTP 429 可重试，PATCH 的永久 4xx 错误停止该条同步，后续词仍可发送。
尚未发送的 POST 可安全重试。待同步内容和 UUID 不持久化，复位/断电后清空；不保证断电恢复或严格恰好一次投递。
本地历史始终保留；RAM 耗尽时返回错误，不静默丢弃历史。

## 配置与安全

组件 Kconfig 提供 `CONFIG_TELEGRAPH_SYNC_WIFI_SSID`、`CONFIG_TELEGRAPH_SYNC_WIFI_PASSWORD`、
`CONFIG_TELEGRAPH_SYNC_BASE_URL`（HTTPS，无结尾 `/`）。
实板 SSID 默认为空，此时不同步、不启动网络；在本地 menuconfig 配置后启用。
Wokwi SH1107 构建默认连接开放网络 `Wokwi-GUEST`，密码为空。
Wokwi defaults 将 TCP MSS 降为 536：原抓包中 1440 字节段反复重传且 ACK 不前进，降低后已验证可发送；实板仍使用默认 1440。
SSID/密码仅保存于未跟踪的生成配置；不要把真实凭据加入 defaults 或提交。
Wi-Fi 配置使用 RAM，不写入 NVS；NVS 初始化异常时报告错误，不自动擦除。

HTTPS 使用 ESP-IDF 的 CA certificate bundle 和 SNTP 校时，保留证书与主机名校验。
联网固件超过原 1 MiB 分区，工程 defaults 使用 `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`（1500 KiB 单应用，无 OTA）；NVS 与应用起始地址不变。
需启用 `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`（工程当前已启用），网络需允许 NTP 访问 `pool.ntp.org`。
无 API token 配置；按给定接口使用公开 JSON API。网络初始化失败只停止同步，本地操作继续。

## 验证

运行 `python3 tools/test_host.py`：覆盖词记录归属、增量更新、积压历史、JSON 转义、Location 校验，
以及 HTTP 替身验证 POST/PATCH 复用客户端、请求状态重置、JSON 生命周期、断线重建、完整字段及重试策略；不向线上写入测试数据。
HTTP 替身按 IDF 6.1 行为模拟清空请求体：先清空引用，再删除 Content-Type；缺失头的 `ESP_ERR_NOT_FOUND` 仅在清空时按正常结果处理，非空请求体与其他错误不被忽略。
HTTP 替身也覆盖启动 HEAD、25 秒探测、40 秒旧连接关闭、断连事件与预探测失败不发送 POST，以及 P-256、证书包、会话保存和客户端生命周期。
修改 Kconfig 后，按根目录 AGENTS.md 先取得重新配置授权，再执行实板与 Wokwi 构建。
联网仿真应观察串口 POST/PATCH 的状态码，并确认同词 UUID 不变、下一词使用新 UUID，
同时检查按键声光、模式切换和翻页不被网络超时阻塞。

## TLS 握手分段计时

每次实际建联的 HEAD 或业务请求结束后集中输出 `TLS_TIMING` 日志；失败也输出，后续重试重新计时；复用现有连接的请求不重复输出。
启动预热已恢复。仅记录阶段名、耗时和调用次数，不记录证书、密钥、会话票据或请求内容。

- `dns_tcp_setup`：从 HTTP perform 开始到 TLS 配置回调，包含 DNS、TCP 建连及前置初始化，不是单独的 DNS 耗时。
- `handshake`：首次 TLS 1.2 客户端状态步骤到 HTTP `ON_CONNECTED`；包含握手步骤间的调度、网络等待及少量收尾。
- `after_connect`：连接完成到 HTTP perform 返回，包含请求发送和响应处理。
- `request_total`：本次 HTTP perform 全程。

关键阶段日志中的 `elapsed` 为该状态所有调用的墙钟耗时之和，`calls` 包含 WANT_READ 等重试。
这些计时包含任务抢占和网络等待，不等于纯 CPU 耗时；阶段内部不打印日志，避免串口开销干扰测量。

| `stage` | 可定位的工作 |
|---|---|
| `server_hello_wait_parse` | 等待并解析 ServerHello |
| `server_certificate_read_verify` | 接收、解析证书与证书链校验 |
| `server_key_exchange_read_verify` | 接收密钥交换参数与验证服务器签名 |
| `client_key_generation_agreement_write` | 客户端临时密钥生成、共享密钥计算和 ClientKeyExchange 写出 |
| `client_cipher_spec_derive_keys` | 派生会话密钥和 ChangeCipherSpec |
| `server_ticket_or_cipher_spec_wait` / `server_finished_wait_verify` | 等待服务器票据、切换密码状态和完成确认 |

逐次签名验证额外输出 `TLS_VERIFY`：在同一握手步骤内包装 `psa_verify_hash`，覆盖当前版本证书链、证书包信任锚和 TLS 1.2 服务端签名的验证路径。
每条包含 `index`、`stage`、`alg`（RSA_PKCS1v1_5 / RSA_PSS / ECDSA）、`hash`、原始 `alg_id`、`key`、`bits`、`elapsed_us`、`result`、`attrs_result` 和 `cache_hit`。
`bits` 是用于验证签名的公钥位数（签发者/签名者的密钥），不是被签名证书自身的公钥位数；`elapsed_us` 除以 1000 得到毫秒。
`result=0` 表示验签成功，`attrs_result=0` 表示公钥元数据读取成功；元数据读取失败时仍原样执行验签，位数标为 0，并保留错误码。
`elapsed_us` 测量验签或缓存查询的耗时（缓存命中包含指纹计算）；诊断用公钥属性读取和日志打印不计入，仍包含任务抢占。
记录按调用顺序在请求结束后集中打印，不输出公钥/签名/哈希内容或密钥标识。

签名记录最多缓存 32 次，`TLS_VERIFY count=... recorded=... omitted=...` 明确报告超出数量，缓存满不影响真实验签。
只记录当前同步任务、当前 SSL 配置的握手步骤内的调用，其他任务和后续复用请求不重复记录。
优先对照 `stage=server_certificate_read_verify key=RSA bits=4096` 各条 `elapsed_us`，检验 RSA 4096 验签是否占据原约 22 秒证书处理时间；与阶段总耗时之差还包括证书解析、哈希、密钥导入及调度等工作。

例如先比较证书阶段、服务器签名验证阶段、客户端密钥交换阶段，找到接近原 23 秒等待的主耗时。
具体状态的工作范围对应 ESP-IDF 6.1 所带 mbedTLS TLS 1.2 实现；阶段不能进一步区分其中的密码计算和调度等待。

实现通过链接器 `--wrap=mbedtls_ssl_handshake_client_step` 和 `--wrap=psa_verify_hash` 调用原函数前后采样，不修改 SDK、不启用全量 mbedTLS debug。
仅诊断此 HTTP 客户端的 SSL 配置；原状态变化和返回码保持不变。
本诊断适配当前 IDF 6.1 的内部状态机入口与 `state` 字段；升级 IDF 时需复核链接入口和字段。
若出现 `no TLS 1.2 steps observed`，需检查链接包装是否生效或是否协商到其他 TLS 版本，不能把空数据解释为零耗时。
主机测试覆盖耗时累计、失败重试、不同配置过滤、连接复用时不重复日志，以及原函数返回值和状态透传；还覆盖逐次验签算法/位数、失败验证、元数据查询失败清理、缓存溢出报告及验签参数透传。真实分段耗时仍需重新运行仿真或实板采集。

## 证书签名验证缓存与长时间空闲

`4-wokwi.pcap` 显示旧连接在最后响应约 52 秒后关闭；第二次 ClientHello 带有 208 字节票据，说明客户端确实尝试恢复。
服务端票据 lifetime hint 为 300 秒，再次 ClientHello 距上次出票约 304 秒，并收到完整 Certificate / ServerKeyExchange，符合票据过期后回退完整握手的情况；服务端也可以因其他策略拒绝恢复。
25 秒 HEAD 用于维护 HTTP 连接；断线后仍由 ESP-IDF 保存和尝试票据。过期/被拒绝时自动回退完整握手，不依赖恢复成功。

缓存实现为 `sync_verify_cache`：覆盖链中网站证书、中间证书与信任锚对其子证书的成功验签结果；无需重复保存服务器每次都会发送的 DER 证书。
公钥身份使用导出的公钥内容，不使用会被回收的 PSA key id；SHA-256 缓存键绑定公钥类型、位数、usage/algorithm/lifetime 策略、验签算法、完整待验哈希与完整签名，以及各字段长度。
只在当前同步任务、当前客户端 TLS 1.2 握手步骤内启用，不影响其他任务的 PSA 调用。

- 只缓存实际验签成功的结果；失败、导出公钥失败或计算指纹失败时仍调用原 PSA 验签。
- 每次 X.509 验证仍重新解析证书并检查时间、主机名、用途、信任路径；缓存命中不等于信任整张证书，不修改证书校验回调或 flags。
- 任意公钥、策略、哈希、签名或算法变化都会缓存未命中；不缓存服务器临时密钥对应的整个握手结果。
- RAM 中最多 32 项，LRU 淘汰，成功缓存 1 小时后过期；重启清空。缓存不是无限容量，不保证断电后继续加速。
- `TLS_VERIFY ... cache_hit=1` 表示复用数学验签结果；`cache_hit=0` 表示真实验签或无法缓存。服务器握手签名随随机数变化，通常仍会未命中。

冷启动仍需一次完整证书校验，启动预热将其提前到后台。之后票据失效时，缓存命中可以避免重复的两次 RSA 4096 验签；证书链变更或缓存过期会重新验证。
主机替身覆盖缓存输入绑定、失败不缓存、元数据/导出/哈希失败回退、TTL 和 LRU；固件构建不能替代实际断网、票据恢复及缓存提速的运行验证。

## Component Registry 调研（2026-10-23）

检索了 Wi-Fi、Wi-Fi manager、HTTP、TLS 组件及其 README，未发现已证实将业务 API 启动预热、连接复用、TLS 会话恢复及 C6/Wokwi 冷握手加速全部集成的 Wi-Fi 组件。

| 候选 | 已文档化的能力 | 对当前慢握手的适用性 |
|---|---|---|
| [78/esp-wifi-connect 3.3.1](https://components.espressif.com/components/78/esp-wifi-connect/versions/3.3.1/readme) | 已存网络连接、配网门户、多 AP、频道缓存、SmartConfig，C++ 接口 | 可替换配网管理，不负责业务 HTTPS 预热或密码计算 |
| [espressif/esp_wifi_service 0.5.3](https://components.espressif.com/components/espressif/esp_wifi_service/versions/0.5.3/readme) | 官方服务化凭据存储、Web/BluFi 配网、自动选网、网络质量探测 | 适合扩展产品配网；未文档化当前 API 的 TLS 加速 |
| [embedblocks/wifi-manager 0.4.1](https://components.espressif.com/components/embedblocks/wifi-manager/versions/0.4.1/readme) | C 接口、重连、多 AP、BLE 配网；README 的 C6 状态为 Expected to work | 尚无 C6 实测保证，且不封装业务 HTTPS |
| [espressif/brookesia_service_http 0.8.2](https://components.espressif.com/components/espressif/brookesia_service_http/versions/0.8.2/readme) | [HTTP 服务接口](https://docs.espressif.com/projects/esp-brookesia/en/latest/service/http.html)、异步请求、取消、事件及资源管理 | 封装 ESP-IDF HTTP；未找到覆盖全部所需优化的承诺 |

当前保持 ESP-IDF 内置 HTTP/TLS，无新增托管依赖；会话恢复见 [ESP-TLS v6.1 文档](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c6/api-reference/protocols/esp_tls.html)。

抓包显示首次握手服务器数据约 0.1 秒内到达，客户端在约 36–40 秒后才发送加密 HTTP；ServerKeyExchange 使用组 29（X25519）。
当前 ESP-IDF C6 ECC 驱动对 X25519 使用软件回退，值得比较服务器也支持的 P-256 与 C6 硬件 ECC 路径。
主机已验证服务器接受 P-256，但不能据此推断 ESP32 或 Wokwi 的实际耗时；当前已将此客户端密钥交换限制为 P-256，需另做仿真和实板性能对照。
