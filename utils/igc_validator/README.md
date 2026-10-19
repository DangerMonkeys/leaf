# Leaf IGC validator

Tiny C checker for XLV files using IgcLogger v0.0.6's MD5 G record.
No Python runtime. This verifies a checksum, not recorder authenticity or
CIVL/XContest approval.

## macOS

With Apple's command-line developer tools installed, from the repository root:

```sh
make -C utils/igc_validator
utils/igc_validator/dist/vali-xlv sim/sdcard/tracks/flight.igc
```

Uses system CommonCrypto. Linux native builds use the same commands but require
a C compiler, make and OpenSSL development headers/libcrypto.

## Docker: build Linux and Windows executables

From the repository root on macOS/Linux:

```sh
docker build -t leaf-igc-validator utils/igc_validator
docker run --rm -v "$PWD/utils/igc_validator:/src" leaf-igc-validator all windows
```

Windows PowerShell, with Docker Desktop using Linux containers:

```powershell
docker build -t leaf-igc-validator utils/igc_validator
docker run --rm -v "${PWD}/utils/igc_validator:/src" leaf-igc-validator all windows
```

Outputs in `utils/igc_validator/dist/`:

- `vali-xlv`: Linux executable for the container architecture.
- `vali-xlv.exe`: 32-bit Windows executable (also runs on x64 Windows), using
  built-in Windows CryptoAPI without additional DLLs or runtimes.

Run `make -C utils/igc_validator clean` before switching between native macOS
and Docker builds, as both use the same Unix output filename. Docker produces
Linux and Windows binaries; build macOS natively.

On Windows:

```powershell
utils/igc_validator/dist/vali-xlv.exe sim/sdcard/tracks/flight.igc
```

Run the Linux executable through Docker, from the repository root:

```sh
docker run --rm -v "$PWD:/leaf:ro" --entrypoint /leaf/utils/igc_validator/dist/vali-xlv leaf-igc-validator /leaf/sim/sdcard/tracks/flight.igc
```

## Behavior

Reads one file without modifying it. Prints `STATUS_IGC_PASSED` (exit 0),
`STATUS_IGC_FAILED` (exit 1), or `STATUS_IGC_ERROR` (exit 2). Details go to stderr.

Requires an AXLV header, at least one B record, and a final G record containing
32 uppercase hexadecimal MD5 digits. Hashes every preceding ASCII record plus
one LF byte, matching the logger. Accepts LF/CRLF; rejects duplicate A records,
records after G, non-printable bytes, and records longer than 4096 bytes.
Does not perform full IGC syntax or flight plausibility validation. Unsigned
logs fail; an MD5 checksum can be recomputed after editing a file.
