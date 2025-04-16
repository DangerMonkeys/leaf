# Leaf Hardware
Brief summary of hardware changes

1. [PCBA versions and changes](#PCBA)
2. [Plastic Case](#plastic-case)
3. [Battery](#battery)

## PCBA

### v3.2.6
+ Added 10pin connector for Sharp Memory-in-Pixel display
+ IMU rotated 90deg clockwise (+X is "forward" now)
+ Removed 0-ohm configuration resistors not being used
+ Reposition LoRa module and I2C connector to make hand-soldering LoRa easier 
+ Remove ground plane contact from LoRa ground pins to reduce heat-sink effect when soldering
+ Scoot LoRa and I2C connectors further 'rearward' (versus 3.2.5); this brings the battery closer to the original 3.2.4 position; though battery is still a bit forward from 3.2.4.
+ Temp/Humidity "isolation slot" is shrunk toward rear slightly -- needs plastic case modification
+ Increased size of R21 (backlight LED resistor) to allow easier hand-solder replacement if user needs a different R-value
+ Added ISET, LED, and PWR_GOOD to ESP32
++ ISET: Using ESP32 ADC, can read actual charging current from charger IC
++ LED: ESP32 can now control green power LED (previously was on only when USB plugged in)
++ PWR_GOOD allows ESP32 to measure presence of input power
++ All of the above will allow more fine control over power supply modes and reactions

+ Significant Pin Changes to accommodate the above and also make full use of IO-expander

#### ESP32 Pinout Changes
| ESP32  | 3.2.6     | Prev 3.2.5 |    Prev 3.2.4    |
|--------|-----------|------------|------------------|
|GPIO  0 |LORA_RESET | <- same    | AVAIL_GPIO_0     |
|GPIO  7 | ISET      | LORA_RF_SW | IMU_INT / GPIO_7 |
|GPIO 15 |LORA_DIO1  | <- same    | SPKR_VOL_A       |
|GPIO 16 |LORA_BUSY  | <- same    | SPKR_VOL_B       |
|GPIO 39 |IO_EX_INT  | <- same    | AVAIL_GPIO_39    |
|GPIO 46 |LORA_CS    | <- same    | GPS_1PPS         |
|GPIO 26 |LORA_RFSW  | SD_DETECT  |   <- same        | 
|GPIO 40 |GPS 1PPS   | GPS_BAK_EN |   <- same        |
|GPIO 41 |EYESPI_BUSY| PWR_CHG_i1 |   <- same        |
|GPIO 42 |EYESPI_INT | PWR_CHG_i2 |   <- same        |
|GPIO 45 |EYESPI_SDCS| GPS_RESET  |   <- same        |
|GPIO 47 | LED       | CHG_GOOD   |   <- same        |

|  IOEX  |    3.2.6   | Prev 3.2.5  |
|--------|------------|-------------|
| A0 P00 |EYESPI_MEM  | GPS_1PPS    |
| A1 P01 |EYESPI_TOUCH| SPRK_VOLB   |
| A2 P02 |EYESPI_GP1  | SPRK_VOLA   |
| A3 P03 |EYESPI_GP2  | EYESPI_SDCS |
| A4 P04 |SPKR_VOLB   | N/C         |
| A5 P05 |SPKR_VOLA   | EYESPI_GP2  |
| A6 P06 |IOEX_1      | EYESPI_GP1  |
| A7 P07 |IOEX_2      | EYESPI_BUSY |
| B0 P10 |CHG_GOOD    | EYESPI_INT  |
| B1 P11 |PWR_GOOD    | EYESPI_MEM  |
| B2 P12 |PWR_CHG_i1  | EYESPI_TOUCH|
| B3 P13 |PWR_CHG_i2  | IMU_INT     |
| B4 P14 |IMU_INT     | N/C         |
| B5 P15 |SD_DETECT   | N/C         |
| B6 P16 |GPS_RESET   | N/C         |
| B7 P17 |GPS_BAK_EN  | N/C         |


### v3.2.5

#### Changes
+ Replace bare ESP32S3 with full module (ESP32-S3-MINI-1).  Can utilize Espressif's FCC and CE certifications
+ Add optional LoRa module (Wio-SX1262 by SeeedStudio).  NOTE: with LoRa module installed, battery moves forward in position by 3mm compared to 3.2.4.  May not fit in case design v3.2.4.
+ Add IO Expander for 16 more GPIO pins
+ Adjust USB-EN voltage divider (used for signaling HI on power_enable and also GPS backup power when no battery).  Also connected USB-EN to GPS Backup Regulator, instead of GPS directly.  These changes allow for higher input voltage on USB connector (now limit is 9V vs previous 6V).  
+ Moved all header pins to one edge (out of the way of ESP module antenna)
+ Moved Charge, VCC 3.3 circuitry and battery connector to make more room for battery
+ Other minor changes


#### ESP32 Pinout Changes
| ESP32  |   3.2.5   |   Prev 3.2.4    |
|--------|-----------|-----------------|
|GPIO   0|LORA_RESET |AVAIL_GPIO_0     |
|GPIO   7|LORA_RF_SW |IMU_INT / GPIO_7 |
|GPIO  15|LORA_DIO1  |SPKR_VOL_A       |
|GPIO  16|LORA_BUSY  |SPKR_VOL_B       |
|GPIO  39|IO_EX_INT  |AVAIL_GPIO_39    |
|GPIO  46|LORA_CS    |GPS_1PPS         |

#### LoRa SX1262 Comms Module
Seeed Studio WIO-SX1262
[info & datasheet near bottom of page](https://www.seeedstudio.com/Wio-SX1262-Wireless-Module-p-5981.html)

#### IO Expander 
[datasheet](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2304251416_XINLUDA-XL9535QF24_C5444301.pdf)
Chip is configured with I2C address: 0x20

|IOEX Pin# |Function
|--------|--------|
|P00|GPS_1PPS (input)
|P01|SPKR_VOL_B           
|P02|SPKR_VOL_A     
|P03|EYESPI_SDcard_CS     
|P04|No Connection
|P05|EYESPI_GPIO_2 
|P06|EYESPI_GPIO_1  
|P07|EYESPI_BUSY (input)  
|P10|EYESPI_INT (input)
|P11|EYESPI_MEM_CS     
|P12|EYESPI_TS_CS  
|P13|IMU_INT (input)
|P14|EX_14 *Expanded GPIO
|P15|EX_15 *Expanded GPIO
|P16|EX_16 *Expanded GPIO
|P17|EX_17 *Expanded GPIO

(*EYESPI is the multipurpose AdaFruit 18-pin connector ("Display 2"))


### v3.2.4
+ Fix: insufficient current to power GPS backup supply from USB cable (when no battery attached)
+ Fix: silkscreen errors
### v3.2.3
+ Change to superior 12-pin LCD display (192x96px)
+ Add additional "eye-spi" LCD connector for any AdaFruit and other 3rd party displays
+ improved header pins along edges and added notches for backlight version of LCD
### v3.2.2
+ 14-pin 96px wide LCD display
+ Add I2C expansion connector
+ Add Temp/Humidty sensor
+ Boot/Reset switches moved underneath speaker grille
### v3.2.0
+ First version with new ESP32 S3 processor
+ 10-pin 64px wide LCD display

## Plastic Case
### v3.2.6
(These are all still TODO items)
+ Shrink Temp/Humidity isolation slot
+ Add button-protecting ribs
+ Solidify decision on PCB thickness and adjust for it
+ Move battery ribs to new PCB_v3.2.6 position
+ Make version for both with and without antenna
+ Make version for Sharp Memory-in-pixel display
+ Increase caddy snap-angle to nearly 90deg
+ Adjust baseplate caddy for adjusted battery and slot positions

### v3.2.5
+ Move battery ribs forward to allow more space in the rear for LoRa module
+ Add antenna exit and mount
+ Tweaks to certain elements to accomodate thinner PCB (1mm vs 1.6mm)
### v3.2.4
+ Increased 'flares' of main button for easier grip
+ Increased 'clip force' in baseplate
+ Baseplate can use same width velcro as Leaf
+ Stiffening webs added to battery tabs
+ Screw bosses adjusted for proper M2x8mm thread-forming screws
### v3.2.0
+ Initial case design
+ Alternative-Configuration files available to support additional display types (TFT color LCD, FSTN with backlight, etc)

## Battery
+ Standard size 103450 single cell lipo (3.7V).  10mm x 34mm x 50mm