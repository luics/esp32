"""运行组件的主机测试及真实应用入口的外设替身测试，无需 ESP-IDF 环境。"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ROOT / "components"
MORSE = COMPONENTS / "morse"
TELEGRAPH = COMPONENTS / "telegraph"

# 与组件边界对应地编译，每组测试只获得其实际需要的头文件和源文件。
cases = [
    ("morse", [MORSE / "include"], [MORSE / "morse.c"]),
    ("telegraph", [MORSE / "include", TELEGRAPH / "include"],
     [MORSE / "morse.c", TELEGRAPH / "telegraph.c"]),
    ("keyer", [MORSE / "include", TELEGRAPH / "include"],
     [MORSE / "morse.c", TELEGRAPH / "telegraph.c", TELEGRAPH / "keyer.c"]),
    ("sh1107_pixels", [COMPONENTS / "display"], []),
    ("sync_outbox", [MORSE / "include", COMPONENTS / "telegraph_sync"],
     [MORSE / "morse.c", COMPONENTS / "telegraph_sync/sync_outbox.c"]),
]

with tempfile.TemporaryDirectory(prefix="esp32-host-tests-") as directory:
    for name, includes, sources in cases:
        binary = Path(directory) / name
        command = shlex.split(os.environ.get("CC", "cc"))
        command += ["-std=c11", "-Wall", "-Wextra", "-Werror"]
        for include in includes:
            command += ["-I", str(include)]
        command += [str(source) for source in sources]
        command += [str(ROOT / "tests" / f"test_{name}.c"), "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)

subprocess.run([sys.executable, str(ROOT / "tests/test_app_feedback.py")], check=True)
subprocess.run([sys.executable, str(ROOT / "tests/test_sync_http.py")], check=True)

subprocess.run([sys.executable, str(ROOT / "tests/test_sync_tls_timing.py")], check=True)

subprocess.run([sys.executable, str(ROOT / "tests/test_sync_verify_cache.py")], check=True)
