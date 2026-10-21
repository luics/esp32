# ESP-IDF Agent Instructions

This file is a reusable baseline for coding agents working in ESP-IDF application
and component repositories. Follow more specific instructions in the target
repository when they override this baseline.

## 当前项目：微雪 ESP32-C6-Zero

- 环境：ESP-IDF v6.1，EIM 环境名 `stable-mcp`；target 为 `esp32c6`，Flash 为 8 MB。
- 板载单颗 WS2812 的数据引脚为 GPIO8，使用 **RGB** 数据顺序；通过 RMT 驱动，
  蓝光亮、灭各 500 ms。实板无需外接 LED 或电阻。
- 应用配置使用 `CONFIG_BLINK_RGB_GPIO`，默认值见 `sdkconfig.defaults`；旧符号
  `CONFIG_BLINK_GPIO` 已停用。组件接口与行为见 [blink 说明](components/blink/README.md)。
- 控制台保留 UART 输出和 USB Serial/JTAG 辅助输出，分别用于 Wokwi 与实板原生 USB 日志。

构建命令（重新配置的确认规则见下文）：

```sh
eim run "idf.py reconfigure" stable-mcp
eim run "idf.py build" stable-mcp
```

重新配置后确认生成的 `sdkconfig` 中 `CONFIG_BLINK_RGB_GPIO=8`；已有配置可能覆盖 defaults。
在 VS Code 执行 **Wokwi: Start Simulator**；固件路径由 `wokwi.toml` 指定，使用
`build/flasher_args.json` 和 `build/esp32_blink.elf`，无需另行合并固件。

Wokwi 使用 DevKitC-1 替代板型，并用右侧独立 `wokwi-neopixel` 模拟 Zero 的板载灯：
GPIO8 → DIN、5V → VDD、GND → VSS。验证时实际观察蓝灯亮灭，不能只检查启动日志；
板上的常亮红灯是电源指示灯。替代模型不复现 Zero 的完整硬件，蓝光也不能验证
RGB/GRB 的红绿通道顺序；实板烧录验证尚未进行。

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
