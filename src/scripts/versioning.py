from datetime import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time


exit_events = []
"""List of functions accepting a bool indicating whether the exit was successful; each will be called upon exit."""


def get_git_info() -> dict:
    # Change the working directory to the project's directory
    os.chdir(env['PROJECT_DIR'])

    # Commands to extract various information
    commands = {
        'git_tag': "git describe --tags --always --dirty --match=\"v[0-9]*\"",
        'commit_hash': "git rev-parse HEAD",
    }

    info = {}
    for key, command in commands.items():
        try:
            result = subprocess.check_output(command, shell=True).decode().strip()
            info[key] = result
        except subprocess.CalledProcessError:
            # Handling command execution errors
            info[key] = "unknown"

    return info


def get_versions(env) -> tuple[str, str, str]:
  """Determine versions of interest for the current build.

  See ../README.md#versioning

  Returns:
      * Firmware version
      * Tag version
      * Hardware variant
  """

  git_info = get_git_info()
  print("[versioning.py] git_info: " + json.dumps(git_info, indent=2))
  if "-" in git_info["git_tag"]:
    parts = git_info["git_tag"].split("-")
    tag_version = parts[0][1:]
    prerelease = git_info["commit_hash"][0:3]
    if parts[-1] == "dirty":
      prerelease += "d"
  else:
    tag_version = git_info["git_tag"][1:]
    prerelease = None

  env_name = env["PIOENV"]
  behavior_variant = env_name.split("_")[-1]
  if behavior_variant != "release":
    prerelease = (prerelease + "." + behavior_variant) if prerelease else behavior_variant

  hw_version = ".".join(env_name.split("_")[-4:-1])
  build_metadata = "h" + hw_version

  firmware_version = tag_version
  if prerelease:
    firmware_version += "-" + prerelease
  if build_metadata:
    firmware_version += "+" + build_metadata

  hardware_variant = "_".join(env["PIOENV"].split("_")[0:-1])

  return firmware_version, tag_version, hardware_variant


def set_defines(lines, values) -> tuple[set, set]:
    """Replace the #define values of the constants named according to `values`'s keys with `values`'s values in `lines`"""

    missing = set(values.keys())
    updated = set()
    pattern = re.compile(r"^#define ([A-Z_0-9]+) ")
    for i, line in enumerate(lines):
        m = pattern.match(line)
        if m and m.group(1) in missing:
            constant = m.group(1)
            missing.remove(constant)
            correct_line = f"#define {constant} \"{values[constant]}\"\n"
            if line != correct_line:
                lines[i] = correct_line
                updated.add(constant)
    return missing, updated


def update_version_header(env, firmware_version, tag_version, hardware_variant) -> None:
    """Temporarily modify version.h to hold constants that apply to this build.
    
    version.h's last-modified date is manipulated so PlatformIO will rebuild appropriately when
    the content in version.h and the constants have changed or not changed since last build.

    After the build is complete, the content of version.h as built is stored in version.h.lastbuild
    for inspection and for checking whether artifacts depending on version.h should be rebuilt next
    time.

    The original content of version.h is backed up in version.h.original, and should be restored at
    the end of the build.
    """

    # These are the #defines in version.h that should be set per build
    values = {
        "FIRMWARE_VERSION": firmware_version,
        "TAG_VERSION": tag_version,
        "HARDWARE_VARIANT": hardware_variant,
    }

    # Construct what version.h should be for this build
    vario_path = os.path.join(env["PROJECT_DIR"], "src", "vario")
    version_h_path = os.path.join(vario_path, "version.h")
    with open(version_h_path, "r") as f:
        version_lines = f.readlines()
    missing, _ = set_defines(version_lines, values)
    if missing:
        raise ValueError("version.h does not have a line #defining " + ", ".join(missing))
    
    # Check if there are changes to what version.h should be for this build as compared to what it
    # was last build
    last_version_h_path = os.path.join(vario_path, "version.h.lastbuild")
    if os.path.exists(last_version_h_path):
        with open(last_version_h_path, "r") as f:
            last_version_lines = f.readlines()
        current = "\n".join(version_lines).strip() == "\n".join(last_version_lines).strip()
    else:
        current = False
    
    # Determine whether to mark version.h as unchanged for the build
    if current:
        timestamp = os.path.getmtime(version_h_path)
        print(f"[versioning.py] The constants {', '.join(values)} have not changed since last build; version.h's modified time will be set to {datetime.fromtimestamp(timestamp)} for the build")
    else:
        timestamp = None
        print(f"[versioning.py] version.h or the constants {', '.join(values)} have changed since last build")

    # Back up current version.h content
    original_version_h_path = os.path.join(vario_path, "version.h.original")
    shutil.copy2(version_h_path, original_version_h_path)

    # Define action to restore version.h to its original state when needed
    restored = False
    def restore_version_h():
        nonlocal restored
        if not restored:
            print("[versioning.py] Restoring version.h to its original content")
            shutil.copy2(original_version_h_path, version_h_path)
            os.remove(original_version_h_path)

            if timestamp:
                # Restore version.h's last-modified timestamp
                print(f"[versioning.py] Setting date modified to {datetime.fromtimestamp(timestamp)}")
                current_time = time.time()
                os.utime(version_h_path, (current_time, timestamp))
            restored = True
        else:
            print("[versioning.py] Already restored version.h to its original content")

    # Update version.h for this build
    with open(version_h_path, "w") as f:
        f.writelines(version_lines)
    if timestamp:
        current_time = time.time()
        os.utime(version_h_path, (current_time, timestamp))

    # Queue tasks for build completion
    restored = False
    def after_build(*args, **kwargs):
        # Record version.h content used for this build
        shutil.move(version_h_path, last_version_h_path)
        current_time = time.time()
        os.utime(last_version_h_path, (current_time, current_time))
        
        restore_version_h()
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)

    # Make sure to restore version.h to its original content even if there was a build error
    global exit_events
    exit_events.append(lambda success: restore_version_h())


def make_version_files(env, tag_version) -> None:
    """Create files defining the latest tag version for each hardware variant.
    
    This includes latest_versions.json, and leaf.version for backwards compatibility.
    """

    variant_tag_versions = {}
    variants_folder = os.path.join(env["PROJECT_DIR"], "src", "variants")

    # Set tag versions of retired variants
    retired_variants_path = os.path.join(variants_folder, "retired_variants.txt")
    with open(retired_variants_path, "r") as f:
        lines = f.readlines()
    for line in lines:
        cols = [s.strip() for s in line.split("=")]
        if len(cols) != 2:
            continue  # Skip lines that don't take the form `variant=tag version`
        variant_tag_versions[cols[0]] = cols[1]

    # Set tag versions of current variants to the most recent tag version
    for item in os.listdir(variants_folder):
        item_path = os.path.join(variants_folder, item)
        if os.path.isdir(item_path):
            # Every subfolder of src/variants is considered a hardware variant
            # If the folder exists, presumably the variant is still supported, so
            # the current tag version is the most recent firmware.
            variant_tag_versions[item] = tag_version
    
    # Write collection of latest versions
    latest_versions_path = os.path.join(env.subst("$BUILD_DIR"), "latest_versions.json")
    with open(latest_versions_path, "w") as f:
        json.dump({"latest_tag_versions": variant_tag_versions}, f, indent=2)

    # Write legacy leaf.version file for backwards compatibility
    version_path = os.path.join(env.subst("$BUILD_DIR"), "leaf.version")
    with open(version_path, "w") as f:
        f.write(tag_version)


Import("env")
firmware_version, tag_version, hardware_variant = get_versions(env)
update_version_header(env, firmware_version, tag_version, hardware_variant)
make_version_files(env, tag_version)


# Trigger events at the end of the build
original_exit = sys.exit

def custom_exit(code=0):
    success = code == 0  # Non-zero exit code means a build error occurred
    for exit_event in exit_events:
        exit_event(success)
    original_exit(code)

sys.exit = custom_exit
