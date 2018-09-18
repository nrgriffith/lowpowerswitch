#include <SPI.h>
#include <Adafruit_GPS.h>
#include <SoftwareSerial.h>
#include <SD.h>
#include <avr/sleep.h>

char unit[3]="22";
char directory[7];


// Ladyada's logger modified by Bill Greiman to use the SdFat library
//
// This code shows how to listen to the GPS module in an interrupt
// which allows the program to have more 'freedom' - just parse
// when a new NMEA sentence is available! Then access data when
// desired.-3
//
// Tested and works great with the Adafruit Ultimate GPS Shield
// using MTK33x9 chipset
//    ------> http://www.adafruit.com/products/
// Pick one up today at the Adafruit electronics shop
// and help support open source hardware & software! -ada
// Fllybob added 10 sec logging option


SoftwareSerial mySerial(8, 7);
Adafruit_GPS GPS(&mySerial);

// Set GPSECHO to 'false' to turn off echoing the GPS data to the Serial console
// Set to 'true' if you want to debug and listen to the raw GPS sentences
#define GPSECHO  false
/* set to true to only log to SD when GPS has a fix, for debugging, keep it false */
#define LOG_FIXONLY false

// this keeps track of whether we're using the interrupt
// off by default!
boolean usingInterrupt = false;
void useInterrupt(boolean); // Func prototype keeps Arduino 0023 happy

// Set the pins used
#define chipSelect 10
#define ledPin 13    // yellow LED for error codes
#define OnSelect 5
#define OffSelect 6
#define set_Speed 7  //Knot speed that will turn on Pi system
#define Pi_active 4
#define SDled 7   // LED to give feedback 

int val = 0;
int i = 0;
int relayOpen = 0;
boolean sderror = true;
boolean initial = true;
uint32_t timer = millis();

File logfile;

// read a Hex value and return the decimal equivalent
uint8_t parseHex(char c) {
    if (c < '0')
         return 0;
    if (c <= '9')
         return c - '0';
    if (c < 'A')
         return 0;
    if (c <= 'F')
         return (c - 'A')+10;
}

// blink out an error code
void error(uint8_t errno) {
    uint8_t i;
    for (i=0; i<errno; i++) {
        digitalWrite(ledPin, HIGH);
        delay(100);
        digitalWrite(ledPin, LOW);
        delay(100);
    }
    for (i=errno; i<10; i++) {
        delay(200);
    }
}

void setup() {
    // for Leonardos, if you want to debug SD issues, uncomment this line
    // to see serial output
    //while (!Serial);
    directory[0] = 'u';
    directory[1] = 'n';
    directory[2] = 'i';
    directory[3] = 't';
    directory[4] = unit[0];
    directory[5] = unit[1];
    directory[6] = '\0';

    char filename[19];
    filename[0] = 'u';
    filename[1] = 'n';
    filename[2] = 'i';
    filename[3] = 't';
    filename[4] = unit[0];
    filename[5] = unit[1];
    filename[6] = '/';
    filename[7] = 'u';
    filename[8] = unit[0];
    filename[9] = unit[1];
    filename[10] = '_';
    filename[11] = '0';
    filename[12] = '0';
    filename[13] = '0';
    filename[14] = '.';
    filename[15] = 'T';
    filename[16] = 'X';
    filename[17] = 'T';
    filename[18] = '\0';

    // connect at 115200 so we can read the GPS fast enough and echo without dropping chars
    // also spit it out
    Serial.begin(115200);
    Serial.println("\r\nUltimate GPSlogger Shield");
    pinMode(ledPin, OUTPUT);
    pinMode(SDled, OUTPUT);
    pinMode(OnSelect, OUTPUT);
    pinMode(OffSelect, OUTPUT);
    pinMode(Pi_active, INPUT);

    digitalWrite(SDled, LOW);

    //make user that the system is initally off
    digitalWrite(OffSelect, HIGH);
    delay(1000); // Edited by Nichole on Nov 5, 2017, to let user know that the relay is off
    digitalWrite(OffSelect, LOW);

    // make sure that the default chip select pin is set to
    // output, even if you don't use it:
    pinMode(10, OUTPUT);

    // see if the card is present and can be initialized:
    // if (!SD.begin(chipSelect, 11, 12, 13)) {
    if (!SD.begin(chipSelect)) {      // if you're using an UNO, you can use this line instead
        Serial.println("Card init. failed!");
        Serial.println("\n\n----\n\n");
        sderror = true;
        //error(2);
    }
    else {
        sderror = false;
    }
    
    if (!sderror){
        // turn light on
        Serial.print("SD Card Found\n\n");
        digitalWrite(SDled, HIGH);
        // make file directory
        if (!SD.exists(directory)){
          
            if(SD.mkdir(directory)){
              Serial.println("Created directory: ");
              Serial.println(directory);
            }
            else {
              Serial.println("Unable to create directory: ");
              Serial.println(directory);
              Serial.println("\n\n----\n\n");
            }
        }
        // get new file name

        for (uint16_t i = 0; i < 1000; i++) {
            filename[11] = '0' + i/100;
            filename[12] = '0' + (i%100)/10;
            filename[13] = '0' + (i%100)%10;
            // create if does not exist, do not open existing, write, sync after write
            if (! SD.exists(filename))
                break;
        }
    
        logfile = SD.open(filename, FILE_WRITE);
        if( ! logfile ) {
            // print errror message if could not create file
            Serial.print("Couldnt create ");
            Serial.println(filename);
            Serial.println("\n\n----\n\n");
            error(3);
            sderror=true;
            digitalWrite(SDled, LOW);
        }
        else{
            Serial.print("Writing to ");
            Serial.println(filename);
            Serial.println("\n\n----\n\n");
        }
    }
    // connect to the GPS at the desired rate
    GPS.begin(9600);

    // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
    // uncomment this line to turn on only the "minimum recommended" data
    //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
    // For logging data, we don't suggest using anything but either RMC only or RMC+GGA
    // to keep the log files at a reasonable size
    // Set the update rate
    //GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);   // 100 millihertz (once every 10 seconds), 1Hz or 5Hz update rate
    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_200_MILLIHERTZ);   // 200 millihertz (once every 5 seconds), 1Hz or 5Hz update rate
    // Turn off updates on antenna status, if the firmware permits it
    GPS.sendCommand(PGCMD_NOANTENNA);

    // the nice thing about this code is you can have a timer0 interrupt go off
    // every 1 millisecond, and read data from the GPS for you. that makes the
    // loop code a heck of a lot easier!
    useInterrupt(true);

    Serial.println("Ready!");
}

// Interrupt is called once a millisecond, looks for any new GPS data, and stores it
SIGNAL(TIMER0_COMPA_vect) {
    char c = GPS.read();
    // if you want to debug, this is a good time to do it!
    #ifdef UDR0
        if (GPSECHO)
            if (c) UDR0 = c;
            // writing direct to UDR0 is much much faster than Serial.print
            // but only one character can be written at a time.
    #endif
}

void useInterrupt(boolean v) {
    if (v) {
        // Timer0 is already used for millis() - we'll just interrupt somewhere
        // in the middle and call the "Compare A" function above
        OCR0A = 0xAF;
        TIMSK0 |= _BV(OCIE0A);
        usingInterrupt = true;
    }
    else {
        // do not call the interrupt function COMPA anymore
        TIMSK0 &= ~_BV(OCIE0A);
        usingInterrupt = false;
    }
}

// MAIN LOOP
void loop() {
    // turn on LED for ~3 seconds to communicate that SD card is found
    if((!sderror) && (initial)) {
        //Serial.print("No SD Card Error.");
        unsigned long currentMillis = millis();
        if((currentMillis) >= 10000){
            Serial.print("Turning light off...");
            digitalWrite(SDled, LOW);
            initial = false;
        }
    }
    else {
        digitalWrite(SDled, LOW);
    }
    
    val = digitalRead(Pi_active);
    
    if (! usingInterrupt) {
        // read data from the GPS in the 'main loop'
        char c = GPS.read();
        // if you want to debug, this is a good time to do it!
        if (GPSECHO)
            if (c) Serial.print(c);
    }

    // if a sentence is received, we can check the checksum, parse it...
    if (GPS.newNMEAreceived()) {
        // a tricky thing here is if we print the NMEA sentence, or data
        // we end up not listening and catching other sentences!
        // so be very wary if using OUTPUT_ALLDATA and trying to print out data
        
        // Don't call lastNMEA more than once between parse calls!  Calling lastNMEA
        // will clear the received flag and can cause very subtle race conditions if
        // new data comes in before parse is called again.
        char *stringptr = GPS.lastNMEA();

        if (!GPS.parse(stringptr))   // this also sets the newNMEAreceived() flag to false
            return;  // we can fail to parse a sentence in which case we should just wait for another

        if (LOG_FIXONLY && !GPS.fix) {
            Serial.print("No Fix");
            return;
        }

        // Rad. lets log it!
        uint8_t origstringsize = strlen(stringptr);
        uint8_t stringsize = origstringsize + 4;
        char newstring[stringsize] = {0};
        strcpy(newstring,stringptr);
        newstring[origstringsize-1] = ',';
        if(val==1) {
            newstring[origstringsize] = 'a'; // a for active
        }
        else {
            newstring[origstringsize] = 'n'; // n for not active
        }
        newstring[origstringsize+1] = ',';
        if(relayOpen==1){
            newstring[origstringsize+2] = 'o'; // o for open
        }
        else {
            newstring[origstringsize+2] = 's'; // s for shut
        }
        newstring[origstringsize+3] = ';';
        newstring[origstringsize+4] = '\0';

        if(sderror==false) {
            //write the string to the SD file
            if (stringsize != logfile.write(newstring)){
                error(4);
            }
            if (strstr(stringptr, "RMC") || strstr(stringptr, "GGA"))   logfile.flush();
            Serial.println();
        }

        Serial.print(newstring);
        // if millis() or timer wraps around, we'll just reset it
        if (timer > millis())  timer = millis();

        // approximately every 5 seconds or so, print out the current stats
        if (millis() - timer > 5000) {
            timer = millis(); // reset the timer
            
            if (GPS.fix) {
                if(GPS.speed > set_Speed){
                     if(val==0){
                         // turn PI on
                         digitalWrite(OnSelect, HIGH);  // turn on Pi need to be high for 30 ms return low to conserve power
                         relayOpen = 1; // indicate that relay is open
                         delay(10);
                         digitalWrite(OnSelect, LOW);
                         //digitalWrite(OnSelect, LOW);
                         //give Pi 30 seconds to start up and pull line high or kick out of loop once line is high
                         for(i=0;i<30;i++){
                             delay(1000);
                             val = digitalRead(Pi_active);  // read the input pin
                             if( val == 1){
                                 i=31;  //Pi stated that it is running by pulling the line high
                             }
                         }
                     } //end while loop for Pi recording
                }  //end if loop for speed above threshold
            }  //end loop for GPS fix
            
            if(val!=1){ //Pi indicated that is is done recording by setting line low
                for(i=0;i<5;i++){   //delay 5 seconds and check to see if it is still high
                    delay(1000);
                    val = digitalRead(Pi_active);   // read the input pin
                    if(val == 1) {
                        i=6;        // if high then Pi is still running and a glitch caused a high
                    }
                }
                // if no glitch detected, shut relay
                if(i != 6) {
                    //if you get to this place in this loog the Pi must have stopped recording and 60 seconds has elapsed so we can turn power off to Pi
                    digitalWrite(OffSelect, HIGH);
                    relayOpen = 0; // indicate that relay is shut
                    delay(10);
                    digitalWrite(OffSelect, LOW);
                    delay(500);  // allow some time for the system parasitic power to drain off just in case
                }
            }    
        }  //end if millisec timer is at 5 sec

        delay(500);  //slow the process down
        delay(500);
  }
}
