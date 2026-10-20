# ESP-IDF project guidance

## Local development environment

- This Mac uses Espressif Installation Manager (EIM) to manage ESP-IDF and its toolchains. Prefer `eim run "idf.py ..."` for commands so the selected IDF environment is activated consistently. The ESP-IDF Tools Local MCP Server is registered in the local T3/Codex environment as `esp-idf-tools`; it runs `eim run "idf.py mcp-server"` and uses `IDF_MCP_WORKSPACE_FOLDER` for this project.
- Current local setup: EIM 0.20.1; ESP-IDF 6.1.0 is installed as `stable-mcp` under `~/.espressif/stable-mcp/esp-idf`, with its managed Python environment and Espressif toolchains under `~/.espressif/tools/`.
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
