#include <Arduino.h>
#include <assert.h>
#include <stdio.h>

#include "runtime.h"
#include "ui/settings/settings.h"

int main(int argc, char**) {
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/climb-display-average/state";
  options.cardRoot = "sim/build-tests/climb-display-average/card";
  sim::runtime().configure(options);
  sim::runtime().boot();
  if (argc > 1) {
    assert(settings.vario_climbDisplayAverage == 20);
    puts("Twenty-second climb average survived a fresh simulator boot");
    return 0;
  }
  settings.loadDefaults();
  settings.vario_climbDisplayAverage = 0;
  settings.adjustClimbDisplayAverage(Button::LEFT);
  assert(settings.vario_climbDisplayAverage == 0);
  for (int seconds = 1; seconds <= 20; ++seconds) {
    settings.adjustClimbDisplayAverage(Button::RIGHT);
    assert(settings.vario_climbDisplayAverage == seconds);
  }
  settings.adjustClimbDisplayAverage(Button::RIGHT);
  assert(settings.vario_climbDisplayAverage == 20);
  settings.save();
  settings.loadDefaults();
  settings.retrieve();
  assert(settings.vario_climbDisplayAverage == 20);
  settings.adjustClimbDisplayAverage(Button::LEFT);
  assert(settings.vario_climbDisplayAverage == 19);
  settings.adjustClimbDisplayAverage(Button::RIGHT);
  puts("Climb average range, boundaries and persistence passed");
}
