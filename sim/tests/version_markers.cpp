#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "leaf_version.h"
#include "system/version_info.h"
int main() {
  assert(!strcmp(LeafVersionInfo::firmwareVersion(), FIRMWARE_VERSION));
  assert(!strcmp(LeafVersionInfo::hardwareVariant(), HARDWARE_VARIANT));
  puts("Version accessors preserve their values with linker garbage collection enabled");
}
