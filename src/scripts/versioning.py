import json
import os
import re
import subprocess


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


def update_version_header(env, firmware_version, tag_version, hardware_variant) -> None:
    values = {
        "FIRMWARE_VERSION": firmware_version,
        "TAG_VERSION": tag_version,
        "HARDWARE_VARIANT": hardware_variant,
    }

    version_h_path = os.path.join(env["PROJECT_DIR"], "src", "vario", "version.h")
    with open(version_h_path, "r") as f:
        lines = f.readlines()
    
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
    
    if missing:
        raise ValueError("version.h does not have a line #defining " + ", ".join(missing))
    if updated:
        with open(version_h_path, "w") as f:
            f.writelines(lines)
        print(f"[versioning.py] The constant{'s' if len(updated) > 1 else ''} of {', '.join(updated)} {'were' if len(updated) > 1 else 'was'} updated in version.h")
    else:
        print(f"[versioning.py] The constants {', '.join(values)} are up to date in version.h")


def make_version_files(env, tag_version) -> None:
    """Create files defining the latest tag version for each hardware variant."""

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
