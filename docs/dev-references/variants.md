---
title: Variants
description: Making the Leaf software run on multiple hardware platforms
---

As the Leaf development continues, we have a bunch of different hardware revisions. Sometimes displays change,
IO pins change, even hardware is added. We also have the desire to have our software be ported to other
hardware platforms. We do this by virtue of "Variants", as modelled from Meshtastic.

To create a "variant", add one like the following. The "configuration.h" will attempt to load up this
variant.h in the variant directory if the variant flag is set, and we can put variant specific defines here.

```
[env:leaf_3_2_2_dev]
extends = dev
build_flags =
    ${env:dev.build_flags}
    -Isrc/variants/leaf_3_2_2
    -D VARIANT
```

## Configuration Flags Available:

Please see (https://github.com/DangerMonkeys/leaf/blob/main/src/vario/configuration.h)[src/vario/configuration.h"] for all available configurations.
