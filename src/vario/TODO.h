/*


ESP32 Main things to figure out before committing to PCB design and pinouts etc

Pin/Function-Specific:
  HSPI - other spi bus for more than 3 devices (also better to use for devices that don't send stuff back, like LCD)
  Interrupt pins available for button lines (or just poll)
  PWM for speaker output
  ADC battery sense



Firmware only
  Power Saving / Sleep functions
  Wifi & Bluetooth
  Partition and Non volatile storage (for fonts, user settings, etc)




done: UART
done: VSPI 


Baro sensor 
   done, untested: better temperature adjustment accounting for <20C conditions and <-15C conditions

  



*/