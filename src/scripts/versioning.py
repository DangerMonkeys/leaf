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


def get_versions() -> tuple[str, str]:
  git_info = get_git_info()
  if "-" in git_info["git_tag"]:
    parts = git_info["git_tag"].split("-")
    fw_version = parts[0] + "-" + git_info["commit_hash"][0:4]
    if parts[-1] == "dirty":
      fw_version + "_dirty"
  else:
    fw_version = git_info["git_tag"]

  hw_version = env.GetProjectOption("custom_hardware_version")

  firmware_version = fw_version[1:] + "+h" + hw_version
  tag_version = fw_version[1:].split("-")[0]
  return firmware_version, tag_version


def update_build_flags(env, firmware_version, tag_version) -> None:
    defines = [
        ["FIRMWARE_VERSION", f'"\\"{firmware_version}\\""'],
        ["TAG_VERSION", f'"\\"{tag_version}\\""'],
    ]

    print("Adding defines:", defines)

    env.Append(CPPDEFINES=defines)


def make_leaf_version(env, tag_version) -> None:
    version_path = os.path.join(env.subst("$BUILD_DIR"), "leaf.version")
    with open(version_path, "w") as f:
        f.write(tag_version)


Import("env")
firmware_version, tag_version = get_versions()
update_build_flags(env, firmware_version, tag_version)
make_leaf_version(env, tag_version)
