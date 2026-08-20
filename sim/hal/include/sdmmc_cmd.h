#pragma once

// Minimal stand-in for the ESP-IDF SD/MMC driver header. storage/sd_card.h holds one of these as
// a member so its layout has to be a complete type, but the only file that does anything real
// with it -- storage/sd_card.cpp -- talks to a raw SDIO host that has no emulator equivalent and
// is excluded from the sim build (see FW_EXCLUDE in sim/Makefile). Nothing here needs a body.
struct sdmmc_card_t {};
