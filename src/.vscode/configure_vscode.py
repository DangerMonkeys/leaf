import json
import os
import shutil
from typing import List


class PathProvider:
    arduino_path: str

    def __init__(self, arduino_path: str) -> None:
        self.arduino_path = arduino_path

    def get_esp32_path(self) -> str:
        return self.arduino_path + "\\packages\\esp32\\hardware\\esp32\\3.0.2"

    def get_gcc_path(self) -> str:
        return self.arduino_path + "\\packages\\esp32\\tools\\esp-xs3\\2302\\bin\\xtensa-esp32s3-elf-g++"

    def get_lib_path(self) -> str:
        return self.arduino_path + "\\packages\\esp32\\tools\\esp32-arduino-libs\\idf-release_v5.1-bd2b9390ef\\esp32s3"

    def get_additional_include_paths(self) -> List[str]:
        return [
            self.get_lib_path() + "\\qio_qspi\\include",
            self.get_esp32_path() + "\\variants\\esp32s3",
        ]

    def get_flags_path(self) -> str:
        return self.get_lib_path() + "\\flags"


def write_cpp_properties(path_provider: PathProvider) -> None:
    """Assumes current working directory of `src` folder in repo"""

    # If there's no existing c_cpp_properties.json, start from the template
    if not os.path.exists("./.vscode/c_cpp_properties.json"):
        shutil.copy("./.vscode/c_cpp_properties.json.template", "./.vscode/c_cpp_properties.json")

    # We'll be editing the contents of this file
    # Note: https://code.visualstudio.com/docs/cpp/c-cpp-properties-schema-reference
    with open("./.vscode/c_cpp_properties.json", "r") as f:
        cpp_props = json.load(f)

    # Find "Arduino" configuration
    arduino_cfg = [cfg for cfg in cpp_props["configurations"] if cfg["name"] == "Arduino"]
    if not arduino_cfg:
        raise ValueError("Could not find a configuration named 'Arduino' in c_cpp_properties.json")
    if len(arduino_cfg) > 1:
        raise ValueError(f"Found {len(arduino_cfg)} configurations named 'Arduino' in c_cpp_properties.json; expected only 1")
    arduino_cfg = arduino_cfg[0]

    # TODO: Perform all the below based on https://arduino.github.io/arduino-cli/1.0/platform-specification/

    # Specify compiler
    arduino_cfg["compilerPath"] = path_provider.get_gcc_path()

    # Specify include paths
    include_paths: list = arduino_cfg["includePath"]
    include_paths.clear()

    # Include everything in the vario folder
    include_paths.append(os.path.join("${workspaceFolder}", "vario", "**"))

    # Include the src folder of each local library
    for lib in os.scandir("libraries"):
        if not lib.is_dir():
            continue
        lib_src = os.path.join(lib, "src")
        if os.path.exists(lib_src):
            # Assume libraries with a `src` folder container their source in that folder
            include_paths.append(os.path.join("${workspaceFolder}", lib_src))
        else:
            # Assume libraries without a `src` folder contain their source at their root
            include_paths.append(os.path.join("${workspaceFolder}", lib))

    # Include the ESP32 core
    core_path = os.path.join(path_provider.get_esp32_path(), "cores", "esp32")
    if not os.path.exists(core_path):
        raise ValueError(f"Could not find ESP32 core path {core_path}")
    if not os.path.exists(os.path.join(core_path, "Arduino.h")):
        raise ValueError(f"Arduino.h not found in ESP32 core path {core_path}")
    include_paths.append(os.path.join(core_path, "**"))

    # Include each of the ESP32 libraries
    for lib in os.scandir(os.path.join(path_provider.get_esp32_path(), "libraries")):
        if not lib.is_dir():
            continue
        lib_src = os.path.join(lib, "src")
        if not os.path.exists(lib_src):
            raise ValueError(f"ESP32 library src folder does not exist: {lib_src}")
        include_paths.append(lib_src)

    # Include everything ESP32 specifies
    with open(os.path.join(path_provider.get_flags_path(), "includes"), "r") as f:
        includes = f.read().split(" ")
    for include in includes:
        if include.startswith("-"):
            if include == "-iwithprefixbefore":
                pass  # This is what we expect
            else:
                raise NotImplementedError(f"`include` flag {include} not yet supported")
            continue
        include_path = os.path.join(path_provider.get_lib_path(), "include", *include.split("/"))
        if not os.path.exists(include_path):
            print(f"Warning: ESP32 include folder does not exist: {include_path}")
        include_paths.append(include_path)

    # Include additional paths
    for include_path in path_provider.get_additional_include_paths():
        if include_path in include_paths:
            raise ValueError(f"Additional include path {include_path} already specified")
        if not os.path.exists(include_path):
            raise ValueError(f"Additional include path does not exist: {include_path}")
    include_paths.extend(path_provider.get_additional_include_paths())

    # Add defines
    defines = arduino_cfg["defines"]
    with open(os.path.join(path_provider.get_flags_path(), "defines"), "r") as f:
        lib_defines = f.read().split(" ")
    for define in lib_defines:
        if not define:
            continue
        if not define.startswith("-D"):
            raise NotImplementedError(f"Define not starting with -D not yet supported: {define}")
        define = define.replace("\\\"", "\"")
        if define in defines:
            raise ValueError(f"Lib define {define} already specified")
        defines.append(define[2:])

    # Update the file with our revisions
    with open("./.vscode/c_cpp_properties.json", "w") as f:
        json.dump(cpp_props, f, indent=4)


write_cpp_properties(PathProvider(arduino_path=f"C:\\Users\\{os.getlogin()}\\AppData\\Local\\Arduino15"))
