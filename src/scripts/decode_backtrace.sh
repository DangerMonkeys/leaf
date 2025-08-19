#!/usr/bin/env bash
# Decode ESP32 backtrace addresses using the ELF from the active PlatformIO env.
# Usage:
#   1) Pipe/paste a whole backtrace:   cat backtrace.txt | ./decode_backtrace.sh
#   2) Or pass addresses directly:     ./decode_backtrace.sh 0x42010abc 0x4037fe12 ...

set -euo pipefail

# ---------- Helpers ----------
err() { printf "Error: %s\n" "$*" >&2; exit 1; }
info() { printf "[info] %s\n" "$*" >&2; }

# ---------- Require PIOENV (provided by tasks.json) ----------
PIOENV="${PIOENV:-}"
[[ -n "$PIOENV" ]] || err "PIOENV is not set. Ensure the VS Code task provides it via \${command:platformio-ide.activeEnvironment}."

# ---------- Resolve ELF ----------
BUILD_DIR=".pio/build/${PIOENV}"
[[ -d "$BUILD_DIR" ]] || err "Build directory not found: ${BUILD_DIR}"

ELF_CANDIDATES=()
if [[ -f "${BUILD_DIR}/firmware.elf" ]]; then
  ELF_CANDIDATES+=("${BUILD_DIR}/firmware.elf")
fi
# Fallback: any .elf in the env's build directory
while IFS= read -r -d '' f; do ELF_CANDIDATES+=("$f"); done < <(find "$BUILD_DIR" -maxdepth 1 -type f -name '*.elf' -print0)

[[ ${#ELF_CANDIDATES[@]} -gt 0 ]] || err "No .elf found in ${BUILD_DIR}."
ELF="${ELF_CANDIDATES[0]}"

info "Using ELF: ${ELF}"

[[ -r "$ELF" ]] || err "ELF not readable: ${ELF}"

# ---------- Locate addr2line ----------
# Preferred (ESP32-S3):
CAND_BIN_NAMES=(
  "xtensa-esp32s3-elf-addr2line"
  "xtensa-esp32-elf-addr2line"
  "riscv32-esp-elf-addr2line"
)

ADDR2LINE_BIN=""
# 1) PATH lookup
for b in "${CAND_BIN_NAMES[@]}"; do
  if command -v "$b" >/dev/null 2>&1; then
    ADDR2LINE_BIN="$(command -v "$b")"
    break
  fi
done

# 2) Search common PlatformIO tool locations if not found
if [[ -z "$ADDR2LINE_BIN" ]]; then
  PIO_HOME="${PLATFORMIO_HOME_DIR:-$HOME/.platformio}"
  if [[ -d "$PIO_HOME/packages" ]]; then
    while IFS= read -r -d '' candidate; do
      ADDR2LINE_BIN="$candidate"
      break
    done < <(find "$PIO_HOME/packages" -type f \( \
        -name 'xtensa-esp32s3-elf-addr2line' -o \
        -name 'xtensa-esp32-elf-addr2line'   -o \
        -name 'riscv32-esp-elf-addr2line'    \
      \) -print0 2>/dev/null | head -zn 1)
  fi
fi

[[ -n "$ADDR2LINE_BIN" ]] || err "Could not locate addr2line (xtensa-esp32s3-elf-addr2line / xtensa-esp32-elf-addr2line / riscv32-esp-elf-addr2line). Ensure PlatformIO toolchains are installed and on PATH."

info "Using addr2line: ${ADDR2LINE_BIN}"

# ---------- Gather addresses ----------
INPUT_ADDRESSES=""
if [[ $# -gt 0 ]]; then
  # Take addresses from CLI args
  INPUT_ADDRESSES="$(printf "%s\n" "$@")"
else
  # Read from stdin (paste backtrace or pipe a file)
  if [ -t 0 ]; then
    info "Paste the backtrace (Ctrl-D when done):"
  fi
  INPUT_ADDRESSES="$(cat)"
fi

# Extract hex addresses of form 0x...
# Keep original order while removing duplicates.
mapfile -t ADDRS < <(printf "%s" "$INPUT_ADDRESSES" \
  | grep -Eo '0x[0-9a-fA-F]+' \
  | awk '!seen[$0]++')

[[ ${#ADDRS[@]} -gt 0 ]] || err "No addresses found (expected tokens like 0x400d1234)."

info "Decoding ${#ADDRS[@]} unique addresses..."

# ---------- Decode ----------
# -p: show function names with offsets, -f: function name on its own line,
# -i: include inlined frames, -a: print address, -C: demangle
"${ADDR2LINE_BIN}" -e "${ELF}" -pfiaC "${ADDRS[@]}"
