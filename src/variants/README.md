# Hardware variants

Leaf supports building nominally the same firmware for different hardware variants.  The specifics for each of these variants are defined in this folder.

## Creating a new variant

To create a new hardware variant:

1. Identify the [hardware type](../README.md#hardware-type) (usually "leaf") and [hardware version](../README.md#hardware-version) (e.g., 3.2.5)
2. Create the name for this hardware variant as: `{hardware type}_{underscored hardware version}` where `{underscored hardware version}` is the hardware version with periods replaced with underscores.
3. Create a folder with the hardware variant's name within this (`variants`) folder.  In the new folder, add:
    1. **platformio.ini**
        1. This partial platformio.ini should define `[env:{hardware variant}_dev]` and `[env:{hardware variant}_release]` which should extend `env:_dev`and `env:_release` respectively.
        2. In each of these environments, `build_flags` must extend the build flags of the base environment to add `-Isrc/variants/{hardware variant}`.
    2. **variant.h**: This file must exist, and should define any differences inherent to this hardware variant.
4. Add this variant name to the list of variants in [build.yaml](../../.github/workflows/build.yaml).
5. If applicable, change the "Copy appropriate hardware firmware to firmware.bin" step in [build.yaml](../../.github/workflows/build.yaml) to use the new hardware variant as the backwards-compatible firmware.bin.
6. If applicable, change [the main platformio.ini](../../platformio.ini) to point `default_envs` at the appropriate environment (see 3.1 above).

## Retiring a variant

When a hardware variant will no longer be supported in future firmware, its subfolder should be deleted and a line added to [retired_variants.txt](./retired_variants.txt) with `{hardware variant}={last tag version supporting this variant}`.  For example, `leaf_3_2_2=0.0.9`.
