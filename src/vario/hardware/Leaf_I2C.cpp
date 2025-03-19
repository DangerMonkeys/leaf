#include "hardware/Leaf_I2C.h"

void wire_init() {
  Wire.begin();
  Wire.setClock(400000);
}
