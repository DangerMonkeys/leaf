import os
import shutil


def rename_firmware(*args, **kwargs):
  env_name = env["PIOENV"]
  hardware_variant, _, behavior_variant = env_name.rpartition("_")
  prefix = f"{behavior_variant}-firmware" if behavior_variant.endswith("bench") else "firmware"

  firmware_path = os.path.join(env.subst("$BUILD_DIR"), env["PROGNAME"] + ".bin")
  new_name = os.path.join(env.subst("$BUILD_DIR"), f"{prefix}-{hardware_variant}.bin")
  shutil.copy(firmware_path, new_name)
  print(f"[rename_firmware.py] Copied {firmware_path} to {new_name}")


Import("env")
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", rename_firmware)
