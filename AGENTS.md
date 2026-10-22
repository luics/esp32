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
| 点按钮（红） | GPIO10 ↔ GND；按下发送 `.`，红光及声音持续 1 单位 |
| 划按钮（绿 / BOOT） | GPIO9 ↔ GND；按下发送 `-`，绿光及声音持续 3 单位 |
| 复位按钮（蓝） | GPIO11 ↔ GND；配置项 `CONFIG_BUTTON_INPUT_BLUE_GPIO` |
| WS2812 DIN | GPIO8；实板 RGB，Wokwi 独立固件 GRB |
| 无源低电流压电蜂鸣器 | 正极 GPIO3，负极 GND；2 kHz，独占 LEDC 低速 timer0/channel0 |
| 间隔电位器 | 两端 3V3 / GND，滑臂 GPIO4 / ADC1 CH4；由 `interval_control` 读取 |
| ILI9341 SCK / MOSI / CS / DC / RST | GPIO6 / 7 / 18 / 19 / 20；独占 SPI2，Wokwi 不接 RST |

- 间隔单位从旋钮低到高为 200（默认）/150/100/60 ms，切档带迟滞；上电读取实际位置。
  当前字符的单位保持不变，新档位从下一个字符生效。
- 按钮长按不重复；快速点击按 FIFO 排队，最多等待 32 个点划，满队列拒绝新点击并告警。
  相邻信号至少静默 1 单位；静默从声光结束起算，3 单位确认字符、7 单位确认词，间隔不叠加。
  同一字符的后续点划须在前一信号结束后 3 单位内点击。
- 蓝键不经过点划 FIFO：先关闭声光，再 `esp_restart()`，复用 `app_main()` 初始化，
  清空输入、队列与历史，重新读取旋钮并开始节拍。长按跨重启等待松开再使能，避免循环重启；复位时松开 BOOT。
- Wokwi 使用 DevKitC-1 与独立 NeoPixel 替代 Zero 板载硬件，不复现完整实板。
  标题为“摩斯码电报机”，功能标签使用“功能 · port”；R/G/B 为点/划/复位快捷键。
  保留 UART/USB Serial-JTAG 日志；板上常亮红色电源灯与 WS2812 信号灯不同。

### 屏幕与历史

- ILI9341 240×320 始终白底黑字；上方显示档位、当前字符单位、输入点划与候选解码。
  右上角小黑点按当前档位显示 3 单位、隐藏 3 单位。
- 支持 A–Z、0–9 及常用标点；未知编码显示 `?`，超过 8 个点划显示 `INVALID`。
  候选解码随输入变化，静默满 3 单位才提交历史；输入中的字符不进历史。
- 底部每组格式为 `> {字符或单词} {摩斯码}`，例如 `> HI .... ..`。
  静默满 7 单位后按词合并，未成词按字符分组；普通词尽量不跨页，超长词换行续页。
  新增历史先显示最新页，随后每 4 秒循环翻页，覆盖本次开机全部历史。
- 全部历史动态存 RAM，断电清空；确认字符时输出串口日志。RAM 耗尽时报告错误并停止，不静默丢弃旧历史。

### 构建与验证

修改 Kconfig/defaults 后先取得重新配置授权（详见下文），再执行实板与仿真构建：

```sh
eim run "idf.py reconfigure build" stable-mcp
eim run "bash tools/build_wokwi.sh reconfigure build" stable-mcp
```

实板使用根 `sdkconfig` 与 `build/`，仿真配置与产物在 `build-wokwi/`，互不覆盖。
VS Code 运行 **Wokwi: Start Simulator**，旋钮默认最低档 200 ms。
主机逻辑测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -I main main/morse.c tests/test_morse.c -o /tmp/esp32-test-morse
/tmp/esp32-test-morse
```

仿真须观察灯光、声音与屏幕，不能仅检查启动日志：

- 确认白底黑字、上电灭灯静音、四档速度与节拍；R/G 产生 1/3 单位红/绿光及同步声音，长按不重复、快速点击不截断。
- 输入 `.... . .-.. .-.. --- / .-- --- .-. .-.. -..` 得到 HELLO WORLD；检查 3/7 单位提交、
  当前输入不进历史、字符/词分组及多页循环翻页。
- 蓝键在空闲、发声、队列积压时均能关闭声光并复位；历史归零、旋钮档位恢复、节拍继续。
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
