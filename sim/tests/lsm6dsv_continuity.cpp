// Exercise the real host driver and ST register decoder with deterministic register I/O.
#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include "dispatch/message_sink.h"
#include "math/flat_acceleration_bias.h"
#include "sim/clock.h"
// Access commissioning phases for the register model, without a production test API.
#define private public
#include "hardware/lsm6dsv.h"
#undef private

namespace {
  uint8_t registers[4][256]{};
  std::deque<std::array<uint8_t, 7>> fifo;
  uint8_t fifoFlags = 0;
  bool readFault = false, discardFault = false;
  unsigned resets = 0, discards = 0, wordsRead = 0;
  LSM6DSV& sensor = LSM6DSV::getInstance();
  struct Receiver : MessageSink<Receiver, MotionUpdate> {
    unsigned published = 0, invalidated = 0;
    MotionUpdate last{0};
    void on_receive(const MotionUpdate& m) {
      if (m.reset)
        ++invalidated;
      else {
        ++published;
        last = m;
      }
    }
    void on_receive_unknown(const etl::imessage&) {}
  } receiver;
  void advance(uint32_t ms) { sim::clock().advanceUs(uint64_t(ms) * 1000); }
  void word(uint8_t tag, uint8_t counter, int16_t z) {
    std::array<uint8_t, 7> data{};
    data[0] = uint8_t((tag << 3) | ((counter & 3) << 1));
    data[5] = uint8_t(z);
    data[6] = uint16_t(z) >> 8;
    fifo.push_back(data);
  }
  void pairs(unsigned count, int16_t accel = 4098, int16_t gravity = 16393) {
    for (unsigned i = 0; i < count; ++i) {
      word(LSM6DSV_XL_NC_TAG, i, accel);
      word(LSM6DSV_SFLP_GRAVITY_VECTOR_TAG, i, gravity);
    }
  }
}  // namespace

// Override ST's weak register access functions. Production driver and decoding stay intact.
extern "C" int32_t lsm6dsv_read_reg(const stmdev_ctx_t*, uint8_t reg, uint8_t* data, uint16_t len) {
  const unsigned bank = registers[0][LSM6DSV_FUNC_CFG_ACCESS] >> 6;
  if (reg == LSM6DSV_FUNC_CFG_ACCESS) {
    data[0] = registers[0][reg];
  } else if (bank == 2 && reg == LSM6DSV_EMB_FUNC_EXEC_STATUS) {
    data[0] = 1;  // Embedded operations complete.
  } else if (bank == 0 && reg == LSM6DSV_FIFO_STATUS1) {
    if (readFault) return -1;
    data[0] = fifo.size() & 255;
    data[1] = uint8_t((fifo.size() >> 8) | fifoFlags);
  } else if (bank == 0 && reg == LSM6DSV_FIFO_DATA_OUT_TAG) {
    assert(len == 7 && !fifo.empty());
    memcpy(data, fifo.front().data(), 7);
    fifo.pop_front();
    ++wordsRead;
  } else if (bank == 0 && reg == LSM6DSV_WHO_AM_I) {
    data[0] = LSM6DSV_ID;
  } else if (bank == 0 && reg == LSM6DSV_STATUS_REG) {
    data[0] = 3;  // Acceleration and gyro ready.
  } else if (bank == 0 && (reg == LSM6DSV_OUTX_L_A || reg == LSM6DSV_OUTX_L_G)) {
    int16_t raw[3]{};
    if (reg == LSM6DSV_OUTX_L_A) {
      raw[2] = 8197;  // 1 g at the commissioning 4 g range.
      if (sensor.testPhase_ == 1)
        for (auto& v : raw) v -= 4098;
    } else {
      for (auto& v : raw) v = sensor.testPhase_ == 3 ? 4286 : 15;
    }
    memcpy(data, raw, len);
  } else {
    for (uint16_t i = 0; i < len; ++i) data[i] = registers[bank][uint8_t(reg + i)];
  }
  return 0;
}
extern "C" int32_t lsm6dsv_write_reg(const stmdev_ctx_t*, uint8_t reg, uint8_t* data,
                                     uint16_t len) {
  const unsigned bank = registers[0][LSM6DSV_FUNC_CFG_ACCESS] >> 6;
  if (bank == 0 && reg == LSM6DSV_FIFO_CTRL4 && (data[0] & 7) == LSM6DSV_BYPASS_MODE) {
    if (discardFault) return -1;
    fifo.clear();
    fifoFlags = 0;
    ++discards;
  }
  if (reg == LSM6DSV_FUNC_CFG_ACCESS) {
    if (data[0] & 4) {
      ++resets;
      memset(registers, 0, sizeof(registers));
      fifo.clear();
    }
    registers[0][reg] = data[0] & ~4;
  } else {
    for (uint16_t i = 0; i < len; ++i) registers[bank][uint8_t(reg + i)] = data[i];
  }
  return 0;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  sim::clock().setSpeed(0);
  std::filesystem::create_directories("sim/build-tests/lsm6dsv-continuity/state");
  Preferences::simSetStatePath("sim/build-tests/lsm6dsv-continuity/state");
  etl::message_bus<1> bus;
  bus.subscribe(receiver);
  sensor.publishTo(&bus);
  sensor.init();
  assert(sensor.running_);
  advance(3000);
  pairs(3);
  sensor.update();
  assert(receiver.published == 1);
  const unsigned faults = sensor.fifoResetCount(), invalidations = receiver.invalidated;
  for (unsigned gap : {200, 250, 500}) {
    advance(gap);
    pairs(gap * 60 / 1000);  // 24, 30, 60 words, all above the former limit.
    const auto before = receiver.published;
    sensor.update();
    assert(receiver.published == before + 1 && receiver.last.t == millis());
    assert(sensor.running_ && sensor.fifoResetCount() == faults);
    assert(receiver.invalidated == invalidations && fifo.empty());
  }
  // The drain bound is inclusive: 64 words are read; 66 are discarded below.
  pairs(32);
  advance(500);
  const auto readBeforeLimit = wordsRead;
  sensor.update();
  assert(wordsRead == readBeforeLimit + 64 && sensor.fifoResetCount() == faults);
  for (unsigned flags : {0, 0x20, 0x40}) {
    // Excess backlog and true FIFO full/overrun discard history without resetting SFLP.
    pairs(flags ? 1 : 33);
    fifoFlags = flags;
    const auto before = receiver.published, readBefore = wordsRead, discardBefore = discards;
    advance(600);
    sensor.update();
    assert(discards == discardBefore + 1 && wordsRead == readBefore);
    assert(receiver.published == before && receiver.invalidated == invalidations);
    assert(sensor.running_ && sensor.fifoResetCount() == faults);
    advance(50);
    pairs(3);
    sensor.update();
    assert(receiver.published == before + 1);
  }
  // Saturation remains a dropped sample, rather than a hardware fault.
  advance(50);
  pairs(3, 32767);
  sensor.update();
  assert(sensor.running_ && sensor.invalidAccelerationPacketCount() == 3);
  // Pairing failure still faults after its existing 500 ms timeout.
  advance(501);
  pairs(3, 4098, 0);
  sensor.update();
  assert(!sensor.running_ && sensor.fifoResetCount() == faults + 1);
  advance(1999);
  sensor.update();
  assert(!sensor.running_);
  advance(1);
  sensor.update();
  assert(sensor.running_);
  auto before = receiver.published;
  advance(100);
  pairs(3);
  sensor.update();
  assert(receiver.published == before);
  advance(2900);
  pairs(3);
  sensor.update();
  assert(receiver.published == before + 1);
  // Actual communication failure, including failed FIFO clearing, keeps full recovery.
  readFault = true;
  sensor.update();
  readFault = false;
  assert(!sensor.running_ && sensor.fifoResetCount() == faults + 2);
  advance(2000);
  sensor.update();
  assert(sensor.running_);
  discardFault = true;
  pairs(33);
  sensor.update();
  discardFault = false;
  assert(!sensor.running_ && sensor.fifoResetCount() == faults + 3);
  advance(2000);
  sensor.update();
  assert(sensor.running_);
  // A stuck overrun indication with timely polls must not hide failed acquisition.
  advance(3000);
  pairs(3);
  sensor.update();
  const auto beforeStuck = sensor.fifoResetCount();
  for (unsigned i = 0; i < 11; ++i) {
    advance(50);
    pairs(1);
    fifoFlags = 0x40;
    sensor.update();
  }
  assert(!sensor.running_ && sensor.fifoResetCount() == beforeStuck + 1);
  sensor.wake();
  assert(sensor.running_);
  // Run every electrical/gyro commissioning phase, then flat capture using the shared sums.
  sensor.startSelfTest();
  for (unsigned i = 0; sensor.testResult() == LSM6DSV::TestResult::Running && i < 400; ++i) {
    advance(50);
    if (sensor.testPhase_ == 5) pairs(3);
    sensor.update();
  }
  assert(sensor.testResult() == LSM6DSV::TestResult::Pass);
  Preferences prefs;
  assert(prefs.begin("imu328", true));
  FlatAccelerationBias reference;
  assert(prefs.getBytesLength("flatBias") == sizeof(reference));
  assert(prefs.getBytes("flatBias", &reference, sizeof(reference)) == sizeof(reference));
  assert(reference.valid() && std::abs(reference.residualG) < .001);
  std::printf(
      "Real LSM driver: late polls, FIFO discard, I2C faults, paired-data watchdog, warmup and "
      "commissioning passed; size=%zu\n",
      sizeof(LSM6DSV));
}
