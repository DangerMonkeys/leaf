#include <U8g2lib.h>
#include <SPI.h>
#include <Arduino.h>
#include "display_tests.h"

#define LCD_RS            8
#define LCD_RESET         9

#define SPI_SS_LCD       10
#define SPI_MOSI         11
#define SPI_CLK          12
#define SPI_MISO         13 

static const int spiClk = 1000000; 

void GLCD_init(void);
void GLCD_data(byte data);
void GLCD_inst(byte data);
void spi_writeGLCD(byte data);



  
  U8G2_ST75256_WO256X128_F_4W_HW_SPI u8g2(U8G2_R3,  /* cs=*/ SPI_SS_LCD, /* dc=*/ LCD_RS, /* reset=*/ LCD_RESET);
    // works with June Huang

  //U8G2_ST75256_JLX256128_F_4W_HW_SPI u8g2(U8G2_R3,  /* cs=*/ SPI_SS_LCD, /* dc=*/ LCD_RS, /* reset=*/ LCD_RESET);

  //U8G2_ST75256_JLX19296_F_4W_HW_SPI u8g2(U8G2_R3,  /* cs=*/ SPI_SS_LCD, /* dc=*/ LCD_RS, /* reset=*/ LCD_RESET);

  //U8G2_ST75256_JLX19296_1_4W_SW_SPI(rotation, clock, data, cs, dc [, reset]) [page buffer, size = 192 bytes]

  //U8G2_ST75256_JLX19296_1_4W_SW_SPI u8g2(U8G2_R1, /* clock=*/ SPI_CLK, /* data=*/ SPI_MOSI, /* cs=*/ SPI_SS_LCD, /* dc=*/ LCD_RS, /* reset=*/ LCD_RESET);
    // works with alice GREEN

uint8_t contrast_setting = 210;
uint8_t drawColorSetting = 1;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Starting Setup");

  // SPI Setup 
    SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, SPI_SS_LCD);
    pinMode(SPI_SS_LCD, OUTPUT);
    digitalWrite(SPI_SS_LCD, HIGH);
    // additional LCD pins  
      pinMode(LCD_RESET, OUTPUT);
      pinMode(LCD_RS, OUTPUT);

  // start u8g2 object
    //u8g2.setBusClock(1000000);
    bool result = u8g2.begin();
    Serial.print("Result of u8g2.begin() was ");
    Serial.println(result);

  // configure u8g2
    //u8g2.setContrast(163);
    //u8g2.clear();
    //u8g2.sendF("c", 0b00100010); // all pixels off
    //u8g2.sendF("ca", 0b10111100, 0b00000010);
  
  //GLCD_init();
  Serial.println("done with setup");

  u8g2.setContrast(contrast_setting);
}



void loop() {
  //u8g2.setContrast(122);  //alice display
  while(1) {
  // DRAW BOX
    Serial.println("u8g2 drawing start");
    u8g2.firstPage();
    do { 

      for (int i = 96; i > 2; i-=3) {
      u8g2.setDrawColor(drawColorSetting);
      u8g2.drawBox(0,0,i,i*2);   
      if (drawColorSetting) drawColorSetting--;
      else drawColorSetting++;
      }
      
    } while ( u8g2.nextPage() ); 

    delay(1000);
    if (drawColorSetting) drawColorSetting--;
    else drawColorSetting++;
    Serial.println("u8g2 drawing end");
    while(1) {}
  }


  /*
  // Cycle Contrast
  while (1) {

    //GLCD_inst(0b10000001); //set Contrast
    //GLCD_inst(contrast_setting++); //contrast value

    Serial.println(contrast_setting);
    u8g2.setContrast(contrast_setting++);
    if (contrast_setting < 190) contrast_setting = 190;
    delay(500);
  }

*/
  


  Serial.println("moving on to other things");



  Serial.println("GO!");
  
  while (1) {
  GLCD_inst(0b01011100);  //write data starting at 0
    Serial.println("going light");
    for (int i=0; i<2304; i++){
        GLCD_data(0b00000000);    //clear LCD
      delay(1);
    }
    Serial.println("going dark");
   for (int i=0; i<2304; i++){
        GLCD_data(0b11111111);    //clear LCD
      delay(1);
    }
    
  }

  delay(2000);

  GLCD_inst(0b01011100);  //write data starting at 0
  display_test_big(3);  
  display_test_big(4);  
  while(1) {}

  for (int i=1; i<5; i++) {
    display_test_big(i);  
    delay(2000);
  }

  GLCD_inst(0b01011100);  //write data starting at 0
    for (int i=0; i<2304; i++){
        GLCD_data(0b11111111);    //write LCD
      delay(1);
    }

  delay(2000);
    
  /*
    while(1) {
      GLCD_inst(0b01011100);  //write data starting at 0
      for (int i=0; i<3584; i++){
          GLCD_data(0b00000000);    //clear LCD
        delay(1);
      }
      GLCD_inst(0b01011100);  //write data starting at 0
      for (int i=0; i<3584; i++){
          GLCD_data(0b11111111);    //write LCD
        delay(1);
      }
    }
  */
}
/*
  delay(500);
  contrast_setting++;
  if (contrast_setting > 180) {
    contrast_setting = 150;
  }
  u8g2.setContrast(contrast_setting);
  // put your main code here, to run repeatedly:
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.setCursor(50,24);
    u8g2.print(contrast_setting);
  } while ( u8g2.nextPage() );  
  Serial.print(" contrast: ");
  Serial.println(contrast_setting);
}
*/


// Initialize the GRAPHIC LCD
// used for writing block data to test bmp display images ... won't save in final version
void GLCD_init(void)
{
  pinMode(LCD_RS, OUTPUT);
  pinMode(LCD_RESET, OUTPUT);
  digitalWrite(LCD_RESET, LOW); 
  delay(10);
  digitalWrite(LCD_RESET, HIGH);  

  //75256 controller
  GLCD_inst(0b00110000); //Instruction set 1
  GLCD_inst(0x94);       // wake up


  GLCD_inst(0x20);        // power control
  GLCD_data(0x0B);        // everythign on
  GLCD_inst(0x81);        // Vop 16V
  GLCD_data(0b00011000);  //110110
  GLCD_data(0b00000101);  //100

  GLCD_inst(0xCA);        // display control
  GLCD_data(0x00);        // CL divide ratio: not divide
  GLCD_data(0xA1);        // duty set: 162 duty
  GLCD_data(0x00);        // frame inverstion 

    GLCD_inst(0b01000001);   // high power mode


  GLCD_inst(0xBC);        // Data Scan Direction
  GLCD_data(0b00000001);
  //GLCD_inst(0xA6);        // Normal Display

  //GLCD_inst(0x15);        // Column address
  //GLCD_data(48);
  //GLCD_data(239);

  //GLCD_inst(0x75);        // Page address
  //GLCD_data(4);
  //GLCD_data(16);


  //GLCD_inst(0b00110001); //Instruction set 2

  //GLCD_inst(0b00100010);  // all pixels off
  GLCD_inst(0b10101111);  // display on
  

  /*

  while (1) {
    GLCD_inst(0b00100011);  // all pixels on
    delay(500);
    GLCD_inst(0b00100010);  // all pixels off
    delay(500);
    Serial.println("wuzzah");
  }

  GLCD_inst(0b11100010); //Reset
  delay(20);
  GLCD_inst(0b10101111); //Enable
  delay(20);
  //GLCD_inst(0b10000001); //set Contrast
  //GLCD_inst(0b00001111); //contrast value
  delay(20);  
  GLCD_inst(0b00000000);  //Column address LSB ->0
  delay(20);
  GLCD_inst(0b00010000);  //Column address MSB ->0
  delay(20);
  GLCD_inst(0b10110000);  //Page address ->0
  delay(20);
  GLCD_inst(0b10100110);  //Set inverse display->NO
  delay(20);

  while(1) {
    for (int i=0; i<1536; i++){
      GLCD_data(0b00000000);    //clear LCD
      delay(1);
    }
    delay(500);
      for (int i=0; i<1536; i++){
      GLCD_data(0b11111111);    //write LCD
      delay(1);
    }
  }
  */
}


void display_test_big(uint8_t test_page) {
  
//Serial.println("starting LCD stuff");
/* NOT SURE WHAT THIS IS
GLCD_inst(0b10100101);
delay(500);
GLCD_data(0b10100101);
delay(500);
GLCD_inst(0b10100100);
delay(500);
GLCD_data(0b10100100);
delay(500);
*/

/*


  //GO HOME GLCD
  GLCD_inst(0b00000000);  //Column address LSB ->0
  delay(20);
  GLCD_inst(0b00010000);  //Column address MSB ->0
  delay(20);
  GLCD_inst(0b10110000);  //Page address ->0
  delay(20);
*/


  if (test_page == 1) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_main_1[d+page*192]);  
      }
    }    
  } else if (test_page == 2) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_nav_1a[d+page*192]);  
      }
    }    
  } else if (test_page == 3) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_nav_1b[d+page*192]);  
      }
    }   
  } else if (test_page == 4) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(vario_nav_2[d+page*192]);  
      }
    }   
  } else if (test_page == 5) {
    for (int page=0; page<8; page++) {
      for (int d=0; d<192; d++) {
        GLCD_data(offroad_1[d+page*192]);  
      }
    }   
  }


  //digitalWrite(LCD_BACKLIGHT, !digitalRead(LCD_BACKLIGHT));
  //delay(500);
//BIG LCD TEST 
}


void spi_writeGLCD(byte data) {
  SPI.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_SS_LCD, LOW);
  SPI.transfer(data);     
  digitalWrite(SPI_SS_LCD, HIGH);
  SPI.endTransaction();
}


void GLCD_inst(byte data) {
  digitalWrite(LCD_RS, LOW);
  spi_writeGLCD(data);
}

void GLCD_data(byte data) {
  digitalWrite(LCD_RS, HIGH);
  spi_writeGLCD(data);
}


