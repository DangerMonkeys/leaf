import subprocess
import os


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


def get_versions(env) -> tuple[str, str]:
  git_info = get_git_info()
  if "-" in git_info["git_tag"]:
    parts = git_info["git_tag"].split("-")
    tag_version = parts[0][1:]
    prerelease = git_info["commit_hash"][0:4]
    if parts[-1] == "dirty":
      prerelease += "d"
  else:
    tag_version = git_info["git_tag"][1:]
    prerelease = None

  behavior_variant = env["PIOENV"].split("_")[-1]
  if behavior_variant != "release":
    prerelease = (prerelease + "." + behavior_variant) if prerelease else behavior_variant

  hw_version = env.GetProjectOption("custom_hardware_version")
  build_metadata = "h" + hw_version

  firmware_version = tag_version
  if prerelease:
    firmware_version += "-" + prerelease
  if build_metadata:
    firmware_version += "+" + build_metadata

  hardware_variant = "_".join(env["PIOENV"].split("_")[0:-1])

  return firmware_version, tag_version, hardware_variant


def update_build_flags(env, firmware_version, tag_version, hardware_variant) -> None:
    defines = [
        ["FIRMWARE_VERSION", f'\\"{firmware_version}\\"'],
        ["TAG_VERSION", f'\\"{tag_version}\\"'],
        ["HARDWARE_VARIANT", f'\\"{hardware_variant}\\"'],
    ]

    print("Adding defines:", defines)

    env.Append(CPPDEFINES=defines)


def make_leaf_version(env, tag_version) -> None:
    version_path = os.path.join(env.subst("$BUILD_DIR"), "leaf.version")
    with open(version_path, "w") as f:
        f.write(tag_version)


Import("env")
firmware_version, tag_version, hardware_variant = get_versions(env)
update_build_flags(env, firmware_version, tag_version, hardware_variant)
make_leaf_version(env, tag_version)
