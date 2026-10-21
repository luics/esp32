# ESP-IDF Agent Instructions

This file is a reusable baseline for coding agents working in ESP-IDF application
and component repositories. Follow more specific instructions in the target
repository when they override this baseline.

## 当前项目：微雪 ESP32-C6-Zero

- 环境：ESP-IDF v6.1，EIM 环境名 `stable-mcp`；target 为 `esp32c6`，Flash 为 8 MB。
- 板载单颗 WS2812 的数据引脚为 GPIO8，使用 **RGB** 数据顺序；通过 RMT 驱动，
  上电灭灯，红（GPIO10）、绿（GPIO9/BOOT）、蓝（GPIO11）按钮分别触发对应颜色，
  最新按钮优先：立即切换颜色并重新计时 200 ms，到期无新按下则灭灯，无强制灭灯等待。
  单槽队列覆盖未处理的旧颜色；同色再次按下延长亮灯，按钮低电平有效、长按不重复。
  实板新增红、蓝按钮分别连接 GPIO10/GPIO11 与 GND；复位时松开 BOOT。
- 应用配置使用 `CONFIG_BLINK_RGB_GPIO`，默认值见 `sdkconfig.defaults`；旧符号
  `CONFIG_BLINK_GPIO` 已停用。组件接口与行为见 [blink 说明](components/blink/README.md)；
  绿色按钮保留 `CONFIG_BUTTON_INPUT_GPIO`，红、蓝分别使用 `CONFIG_BUTTON_INPUT_RED_GPIO`、
  `CONFIG_BUTTON_INPUT_BLUE_GPIO`；接口见 [button_input 说明](components/button_input/README.md)。
- 报警器组件 [beep](components/beep/README.md) 使用 GPIO3 外接无源压电蜂鸣器；
  任意颜色按钮按下即触发 2 kHz、100 ms 短鸣，后台任务独立处理，连续触发重新计时。
  默认值为 `CONFIG_BEEP_GPIO`、`CONFIG_BEEP_FREQUENCY_HZ`、`CONFIG_BEEP_DURATION_MS`；
  独占 LEDC 低速 timer 0/channel 0，实板须外接适配 GPIO 驱动的低电流蜂鸣器。
- 显示组件 [display](components/display/README.md) 驱动 ILI9341 240×320 彩色 LCD；
  点击按钮后更新对应颜色背景，并居中显示“红色/绿色/蓝色”，画面保留到下次事件。
  SPI2 独占，SCK/MOSI/CS/DC/RST 默认 GPIO6/7/18/19/20；后台分条带刷新，只保留最新颜色。
  Wokwi 不提供 RST/背光引脚，RST=20 只需在实板接线。
- 控制台保留 UART 输出和 USB Serial/JTAG 辅助输出，分别用于 Wokwi 与实板原生 USB 日志。

构建命令（重新配置的确认规则见下文）：

```sh
eim run "idf.py reconfigure" stable-mcp
eim run "idf.py build" stable-mcp
```

实板使用以上命令，保持 RGB 数据顺序；Wokwi NeoPixel 使用 GRB，单独构建：

```sh
eim run "bash tools/build_wokwi.sh reconfigure build" stable-mcp
```

该脚本合并 `sdkconfig.defaults` 与 `sdkconfig.defaults.wokwi`，使用 `build-wokwi/sdkconfig`，
启用 `CONFIG_BLINK_LED_ORDER_GRB`；实板 `build/` 及根目录 `sdkconfig` 保持独立。
重新配置后确认灯珠 GPIO8、红/绿/蓝按钮 GPIO10/9/11；已有配置可能覆盖 defaults。
在 VS Code 执行 **Wokwi: Start Simulator**；固件路径由 `wokwi.toml` 指定，使用
`build-wokwi/flasher_args.json` 和 `build-wokwi/esp32_blink.elf`，无需另行合并固件。

Wokwi 使用 DevKitC-1 替代板型，并用右侧独立 `wokwi-neopixel` 模拟 Zero 的板载灯：
GPIO8 → DIN、5V → VDD、GND → VSS；三个按钮分别连接 GPIO10/9/11 与 GND。
蜂鸣器正极（2）接 GPIO3，负极（1）接 GND；验证每个按钮触发短鸣，长按不重复。
点击红、绿、蓝按钮，或聚焦仿真图后按 R/G/B，分别验证红、绿、蓝闪光；板上 BOOT 对应绿色。
长按只触发一次；快速按不同颜色时，验证立即切色并从最新事件重新计时，旧事件不补播。
验证时实际观察三种颜色的亮灭，以及屏幕对应背景色、居中文字与快速切换，不能只检查启动日志；
板上的常亮红灯是电源指示灯。替代模型不复现 Zero 的完整硬件；仿真使用 GRB 的独立固件，
实板使用默认 RGB 固件。实板烧录验证尚未进行。

硬件依据：[微雪引脚图](https://docs.waveshare.com/ESP32-C6-Zero)、
[RGB 顺序说明](https://docs.waveshare.com/ESP32-C6-Zero/FAQ)。
驱动与仿真参考：[led_strip](https://components.espressif.com/components/espressif/led_strip/versions/3.1.0~1/readme)、
[Wokwi 支持板型](https://docs.wokwi.com/guides/esp32)。

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
