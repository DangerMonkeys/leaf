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
