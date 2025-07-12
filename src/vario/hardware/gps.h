#pragma once

#include "hardware/sleepable.h"
#include "hardware/textline_source.h"

class IGPS : public ITextLineSource, public ISleepable {};
