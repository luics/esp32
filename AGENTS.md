# ESP-IDF project guidance

## ESP32 blink

Minimal ESP-IDF GPIO blink example targeting ESP32-C6-DevKitC-1. `sdkconfig.defaults` selects `esp32c6`, 8 MB flash, and GPIO2. Set `CONFIG_BLINK_GPIO` in `idf.py menuconfig` to match the LED wiring. Connect GPIO2 through a 330 Ω resistor to an external LED's anode and connect its cathode to GND. GPIO2 is not a strapping pin on ESP32-C6. The board's GPIO8 LED is an addressable RGB LED, not a plain GPIO LED; this example controls only the external LED. Avoid C6 strapping pins GPIO4/5/8/9/15, USB/JTAG pins GPIO12/13, and flash pins GPIO24–30 for this example. See the [board pinout](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html#header-block).

```sh
eim run "idf.py set-target esp32c6"
eim run "idf.py build"
```

To run on hardware, connect a compatible ESP32 development board over a USB data cable, identify its serial port, then use `idf.py -p <PORT> flash monitor` in the active ESP-IDF environment. A successful build alone does not verify the LED.

### Wokwi simulation in VS Code

Install and activate the Wokwi extension, open this project folder, and build with `eim run "idf.py build"`. Run `Wokwi: Start Simulator` from the command palette. No physical board, serial port, or flashing is needed.

`wokwi.toml` loads the images listed in `build/flasher_args.json` and the debug symbols in `build/esp32_blink.elf`. `diagram.json` uses `board-esp32-c6-devkitc-1` with `esp:2` connected through a 330-ohm resistor to a red LED's anode, with its cathode connected to `esp:GND.1`. This C6 model uses numeric GPIO pin names, unlike the classic DevKit V1's `D2`. The expected behavior is 500 ms on and 500 ms off. If you change `CONFIG_BLINK_GPIO`, update the diagram wiring to match; if you change the target chip, replace the simulated board as well. Rebuild after source changes. After editing the diagram, close and reopen the simulator with `Wokwi: Start Simulator` to reload the wiring.

## Local development environment

- This Mac uses Espressif Installation Manager (EIM) to manage ESP-IDF and its toolchains. Prefer `eim run "idf.py ..."` for commands so the selected IDF environment is activated consistently. The ESP-IDF Tools Local MCP Server is registered in the local T3/Codex environment as `esp-idf-tools`; it runs `eim run "idf.py mcp-server"` and uses `IDF_MCP_WORKSPACE_FOLDER` for this project.
- Current local setup: EIM 0.20.1; ESP-IDF 6.1.0 is installed as `stable-mcp` under `~/.espressif/stable-mcp/esp-idf`, with its managed Python environment and Espressif toolchains under `~/.espressif/tools/`.
- ESP32-C6 uses the RISC-V toolchain. This Mac additionally has `riscv32-esp-elf` version `esp-15.2.0_20251204` and `riscv32-esp-elf-gdb` version `17.1_20260402` under `~/.espressif/tools/tools/`; the local EIM activation script includes their `bin/` directories. If regenerating the EIM environment, retain ESP32-C6 toolchain support. Reopen ESP-IDF terminals or reselect the ESP-IDF setup in VS Code after environment changes.
- The Espressif Documentation MCP Server is registered as `espressif-docs` at `https://mcp.espressif.com/docs`. It is remote and needs internet access plus first-time GitHub or WeChat authentication. Use it for current ESP-IDF, SoC, datasheet, and hardware guidance; local project sources take precedence when they already answer the question.
- For ESP-IDF or Espressif SoC work, consult the documentation MCP when details are version-specific, hardware-specific, or uncertain. The Tools MCP operates on the current valid ESP-IDF project and can report real build, flash, monitor, and connected serial-device results; do not claim a flash or device behavior without a connected target and successful command output.
- Use ESP-IDF's CMake-based project structure and `idf.py` workflows. Keep build artifacts in `build/`; do not commit generated build output or secrets.

## Components and implementation choices

- Before implementing a peripheral, protocol, display, audio, networking, or other substantial feature, search the [Espressif Component Registry](https://components.espressif.com/) for a maintained component that fits the exact chip and ESP-IDF version. Prefer a suitable registry component over copied vendor code or a hand-rolled subsystem.
- Add selected dependencies with `idf.py add-dependency "namespace/component_name"` (or the Component Manager manifest), pin a compatible version, and review the component's license, supported targets, maintenance status, and transitive dependencies. Do not add a component for a tiny operation already covered well by ESP-IDF's built-in driver/API; e.g. a basic GPIO blink should use the built-in GPIO driver.
- Confirm the target chip and board pinout before choosing GPIOs. Board LEDs may be active-low, addressable, or wired to different pins. Make board-specific pins configurable rather than assuming one universal pin.

## Hardware and verification

- Build for the actual target with `idf.py set-target <target>` and `idf.py build`. Before flashing, confirm the connected serial port and board; flashing is a hardware write operation.
- A successful build verifies compilation only. For LED behavior, verify on a simulator that models the selected board/pin or on the physical board; report which was used. If no suitable simulator is available, state the exact board and wiring needed for physical verification.
- Wokwi supports ESP-IDF 6.0+ simulation. The web simulator is useful for quick visual experiments; for a repeatable local `idf.py wokwi` workflow, the `idf-wokwi` extension requires a Wokwi API token (`WOKWI_CLI_TOKEN`). Do not commit the token. A browser-side Wokwi build is a separate build and does not replace compiling the local checkout.
- Keep examples safe at boot: avoid driving strap, flash, USB/JTAG, or reserved pins without checking the chip and board documentation.

## Reference pages

- ESP-IDF setup and resources: <https://developer.espressif.com/tags/esp-idf/zh-cn/>
- ESP-IDF programming guide: <https://docs.espressif.com/projects/esp-idf/en/latest/>
- Component Registry: <https://components.espressif.com/>
- ESP-IDF Tools MCP setup: <https://developer.espressif.com/blog/2026/04/esp-idf-tools-mcp-server/>
- Espressif Documentation MCP setup: <https://developer.espressif.com/blog/2026/04/doc-mcp-server/>
