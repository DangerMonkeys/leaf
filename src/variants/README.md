# Hardware variants

Leaf supports building nominally the same firmware for different hardware variants.  The specifics for each of these variants are defined in this folder.

## Creating a new variant

To create a new hardware variant, first identify the [hardware type](../README.md#hardware-type) (usually "leaf") and [hardware version](../README.md#hardware-version) (e.g., 3.2.5).  The name for this hardware variant must be: `{hardware type}_{underscored hardware version}` where `{underscored hardware version}` is the hardware version with periods replaced with underscores.  Create a folder with this name within this folder.  Add this variant name to the list of variants in [build.yaml](../../.github/workflows/build.yaml).  Change [platformio.ini](../../platformio.ini) to point `default_envs` at the appropriate environment (see below).

In the new folder, add:

### platformio.ini

This partial platformio.ini should define `[env:{hardware variant}_dev]` and `[env:{hardware variant}_release]` which should extend `env:_dev`and `env:_release` respectively.  In each of these environments, `build_flags` must extend the build flags of the base environment to add `-Isrc/variants/{hardware variant}`.

### variant.h

This file must exist, and should define any differences inherent to this hardware variant.

## Retiring a variant

When a hardware variant will no longer be supported in future firmware, its subfolder should be deleted and a line added to [retired_variants.txt](./retired_variants.txt) with `{hardware variant}={last tag version supporting this variant}`.  For example, `leaf_3_2_2=0.0.9`.
