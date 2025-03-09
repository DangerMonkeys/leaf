# Hardware variants

Leaf supports building nominally the same firmware for different hardware variants.  The specifics for each of these variants are defined in this folder.

## Creating a new variant

To create a new hardware variant, first identify the hardware type (usually "leaf") and hardware version (e.g., 3.2.5).  The folder name for this variant should be: `{hardware type}_{underscored hardware version}` where `{underscored hardware version}` is the hardware version with periods replaced with underscores.  Create this folder within this folder.  In this folder, add:

### platformio.ini

This partial platformio.ini should define `[env:{hardware variant}_dev]` and `[env:{hardware variant}_release]` which should extend `env:_dev`and `env:_release` respectively.  In each of these environments, the variables in the documentation in src/scripts/prebuild.py must be defined (`custom_hardware_type` and `custom_hardware_version`).

### variant.h

This file must exist, and should define any differences inherent to this hardware variant.
