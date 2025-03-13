import os
import shutil


def rename_firmware(*args, **kwargs):
  env_name = env["PIOENV"]
  hardware_variant = "_".join(env_name.split("_")[0:-1])

  firmware_path = os.path.join(env.subst("$BUILD_DIR"), env["PROGNAME"] + ".bin")
  new_name = os.path.join(env.subst("$BUILD_DIR"), f"firmware-{hardware_variant}.bin")
  shutil.copy(firmware_path, new_name)
  print(f"[rename_firmware.py] Copied {firmware_path} to {new_name}")


Import("env")
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", rename_firmware)
