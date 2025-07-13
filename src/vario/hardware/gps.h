#pragma once

#include "hardware/power_control.h"
#include "hardware/textline_source.h"

// Every GPS module should produce lines of text (hopefully constituting valid NMEA sentences) and
// respond to power control.
class IGPS : public ITextLineSource, public IPowerControl {};
