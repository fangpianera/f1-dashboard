"""Run ESP-IDF idf.py from an MSys/Git-Bash shell by invoking idf.main()
directly (bypassing idf.py's MSYSTEM guard), with a native Windows env.

Usage:
    python idf_runner.py <idf.py arguments...>
Example:
    python idf_runner.py set-target esp32s3
    python idf_runner.py build
    python idf_runner.py -p COM3 flash monitor
"""
import os
import sys

IDF_PATH = r"C:\Users\Administrator\esp\v5.5.6\esp-idf"
TOOLS = r"C:\Espressif\tools"
VENV = TOOLS + r"\python\v5.5.6\venv"

os.environ["IDF_PATH"] = IDF_PATH
os.environ["IDF_TOOLS_PATH"] = TOOLS
os.environ["IDF_PYTHON_ENV_PATH"] = VENV
os.environ["IDF_PYTHON_ENV"] = VENV
os.environ["ESP_ROM_ELF_DIR"] = TOOLS + "\\esp-rom-elfs\\20260528\\"
os.environ["OPENOCD_SCRIPTS"] = (TOOLS +
    r"\openocd-esp32\v0.12.0-esp32-20260831\openocd-esp32\share\openocd\scripts")
os.environ["IDF_CCACHE_ENABLE"] = "1"

tool_paths = [
    TOOLS + r"\ccache\4.12.1\ccache-4.12.1-windows-x86_64",
    TOOLS + r"\cmake\3.30.2\bin",
    TOOLS + r"\dfu-util\0.11\dfu-util-0.11-win64",
    TOOLS + r"\esp-clang\esp-19.1.2_20250312\esp-clang\bin",
    TOOLS + r"\esp-rom-elfs\20260528",
    TOOLS + r"\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
    TOOLS + r"\idf-exe\1.0.3",
    TOOLS + r"\ninja\1.12.1",
    TOOLS + r"\openocd-esp32\v0.12.0-esp32-20260831\openocd-esp32\bin",
    TOOLS + r"\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin",
    TOOLS + r"\xtensa-esp-elf-gdb\17.1_20260402\xtensa-esp-elf-gdb\bin",
    TOOLS + r"\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin",
    VENV + r"\Scripts",
]
os.environ["PATH"] = ";".join(tool_paths) + ";" + os.environ.get("PATH", "")

for key in ("MSYSTEM", "MINGW_PREFIX", "MSYSTEM_CARCH", "MSYSTEM_CHOST"):
    os.environ.pop(key, None)

sys.argv = ["idf.py"] + sys.argv[1:]
sys.path.insert(0, IDF_PATH + r"\tools")
import idf
idf.main()
