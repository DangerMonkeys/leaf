#pragma once

#include <tusb_config.h>

#if !CFG_TUD_CDC || !CFG_TUD_MSC || CFG_TUD_MSC_BUFSIZE != 4096
#error Leaf USB requires CDC and MSC with a 4096-byte mass-storage buffer.
#endif

// Restrict the device dispatcher to the interfaces registered by Leaf.
#undef CFG_TUD_NCM
#undef CFG_TUD_DFU
#undef CFG_TUD_DFU_RUNTIME
#undef CFG_TUD_HID
#undef CFG_TUD_MIDI
#undef CFG_TUD_VIDEO
#undef CFG_TUD_VENDOR

#define CFG_TUD_NCM 0
#define CFG_TUD_DFU 0
#define CFG_TUD_DFU_RUNTIME 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VIDEO 0
#define CFG_TUD_VENDOR 0
