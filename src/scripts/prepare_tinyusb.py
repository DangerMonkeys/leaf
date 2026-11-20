"""Stage the pinned TinyUSB device dispatcher for the Leaf firmware build."""

import hashlib
from pathlib import Path
from tempfile import NamedTemporaryFile
from urllib.request import urlopen

from SCons.Script import Import

Import("env")

TINYUSB_REVISION = "7c7b30f0ae66a856ada3526cb947bd3a255567f8"
DISPATCHER_SHA256 = "f15a6da11eca127b792bd04d3270e6ea3901b0a29c24f462d5687d63da1df6cb"
SOURCE_URL = f"https://raw.githubusercontent.com/hathach/tinyusb/{TINYUSB_REVISION}/src/device/usbd.c"


def prepare_dispatcher():
    sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
    versions = (sdk / "versions.txt").read_text(encoding="utf-8").splitlines()
    sdk_revisions = [line.split()[-1] for line in versions if line.startswith("tinyusb: ")]
    if sdk_revisions != [TINYUSB_REVISION[:9]]:
        raise RuntimeError("The Leaf USB dispatcher must match the Arduino SDK's TinyUSB revision.")

    # Keep the verified source outside the compiled tree and share it across environments.
    cache = Path(env.subst("$PROJECT_BUILD_DIR")) / "tinyusb" / TINYUSB_REVISION
    source = cache / "leaf_tinyusb_usbd.inc"
    if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != DISPATCHER_SHA256:
        with urlopen(SOURCE_URL, timeout=30) as response:
            content = response.read(64 * 1024)
        if hashlib.sha256(content).hexdigest() != DISPATCHER_SHA256:
            raise RuntimeError("The pinned TinyUSB dispatcher failed SHA-256 verification.")
        cache.mkdir(parents=True, exist_ok=True)
        # Replacing a complete file lets separate build environments safely share the cache.
        with NamedTemporaryFile(dir=cache, delete=False) as temporary:
            path = Path(temporary.name)
            temporary.write(content)
        try:
            path.replace(source)
        finally:
            path.unlink(missing_ok=True)

    env.AppendUnique(CPPPATH=[str(cache)])


prepare_dispatcher()
