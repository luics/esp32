# ESP-IDF Agent Instructions

This file is a reusable baseline for coding agents working in ESP-IDF application
and component repositories. Follow more specific instructions in the target
repository when they override this baseline.

## 当前项目：摩斯码电报机（微雪 ESP32-C6-Zero）

项目说明与开发规范统一维护在本文件。根目录 `README.md` 仅保留本文件引用及维护提示，
不要在其中添加其他内容；组件接口仍维护在各组件 README。
环境：ESP-IDF v6.1，EIM 环境 `stable-mcp`，target `esp32c6`，Flash 8 MB。
配置默认值见 `sdkconfig.defaults`；`volume_control` 和蜂鸣器固定时长配置已移除。

### 操作与接线

| 功能 | 端口与行为 |
|---|---|
| 摩斯码按键（红） | GPIO0 ↔ GND；单键模式按住时长识别；双键模式触发标准点 |
| 划按键（实板黑 / Wokwi 绿） | GPIO1 ↔ GND；仅双键模式触发标准划，单键模式不可用 |
| 模式按钮（黄） | GPIO2 ↔ GND；每次按下切换 S 单键 / D 双键模式，长按不重复切换 |
| 历史翻页按钮（绿 / BOOT） | GPIO9 ↔ GND；按下一次翻一页，末页回首页，不发送点划 |
| 复位按钮（蓝） | GPIO11 ↔ GND；配置项 `CONFIG_BUTTON_INPUT_BLUE_GPIO` |
| 板载点划 RGB LED | GPIO8；实板 RGB，Wokwi GRB；点红、划绿 |
| 外接模式提示 WS2812 DIN | GPIO5；GRB；模式按钮每次按下闪黄光 200 ms，独立于点划声光 |
| 无源低电流压电蜂鸣器 | 正极 GPIO3，负极 GND；2 kHz，独占 LEDC 低速 timer0/channel0 |
| 间隔电位器 | 两端 3V3 / GND，滑臂 GPIO4 / ADC1 CH4；启用 `CONFIG_INTERVAL_CONTROL_ENABLED` 后读取，实板默认关闭、Wokwi 默认启用 |
| SSD1327 CLK / DIN / CS / DC / RST | GPIO21 / 22 / 18 / 19 / 20；侧边排针，独占 SPI2、4 MHz；VCC→3V3，BS1/BS2→0 |
| Wokwi SH1107 SCL / SDA | GPIO6 / GPIO7；VCC 接 3V3，GND 接地，I2C 地址 0x3C |

- 间隔单位从旋钮低到高为 200/150/100（默认）/60 ms，切档带迟滞；未启用电位器时固定 100 ms 且不占用 ADC 引脚；启用后上电读取实际位置。
  当前字符的单位保持不变，新档位从下一个字符生效；红键阈值在按下时锁定，按住时换档不改变此次识别。
- 上电与蓝键复位后默认 D 双键模式；模式按钮可切换为 S 单键模式。S 模式下划键不可用。红键按下亮板载红灯并以 2 kHz 连续发声，按住达到 3 单位切换板载绿灯提示；松开立即关闭声光并提交一个点划。外接灯只在模式按钮每次按下时闪黄光 200 ms。
  声光时长完全跟随按住时间，不固定播放 1/3 单位，不人为延长静默。边沿事件通过 64 槽 FIFO 传递；
  溢出时立即关闭声光、取消未完成按键并等待松开，避免卡在持续发声状态；绿键长按不重复翻页。
  静默从实际关闭声光起算，3 单位确认字符、7 单位确认词，间隔不叠加。
  同一字符的下一次红键须在前一信号结束后 3 单位内按下；按住期间暂停字符/词提交，避免长划拆分字符。
  输入主任务优先级为 3，高于显示任务的 2；等待队列时由边沿唤醒，避免刷新期间短按声光被压缩。
  主任务非阻塞处理边沿；红键按住及发声期间仍响应绿键翻页及蓝键复位。
- D 双键模式：红键点（1 单位红光/声音）、黑键划（3 单位绿光/声音），声音保持 2 kHz。
  每次消抖按下缓存一个动作，松开不截短信号；64 槽 FIFO 保留顺序，信号间至少静默 1 单位。
  长按在每个信号结束及静默后继续触发，松开停止重复；显式点击优先，两键同时长按时交替点划。
  切换模式保留历史，当前信号结束后生效，取消未发送缓存及长按重复；跨模式按住的键须先松开再按。
  单键按住时请求切换，仍按原模式在松开时识别本次输入。发送/排队中持续响应翻页、模式切换及蓝键复位。
- 蓝键不经过点划 FIFO：先关闭声光，再 `esp_restart()`，复用 `app_main()` 初始化，
  清空输入、边沿事件与历史，恢复固定 100 ms（启用电位器时重新读取旋钮）并恢复新词指示。长按跨重启等待松开再使能，避免循环重启；复位时松开 BOOT。
- Wokwi 使用 DevKitC-1，其 GPIO8 板载 RGB 灯表示点划反馈；独立 NeoPixel 接 GPIO5 表示模式提示灯，不复现完整实板。
  标题为“摩斯码电报机”，功能标签仅显示功能名称，不展示 GPIO 编号；R/D/M 为点键/划键/模式切换快捷键。仿真不额外放置独立历史翻页和复位按钮；历史仍可通过开发板 BOOT 翻页。
  保留 UART/USB Serial-JTAG 日志；板上常亮红色电源灯与 WS2812 信号灯不同。
- 调整 Wokwi 布局与走线时，优先减少线与元件的交叉，其次减少线与线的交叉。
- 所有走线优先保持平直；需要转弯时，在满足上述避让优先级的前提下，尽量减少转角。

### 屏幕与历史

- 真实屏幕规格：1.5 英寸 SSD1327，128×128、16 级灰度，支持 SPI/I2C。
  Wokwi 使用原生 `board-grove-oled-sh1107` 128×128 黑白 OLED 近似（仅 I2C），不模拟灰度或 SSD1327 协议。
  仿真通过 `sdkconfig.defaults.wokwi` 启用 `CONFIG_DISPLAY_WOKWI_SH1107`；实板构建使用 SSD1327 四线 SPI 驱动，4 bit 灰度缓冲，白底、黑字与灰色历史。
- 始终白底；标题稍大并加粗，输入与状态为黑色，历史为深灰色（SSD1327 灰度 6/15，Wokwi 黑白近似仍用黑色）；上方显示输入点划与候选解码，不再显示 SET 行；状态行固定在屏幕底部，左侧为页码 x/x，右侧为 S xxxMS / D xxxMS（当前模式及字符单位），历史内容排列在其上方。
  右上角小黑点指示可开始新词：初始显示，单键红键按下或双键发送开始时隐藏，信号结束后静默满 7 个锁定单位再显示并保持；按住时隐藏，不再周期闪烁。
- 支持 A–Z、0–9 及常用标点；未知编码显示 `?`，超过 8 个点划显示 `INVALID`。
  候选解码随输入变化，静默满 3 单位才提交历史；输入中的字符不进历史。
- 底部每组格式为 `> {字符或单词} {摩斯码}`，例如 `> HI .... ..`。
  静默满 7 单位后按词合并，未成词按字符分组；普通词尽量不跨页，超长词换行续页。
  SSD1327 与 SH1107 历史区均为每页 6 行、每行 20 字符。
  历史按最新字符/词组优先排列，词内顺序保持不变；新增历史回到第一页首行，绿键手动循环翻页以浏览全部历史，不再自动轮播。
- 全部历史动态存 RAM，断电清空；确认字符时输出串口日志。RAM 耗尽时报告错误并停止，不静默丢弃旧历史。

### 组件职责

- `main/`：仅启动入口 `app_main.c`、构建声明与工程级引脚配置；入口调用 `telegraph_app_run()`。
- `components/morse/`：纯 C 编解码、字符/词间隔、完整历史与文本分页，公共接口位于 `include/`。
- `components/telegraph/`：纯 C 单键识别与双键发送状态机，公开依赖 `morse`。
- `components/telegraph_app/`：外设初始化、边沿队列、声光同步、显示快照与复位调度，对外仅提供运行入口。
- 外设组件继续负责各自驱动；新增业务逻辑按上述职责归属，组件接口和使用约定见各自 README。

### 构建与验证

修改 Kconfig/defaults 后先取得重新配置授权（详见下文），再执行实板与仿真构建：

```sh
eim run "idf.py reconfigure build" stable-mcp
eim run "bash tools/build_wokwi.sh reconfigure build" stable-mcp
```

实板使用根 `sdkconfig` 与 `build/`，仿真配置与产物在 `build-wokwi/`，互不覆盖。
VS Code 运行 **Wokwi: Start Simulator**，旋钮初值 `value=640`，默认 100 ms。
主机逻辑测试：

```sh
python3 tools/test_host.py
```

仿真须观察灯光、声音与屏幕，不能仅检查启动日志：

- 确认白底黑字、上电灭灯静音、四档速度与新词指示；R 按下立即亮板载灯发声、松开立即关闭；M 点击仅触发外接灯黄光 200 ms，不改变板载点划灯；30/200/500 ms 按住均保持实际声光时长，不在松开后补播。
  检查 3 单位阈值两侧、按住时换档、长按只产生一个划；显示刷新时连续短按不漏声、不补播或延长；BOOT（实板绿键）只翻页，长按不重复。
- 输入 `.... . .-.. .-.. --- / .-- --- .-. .-.. -..` 得到 HELLO WORLD；检查 3/7 单位提交、
  当前输入不进历史、字符/词分组、BOOT 手动循环翻页及空闲时不自动翻页；小黑点初始显示、R 按住隐藏、松开后满 7 单位才显示，换档和翻页不重置静默。
- 确认上电默认 D，M 可切至 S 再切回 D；快速交替点击 R/D，验证 FIFO、1/3 单位声光和 1 单位静默；长按重复，松开停止重复且当前信号完整结束。
  检查四档速度、字符内换档锁定、同时长按交替、发送中切换、S 下黑键无效、缓存溢出恢复。
- 蓝键在空闲、红键按住发声、边沿积压或双键排队时均能关闭声光并复位；历史归零、固定 100 ms 或已启用的旋钮实际档位恢复、新词指示显示。
  长按跨重启只重启一次，松开再按可再次复位。

实板烧录需用户明确授权端口和设备，详见下文 Validation。

## Discover the Project First

- Locate the ESP-IDF project root before running commands. A project root normally
  contains a top-level `CMakeLists.txt` with a `project(...)` call.
- In repositories with multiple applications or test apps, identify the affected
  project roots and run commands from each relevant root.
- Read the repository README, CI configuration, top-level `CMakeLists.txt`,
  `sdkconfig.defaults*`, component manifests, partition tables, and existing build
  scripts before choosing commands.
- Determine the required ESP-IDF version, supported targets, board assumptions,
  and project-specific validation commands. Prefer repository and CI commands over
  generic commands in this file.
- If no project is present, do not create a scaffold unless the user requested a
  new project or the task clearly requires one.
- When a new project scaffold is required, use
  `idf.py create-project <PROJECT_NAME>` instead of manually generating the
  boilerplate files.

## Environment and Documentation

- Ensure the correct ESP-IDF environment is active before using `idf.py`.
- Prefer EIM CLI for installing and selecting ESP-IDF versions when the repository
  does not prescribe another setup. Use these commands as needed:
  - Verify EIM is available: `eim --version`
  - List installed ESP-IDF versions, their absolute installation paths, and the
    selected version: `eim list`
  - Select an installed version: `eim select <ESP_IDF_VERSION>`
  - Run with the selected version: `eim run "idf.py <command>"`
  - Run with a specific version without changing the selection:
    `eim run "idf.py <command>" <ESP_IDF_VERSION>`
  - Verify a specific ESP-IDF environment before building:
    `eim run "idf.py --version" <ESP_IDF_VERSION>`
  - Install a missing version when authorized:
    `eim install -i <ESP_IDF_VERSION>`
  - Open the interactive installer when requested: `eim wizard`
- Use the installation path reported by `eim list` when a task needs `IDF_PATH`
  or direct access to a matching ESP-IDF checkout. Do not guess installation
  paths.
- If the environment is sourced directly, verify that `idf.py --version` matches
  the version required by the project.
- Prefer `idf.py` workflows over invoking CMake or Ninja directly.
- Use ESP-IDF documentation and migration guides matching the project's version
  and target. Do not rely on `latest` documentation for version-sensitive APIs.

## Standard Commands

- Reconfigure: `idf.py reconfigure`
- Build: `idf.py build`
- Select target: `idf.py set-target <TARGET>`
- Flash: `idf.py -p <PORT> flash`
- Monitor: `idf.py -p <PORT> monitor`
- Flash and monitor: `idf.py -p <PORT> flash monitor`

`idf.py set-target` already clears the build directory, moves the previous
`sdkconfig` to `sdkconfig.old`, and reconfigures the project. Ask the user before
running it because it replaces configuration and build state. Do not run a
redundant `idf.py fullclean` afterward. Use `fullclean` only when stale artifacts
are a demonstrated problem, and ask before deleting build state.

## Configuration and Generated Files

- Treat `sdkconfig` as generated configuration. Do not edit it directly unless the
  user explicitly requests that exact file.
- Store intentional project defaults in `sdkconfig.defaults` and target-specific
  overrides in files such as `sdkconfig.defaults.esp32c6`. Keep changes minimal,
  then ask the user whether to run `idf.py reconfigure`.
- A target-specific defaults file is used only when the base
  `sdkconfig.defaults` file also exists; create an empty base file when no common
  defaults are needed.
- `Kconfig` and `Kconfig.projbuild` define configuration symbols, prompts,
  dependencies, and defaults. They do not store the project's selected values.
- Use component `Kconfig` for component-local options. Use `Kconfig.projbuild`
  only when an option must appear at project scope.
- Namespace new symbols, provide a prompt, a sensible default, and useful help
  text. Put configurable hardware assignments such as GPIOs in Kconfig instead
  of hardcoding them.
- After changing `Kconfig` or `Kconfig.projbuild`, ask the user whether to run
  `idf.py reconfigure` before the next build or validation step.
- `idf.py build` normally detects Kconfig definition changes and invokes CMake
  reconfiguration automatically. An explicit `idf.py reconfigure` remains useful
  for validating configuration separately before a build.
- Changing a Kconfig default does not necessarily replace a value already stored
  in `sdkconfig`. When the project's selected value must change, update the
  appropriate `sdkconfig.defaults*` file and reconfigure with user approval.
- Never edit files under `build/` or `managed_components/`.
- Never edit `dependencies.lock` manually. Allow Component Manager to regenerate
  it, and commit the resulting lock file when the application repository tracks
  managed dependencies for reproducible builds.

## Components and Code Conventions

- 新增或修改代码时，为主流程添加简洁的中文注释，说明初始化、事件传递、
  关键状态变化及错误清理的意图；注释应随实现同步更新，避免逐行复述代码。
- Before implementing a dependency, search the ESP Component Registry and inspect
  existing `idf_component.yml` manifests. Prefer adding a suitable managed
  dependency over duplicating it locally.
- Add a managed dependency with
  `idf.py add-dependency "namespace/component^<VERSION>"`. From the project root,
  this updates the `main` component by default. Use `--component=<NAME>` for a
  component under `components/` or `--path=<PATH>` for another component
  directory.
- Prefer `idf.py create-component <name>` when creating a project-local component.
  Keep public headers in `include/`, register sources with
  `idf_component_register(...)`, and declare dependencies explicitly with
  `REQUIRES` or `PRIV_REQUIRES`.
- Keep reusable functionality in components rather than a monolithic application.
  A component intended for publication should be self-contained, documented,
  licensed, versioned, and tested in one or more test applications.
- Keep the firmware entry point as `void app_main(void)`.
- Prefer ESP-IDF APIs and `ESP_LOGI/W/E` for application logging. Handle
  `esp_err_t` results explicitly and avoid long blocking operations without
  yielding where appropriate.

## Validation

- Match validation to the change and follow the repository's CI matrix:
  - documentation-only changes: run relevant documentation checks
  - Kconfig changes: ask whether to run `idf.py reconfigure`
  - manifest or build-system changes: run `idf.py reconfigure`
  - firmware changes: build every affected supported target that is practical
  - component changes: build or test the component's relevant test applications
- Use host-based tests before hardware tests when both cover the behavior.
- Do not flash merely because a serial device is connected. Flash only when the
  user requested it or explicitly authorized hardware validation.
- Before flashing, confirm the port, target, and that the device is safe to
  overwrite. Never assume a port or monitor baud rate.
- When authorized hardware validation is required, use the project's documented
  command, normally `idf.py -p <PORT> flash monitor`, and capture the relevant
  runtime output.
- Report the exact commands, ESP-IDF version, target, and tests run. Summarize
  configuration changes and clearly state anything not validated.

## Safety and Collaboration

- 本次发布授权记录（2026-10-21）：用户明确允许将提交 `9f8f5cc`
  推送到 `https://github.com/luics/esp32.git` 的 `main` 分支；该授权仅适用于此提交。
- Keep changes narrowly scoped. If unrelated workspace changes appear, stop and
  ask how to proceed.
- Ask before destructive actions such as `fullclean`, deleting files, erasing
  flash, changing eFuses, or broadly regenerating configuration.
- Do not commit build outputs, transient logs, generated binaries, or
  `managed_components/` unless the repository explicitly requires them.
- Never hardcode or commit SSIDs, passwords, API keys, certificates, private
  keys, tokens, or device secrets. Use placeholders, configuration, environment
  variables, or secure provisioning.
- Review partition size and OTA/rollback implications when changes affect
  application size, partition tables, or update behavior.

## Specialized Guidance

When these files are present, use the relevant workflow:

- Firmware development: `skills/esp-idf/SKILL.md`
- Component creation and publication:
  `skills/esp-idf-components/SKILL.md`
- ESP-IDF 5.x to 6.0 migration:
  `skills/esp-idf-v6-migration/SKILL.md`
