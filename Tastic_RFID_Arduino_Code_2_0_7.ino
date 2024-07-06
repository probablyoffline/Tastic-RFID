/*
 * Tastic RFID Arduino
 * Update by probablyoffline
 * Original code by Fran Brown
 * Bishop Fox - www.bishopfox.com
 * 
 * Version 2.0.7 - 01/10/2024
 *  
 * For more info, see:
 * 	https://bishopfox.com/tools/rfid-hacking
 * 
*/

//////// Libraries
// Text Only OLED libraries
#include <SSD1306Ascii.h>
#include <SSD1306AsciiAvrI2c.h>
// SD Card libraries
#include <SPI.h>
#include <SD.h>

////////
// Weigand settings
#define MAX_BITS 100                 // max number of bits 
#define WEIGAND_WAIT_TIME 3000      // time to wait for another weigand pulse.  

////////
// OLED settings
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define I2C_ADDRESS 0x3C
#define RST_PIN -1
SSD1306AsciiAvrI2c oled;

////////
// SD Card settings
File cardinfoFile;

////////
// Weigand variables
unsigned char databits[MAX_BITS];    // stores all of the data bits
volatile unsigned int bitCount = 0;
unsigned char flagDone;              // goes low when data is currently being captured
unsigned int weigand_counter;        // countdown until we assume there are no more bits

////////
// Card variables
volatile unsigned long facilityCode=0;        // decoded facility code
volatile unsigned long cardCode=0;            // decoded card code
// Breaking up card value into 2 chunks to create 10 char HEX value
volatile unsigned long bitHolder1 = 0;
volatile unsigned long bitHolder2 = 0;
volatile unsigned long cardChunk1 = 0;
volatile unsigned long cardChunk2 = 0;

////////
// SD Card variables
const uint8_t chipSelect = 10; // CS from SD to Pin 10 on Arduino

////////
// Process interrupts
// interrupt that happens when INTO goes low (0 bit)
void ISR_INT0()
{
  Serial.print("0");
  bitCount++;
  flagDone = 0;
  
  if(bitCount < 23) {
      bitHolder1 = bitHolder1 << 1;
  }
  else {
      bitHolder2 = bitHolder2 << 1;
  }
    
  weigand_counter = WEIGAND_WAIT_TIME;  
  
}
// interrupt that happens when INT1 goes low (1 bit)
void ISR_INT1()
{
  Serial.print("1");
  databits[bitCount] = 1;
  bitCount++;
  flagDone = 0;
  
   if(bitCount < 23) {
      bitHolder1 = bitHolder1 << 1;
      bitHolder1 |= 1;
   }
   else {
     bitHolder2 = bitHolder2 << 1;
     bitHolder2 |= 1;
   }
  
  weigand_counter = WEIGAND_WAIT_TIME;  
}

//////// SETUP function
void setup()
{
  Serial.begin(9600);

  Serial.println("[*] Begin void setup");
  Serial.println("[*] OLED startup...");

  //// OLED start
  #if RST_PIN >= 0
  oled.begin(&Adafruit128x64, I2C_ADDRESS, RST_PIN);
  #else // RST_PIN >= 0
  oled.begin(&Adafruit128x64, I2C_ADDRESS);
  #endif // RST_PIN >= 0
  // Call oled.setI2cClock(frequency) to change from the default frequency.
  oled.setFont(System5x7);
  oled.clear();
  oled.println("[*] OLED ready");
  Serial.println("[*] OLED ready");

  //// Initialize SD card
  Serial.println("[*] Initializing SD card...");
  if (!SD.begin(chipSelect)) {
    Serial.println("[!] Initialization failed!");
    oled.println("[!] SD failed");
    Serial.println("[!] SD failed");
    return;
  }
  Serial.println("[*] Initialization done");
  oled.println("[*] SD ready");
  Serial.println("[*] SD ready");


  pinMode(2, INPUT);     // DATA0 (INT0)
  pinMode(3, INPUT);     // DATA1 (INT1)
  
  // binds the ISR functions to the falling edge of INTO and INT1
  attachInterrupt(0, ISR_INT0, FALLING);  
  attachInterrupt(1, ISR_INT1, FALLING);

  weigand_counter = WEIGAND_WAIT_TIME;
  //// Ready Status
  oled.println("Waiting for card...");
  Serial.println("Waiting for card...");

} // END void setup()


//////// LOOP function
void loop()
{
  // This waits to make sure that there have been no more data pulses before processing data
  if (!flagDone) {

    
    if (--weigand_counter == 0)
      flagDone = 1;	
  }
  
  // if we have bits and we the weigand counter went out
  if (bitCount > 0 && flagDone) {
    unsigned char i;
    
    getCardValues();
    getCardNumAndSiteCode();
       
    printBits();
    writeSD();
     // cleanup and get ready for the next card
     bitCount = 0; facilityCode = 0; cardCode = 0;
     bitHolder1 = 0; bitHolder2 = 0;
     cardChunk1 = 0; cardChunk2 = 0;
     
     for (i=0; i<MAX_BITS; i++) 
     {
       databits[i] = 0;
     }
  }
}

///////////////////////////////////////////////////////
// PRINTBITS function
void printBits()
{
      // I really hope you can figure out what this function does
      Serial.print("\nTime: ");
      Serial.print(millis()/1000);
      Serial.println("s");
      Serial.print(bitCount);
      Serial.print(" bit card. ");
      Serial.print("FC = ");
      Serial.print(facilityCode);
      Serial.print(", CC = ");
      Serial.print(cardCode);
      Serial.print(", 44bit HEX = ");
      Serial.print(cardChunk1, HEX);
      Serial.println(cardChunk2, HEX);
      
      oled.clear();
      oled.print("Time: ");
      oled.print(millis()/1000);
      oled.println("s");
      oled.print(bitCount);
      oled.println(" bit card.");
      oled.print("Facility = ");
      oled.println(facilityCode);
      oled.print("Card = ");
      oled.println(cardCode);
      oled.print("44bitHEX = ");
      oled.print(cardChunk1, HEX);
      oled.println(cardChunk2, HEX);
      delay(1000);
}


///////////////////////////////////////////////////////
// SETUP function
void getCardNumAndSiteCode()
{
     unsigned char i;
  
    // we will decode the bits differently depending on how many bits we have
    // see www.pagemac.com/azure/data_formats.php for more info
    // also specifically: www.brivo.com/app/static_data/js/calculate.js
    switch (bitCount) {

      
    ///////////////////////////////////////
    // standard 26 bit format
    // facility code = bits 2 to 9  
    case 26:
      for (i=1; i<9; i++)
      {
         facilityCode <<=1;
         facilityCode |= databits[i];
      }
      
      // card code = bits 10 to 23
      for (i=9; i<25; i++)
      {
         cardCode <<=1;
         cardCode |= databits[i];
      }
      break;

    ///////////////////////////////////////
    // 33 bit HID Generic    
    case 33:  
      for (i=1; i<8; i++)
      {
         facilityCode <<=1;
         facilityCode |= databits[i];
      }
      
      // card code
      for (i=8; i<32; i++)
      {
         cardCode <<=1;
         cardCode |= databits[i];
      }    
      break;

    ///////////////////////////////////////
    // 34 bit HID Generic 
    case 34:  
      for (i=1; i<17; i++)
      {
         facilityCode <<=1;
         facilityCode |= databits[i];
      }
      
      // card code
      for (i=17; i<33; i++)
      {
         cardCode <<=1;
         cardCode |= databits[i];
      }    
      break;
 
    ///////////////////////////////////////
    // 35 bit HID Corporate 1000 format
    // facility code = bits 2 to 14     
    case 35:  
      for (i=2; i<14; i++)
      {
         facilityCode <<=1;
         facilityCode |= databits[i];
      }
      
      // card code = bits 15 to 34
      for (i=14; i<34; i++)
      {
         cardCode <<=1;
         cardCode |= databits[i];
      }    
      break;

    }
    return;
  
}


//////////////////////////////////////
// Function to append the card value (bitHolder1 and bitHolder2) to the necessary array then tranlate that to
// the two chunks for the card value that will be output
void getCardValues() {
  switch (bitCount) {
    case 26:
        // Example of full card value
        // |>   preamble   <| |>   Actual card value   <|
        // 000000100000000001 11 111000100000100100111000
        // |> write to chunk1 <| |>  write to chunk2   <|
        
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 2){
            bitWrite(cardChunk1, i, 1); // Write preamble 1's to the 13th and 2nd bits
          }
          else if(i > 2) {
            bitWrite(cardChunk1, i, 0); // Write preamble 0's to all other bits above 1
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 20)); // Write remaining bits to cardChunk1 from bitHolder1
          }
          if(i < 20) {
            bitWrite(cardChunk2, i + 4, bitRead(bitHolder1, i)); // Write the remaining bits of bitHolder1 to cardChunk2
          }
          if(i < 4) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i)); // Write the remaining bit of cardChunk2 with bitHolder2 bits
          }
        }
        break;

    case 27:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 3){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 3) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 19));
          }
          if(i < 19) {
            bitWrite(cardChunk2, i + 5, bitRead(bitHolder1, i));
          }
          if(i < 5) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 28:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 4){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 4) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 18));
          }
          if(i < 18) {
            bitWrite(cardChunk2, i + 6, bitRead(bitHolder1, i));
          }
          if(i < 6) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 29:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 5){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 5) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 17));
          }
          if(i < 17) {
            bitWrite(cardChunk2, i + 7, bitRead(bitHolder1, i));
          }
          if(i < 7) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 30:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 6){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 6) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 16));
          }
          if(i < 16) {
            bitWrite(cardChunk2, i + 8, bitRead(bitHolder1, i));
          }
          if(i < 8) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 31:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 7){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 7) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 15));
          }
          if(i < 15) {
            bitWrite(cardChunk2, i + 9, bitRead(bitHolder1, i));
          }
          if(i < 9) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 32:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 8){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 8) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 14));
          }
          if(i < 14) {
            bitWrite(cardChunk2, i + 10, bitRead(bitHolder1, i));
          }
          if(i < 10) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 33:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 9){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 9) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 13));
          }
          if(i < 13) {
            bitWrite(cardChunk2, i + 11, bitRead(bitHolder1, i));
          }
          if(i < 11) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 34:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 10){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 10) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 12));
          }
          if(i < 12) {
            bitWrite(cardChunk2, i + 12, bitRead(bitHolder1, i));
          }
          if(i < 12) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 35:        
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 11){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 11) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 11));
          }
          if(i < 11) {
            bitWrite(cardChunk2, i + 13, bitRead(bitHolder1, i));
          }
          if(i < 13) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 36:
       for(int i = 19; i >= 0; i--) {
          if(i == 13 || i == 12){
            bitWrite(cardChunk1, i, 1);
          }
          else if(i > 12) {
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 10));
          }
          if(i < 10) {
            bitWrite(cardChunk2, i + 14, bitRead(bitHolder1, i));
          }
          if(i < 14) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;

    case 37:
       for(int i = 19; i >= 0; i--) {
          if(i == 13){
            bitWrite(cardChunk1, i, 0);
          }
          else {
            bitWrite(cardChunk1, i, bitRead(bitHolder1, i + 9));
          }
          if(i < 9) {
            bitWrite(cardChunk2, i + 15, bitRead(bitHolder1, i));
          }
          if(i < 15) {
            bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
          }
        }
        break;
  }
  return;
}


void writeSD() {
  Serial.println("[*] Start of writeSD function");
  Serial.println("[*] Opening data file");
  cardinfoFile = SD.open("CARDS.txt", FILE_WRITE);
  if (cardinfoFile) {
    Serial.println("[*] File successfully opened");
  } else {
    // if the file didn't open, print an error:
    Serial.println("[!] Error opening file");
  }

  if (cardinfoFile) {
    cardinfoFile.print(bitCount);
    cardinfoFile.print(" bit card- ");
    cardinfoFile.print(cardChunk1, HEX);
    cardinfoFile.print(cardChunk2, HEX);
    cardinfoFile.print(", FC = ");
    cardinfoFile.print(facilityCode, DEC);
    cardinfoFile.print(", CC = ");
    cardinfoFile.print(cardCode, DEC);
    cardinfoFile.print(", BIN- ");
    for (int i = 19; i >= 0; i--) {
      cardinfoFile.print(bitRead(cardChunk1, i));
    }
    for (int i = 23; i >= 0; i--) {
      cardinfoFile.print(bitRead(cardChunk2, i));
    }
    cardinfoFile.println();  // To add a newline at the end of the data
    Serial.println("[+] Wrote data to SD card");
  } 
  else {
    Serial.println("[!] Error opening CARDS.txt");
  }
  cardinfoFile.close();
  Serial.println("[*] End of writeSD function"); 
}

//////// END
