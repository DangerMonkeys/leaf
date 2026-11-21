"""Recompile the pinned SDK's record-size-dependent mbedTLS files for the bench."""

import hashlib
import io
from pathlib import Path
import ssl
import tarfile
from urllib.request import urlopen

import certifi
from SCons.Script import Import

Import("env")

IDF_REVISION = "083aad99cf"
MBEDTLS_REVISION = "98fcfd6d2cea90d306e8fde8e5bffd6087c9cda8"
ARCHIVE_SHA256 = "6c12e47019fd78acf2110b138874a69ad776a6202470da4674283a619f898fb4"
ARCHIVE_URL = f"https://codeload.github.com/espressif/mbedtls/tar.gz/{MBEDTLS_REVISION}"
# These are the translation units that use the record sizes, directly or via ssl_misc.h.
SOURCES = ("ssl_msg.c", "ssl_tls.c", "ssl_tls12_client.c", "ssl_tls12_server.c",
           "ssl_tls13_generic.c")


def preserve_default_receive_limit(data):
    # This revision uses min(RX, TX) for the "no MFL negotiated" code. Skip that
    # conversion on the receive side while preserving explicit MFL negotiation.
    replacements = {
        b"ssl->state >= MBEDTLS_SSL_SERVER_HELLO_DONE) {\n"
        b"        return ssl_mfl_code_to_length": (
            b"ssl->state >= MBEDTLS_SSL_SERVER_HELLO_DONE &&\n"
            b"        ssl->conf->mfl_code != MBEDTLS_SSL_MAX_FRAG_LEN_NONE) {\n"
            b"        return ssl_mfl_code_to_length"
        ),
        b"if (ssl->session_out != NULL) {\n        read_mfl": (
            b"if (ssl->session_out != NULL &&\n"
            b"        ssl->session_out->mfl_code != MBEDTLS_SSL_MAX_FRAG_LEN_NONE) {\n"
            b"        read_mfl"
        ),
        b"if (ssl->session_negotiate != NULL) {\n        read_mfl": (
            b"if (ssl->session_negotiate != NULL &&\n"
            b"        ssl->session_negotiate->mfl_code != MBEDTLS_SSL_MAX_FRAG_LEN_NONE) {\n"
            b"        read_mfl"
        ),
    }
    for old, new in replacements.items():
        if data.count(old) != 1:
            raise RuntimeError("The pinned mbedTLS receive limit patch did not match.")
        data = data.replace(old, new)
    return data


def source_directory():
    sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
    versions = (sdk / "versions.txt").read_text(encoding="utf-8").splitlines()
    idf = [line.split()[-1] for line in versions if line.startswith("esp-idf: ")]
    if idf != [IDF_REVISION]:
        raise RuntimeError("Review the TLS bench source and settings against the new ESP-IDF SDK.")

    cache = Path(env.subst("$PROJECT_BUILD_DIR")) / "tls-bench"
    archive = cache / "mbedtls.tar.gz"
    cache.mkdir(parents=True, exist_ok=True)
    if archive.is_file():
        content = archive.read_bytes()
    else:
        context = ssl.create_default_context(cafile=certifi.where())
        with urlopen(ARCHIVE_URL, context=context, timeout=30) as response:
            content = response.read(16 * 1024**2)
    if hashlib.sha256(content).hexdigest() != ARCHIVE_SHA256:
        raise RuntimeError("The pinned Espressif mbedTLS archive failed SHA-256 verification.")
    if not archive.is_file():
        archive.write_bytes(content)

    library = cache / "library"
    library.mkdir(exist_ok=True)
    prefix = f"mbedtls-{MBEDTLS_REVISION}/library/"
    with tarfile.open(fileobj=io.BytesIO(content), mode="r:gz") as sources:
        for member in sources:
            name = member.name.removeprefix(prefix)
            if member.isfile() and member.name.startswith(prefix) and "/" not in name:
                target = library / name
                data = sources.extractfile(member).read()
                if name == "ssl_tls.c":
                    data = preserve_default_receive_limit(data)
                if not target.is_file() or target.read_bytes() != data:
                    target.write_bytes(data)
    return library


library = source_directory()
config_directory = Path(env.subst("$PROJECT_DIR")) / "src" / "scripts"
env.AppendUnique(CPPPATH=[str(config_directory)],
                 CPPDEFINES=[("MBEDTLS_USER_CONFIG_FILE", r'\"tls_bench_config.h\"')])
# Link these objects ahead of the SDK archives. Other SDK TLS and crypto objects stay intact.
env.BuildSources("$BUILD_DIR/tls-records", str(library),
                 [f"+<{source}>" for source in SOURCES])
