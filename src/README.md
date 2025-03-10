# leaf source code

This folder contains the source code for the leaf vario including [libraries](libraries) and [core source code](vario).  It requires Arduino and the ESP 32 Board Manager (see [documentation](vario/README.md)).

## PlatformIO

To use [PlatformIO](https://platformio.org/) to develop this project:

### Setup

* Download [Visual Studio Code](https://code.visualstudio.com/)
* In the Extensions tab, search for PlatformIO and install the extension (make sure to restart VS Code when requested)

### Development

* Open the [root repository folder](..) (File -> Open Folder...
* Change build environment as desired
    * Click second-from-right icon in PlatformIO toolbar (small bar at bottom of IDE) and select "release" or "dev" environment
* Build and upload firmware
    * If needed (if upload fails and/or if running a release version with mass storage enabled), put in bootloader mode:
        * Unplug device (leaf)
        * Make sure device is turned off (hold center button to turn off)
        * Hold down boot button (smallest speaker hole)
        * Plug in device to USB
            * Alternately, press and release reset button (largest speaker hole)
        * Release boot button
    * Click the small rightward arrow in the PlatformIO toolbar to build and upload firmware

### Versioning

The many types of versions used by leaf are documented here.

#### Tag version

Releases on this repository are assigned [semantic versions](https://semver.org) which point to a specific commit in the repository, and the tag for the release is named according to that version, prefixed with a "v" (e.g., `v0.0.9`).  That semantic version is a "tag version".  Since a release contains/produces firmware for multiple hardware variants, a given tag version can be associated with multiple firmware versions.

#### Hardware version

All leaf-compatible hardware designs are assigned [semantic versions](https://semver.org); e.g., 2.3.5.

#### Hardware type

Currently, firmware is only built for native leaf hardware and the hardware type for all leaf hardware versions is "leaf".  If support were ever added for hardware other than leaf hardware, that hardware would have a different hardware type.  The hardware type should be a lowercase alphanumeric identifier starting with an alphabet character.

#### Hardware variant

A hardware variant refers to a specific type of hardware.  It is formed by replacing the periods in the hardware version with underscores, and then concatenating the hardware type and underscored hardware version with an underscore.  So, for instance leaf hardware version 2.3.5 would be the hardware variant `leaf_2_3_5`.  Each hardware variant customizes the build as described in [variants](./variants).

#### Behavior variant

There are multiple different sets of desired firmware behavior:

* `release`: Standard production firmware that should be used by most consumers.
* `dev`: Debug firmware that may be useful to developers.

Only `release` firmware is published as part of a release.

Behavior variant names should be very short alphanumeric names starting with an alphabet character.  No underscores are allowed.

#### Firmware version

A firmware version is a [semantic version](https://semver.org) describing a particular firmware binary.  It is composed of the tag version of the most recent release tag in the history of the branch from which the firmware was built, plus a prerelease label when appropriate, plus build metadata.

When firmware is built from a commit tagged as a release, no prerelease label is added (e.g., `0.0.9+h2.3.5`).  When firmware is built from a clean workspace at a commit later than a tagged release, a prerelease label of the first three characters of the commit hash is added (e.g., `0.0.9-fa8+h2.3.5`).  If the workspace is dirty (there are uncommitted changes that would affect the repository), a "d" is appended to the prerelease label (e.g., `0.0.9-fa8d+h2.3.5` for local changes on top of commit faa8 after/on the `0.0.9` tag version).  If the behavior variant is something other than `release`, then the behavior variant is added as a prerelease identifier (e.g., `0.0.9-dev+h2.3.5` for a dev variant of the `0.0.9` tag version, `0.0.9-fa8d.dev+h2.3.5` for local changes to a dev variant of commit faa8 after tag version `0.0.9`).

The build metadata is the hardware version prefixed with "h" (e.g., `h2.3.5` for leaf hardware version 2.3.5).  The build metadata is appended to the firmware version with a plus, per semantic versioning.  Note that hardware type is not encoded in firmware version.
