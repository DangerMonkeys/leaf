import os

"""Generates build information that can't be easily supplied in platformio.ini.

Variables that must be defined in the environment: (Note that custom variable names must be prefixed custom_ or board_)
    custom_hardware_type
        Type of hardware (prefix of variant); e.g. leaf
    custom_hardware_version
        Semantic version of hardware; e.g., 3.2.2

See ../variants/README.md for expectations regarding hardware variants.
"""

def generate_build_flags(env):
    hw_type = env.GetProjectOption("custom_hardware_type")
    hw_version = env.GetProjectOption("custom_hardware_version")

    hw_variant = hw_type + "_" + hw_version.replace(".", "_")

    defines = [
        ("DEBUG_MESSAGE", hw_variant),
    ]

    # Determine the include path for the selected hardware variant
    variant_path = os.path.join(env["PROJECT_DIR"], "src", "variants", hw_variant)

    includes = [
      variant_path
    ]

    print("Adding defines:", defines)
    print("Adding include paths:", includes)

    env.Append(CPPDEFINES=defines)
    env.Append(CPPPATH=includes)


Import("env")
generate_build_flags(env)
