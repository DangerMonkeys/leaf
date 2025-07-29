#include "instruments/ambient.h"

#include "hardware/aht20.h"
#include "hardware/ambient_source.h"

AHT20 aht20;
IAmbientSource* ambient = &aht20;
