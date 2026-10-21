# LSM6DSV register driver

Vendored from [STMicroelectronics/lsm6dsv-pid v5.0.1](https://github.com/STMicroelectronics/lsm6dsv-pid/tree/v5.0.1).
This is the LSM6DSV driver, not the LSM6DSV16X driver. See LICENSE.

Local change: the three polling loops in `lsm6dsv_sflp_game_gbias_set` stop on an I/O error or after 100 attempts with 1 ms between attempts. The Leaf wrapper restores the main register bank, disables the sensor, and resets it before retrying after any error. This prevents an absent/faulty device from hanging boot in upstream's unbounded polling loops.

Application configuration and commissioning thresholds follow ST's
[fusion](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_sensor_fusion.c) and
[self-test](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_self_test.c) examples.
