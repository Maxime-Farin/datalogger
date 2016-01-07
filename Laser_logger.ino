#include <avr/sleep.h>
#include <SdFat.h>
#include <Wire.h>
#include <DS3231.h>

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif

SdFat sd;
SdFile datafile;
SdFile laserdatafile;
SdFile laserstartfile;
SdFile laserstopfile;

char* filename="RAIN.TXT";
char* laserfilename="LASER.TXT";
char* startfilename = "START.TXT";
char* stopfilename = "STOP.TXT";

RTClib RTC;
DS3231 Clock;

const int SDpin = 8; // Turns on voltage source to SD card
const int CSpin = 10;

int LaserPinP = 0;
int LaserPinM = 1;
int PowerPin = 6;
int TipPin = 3;
int LEDpin = 9;

bool tip_wakeup = false;

//int ntip = 0;
double val1;
double val2;
double tension;
int time_between_samples = 200; // in ms (for laser)
int additional_time = 1800;//1800; // in s (additional time when bucket tips)
volatile int counter_time = 0;
int laser_clock;

DateTime instant_t;
DateTime start_time;

void setup() {
  // put your setup code here, to run once:
  pinMode(TipPin, INPUT); // Necessary or else interrupt won't work (Rain gage Pin)
  digitalWrite(TipPin, HIGH);
  pinMode(2, INPUT); // Necessary or else interrupt won't work (Clock Pin)
  digitalWrite(2, HIGH); 
  
  pinMode(PowerPin, OUTPUT); // Laser is initially off
  digitalWrite(PowerPin, LOW);
  
  pinMode(SDpin, INPUT);
  digitalWrite(SDpin, HIGH);
  
  // I don't know exactly what these lines are doing
  //cbi(ADCSRA,ADEN);                    // switch Analog to Digitalconverter OFF
  //Clock.setA2Time(1, 0, 0, 0b01110000, false, false, false); // just min mask - set for laser timeout


  pinMode(LEDpin, OUTPUT); // Manual wake pin of the LED
  LEDgood();
  
  Serial.begin(57600); 
  Serial.println(" --- ");
  Serial.println("Start !");
  Serial.println(" --- ");

  delay(10);
}


// MAIN LOOP
void loop() {
  //set_sleep_mode(SLEEP_MODE_PWR_DOWN);   // sleep mode is set here
  //sleep_enable();   
  
  attachInterrupt(1, wakeUpNow_tip, LOW); // interrupt 1 is pin 3; triggers function wakeUpNow_tip whenever the pin 1 is LOW
  delay(20);
  
  if (tip_wakeup == true){
    tip_wakeup = false;
    delay(50);
    
    Serial.println(" ");
    Serial.println("TIP !");
    Serial.println(" ");
    
    // TIP COUNTER
    log_time();
    
    digitalWrite(LEDpin, HIGH);
    delay(50);
    digitalWrite(LEDpin, LOW);
    
    MesureLaser();
    }
  
  //sleep_mode();            // here the device is actually put to sleep!!
}


void MesureLaser(){
  Serial.println(" --- ");
  Serial.println("Powering up Laser...");
  Serial.println(" --- ");
  digitalWrite(PowerPin, HIGH);
  digitalWrite(LEDpin, HIGH);
  log_laser_start();
  
  delay(1000);
  
  start_time = RTC.now(); // get starting time on RTC Clock
  instant_t = RTC.now(); // get actual time on RTC Clock
  laser_clock = counter_time - (instant_t.unixtime() - start_time.unixtime()); // decrement counter
      
  while(laser_clock >= 0){

    attachInterrupt(1, wakeUpNow_tip, LOW); // interrupt 1 is pin 3; triggers function wakeUpNow_tip whenever the pin 1 is LOW
    delay(20);
    
    if (tip_wakeup == true){
      tip_wakeup = false;
      Serial.println(" ");
      Serial.println("TIP !");
      Serial.println(" ");

      // TIP COUNTER
      log_time();
    }
    
    log_laser();    
    delay(time_between_samples);
    
    instant_t = RTC.now(); // get actual time on RTC Clock
    laser_clock = counter_time - (instant_t.unixtime() - start_time.unixtime()); // decrement counter

    Serial.print(" --- ");
    Serial.print("time remaining: ");
    Serial.print(laser_clock/60.0);
    Serial.println(" min --- ");
    
  }
  
  counter_time = 0; // set counter back to 0 when acquisition is done.
  
  Serial.println(" --- ");
  Serial.println("Powering down Laser...");
  Serial.println(" --- ");
  digitalWrite(PowerPin, LOW);
  digitalWrite(LEDpin, LOW);
  log_laser_stop();
    
}


// Interrupt Service Routines (ISR) 
// called in attachInterrupt functions

void wakeUpNow_tip(){
  detachInterrupt(1);
  counter_time = counter_time + additional_time; // add 30 min of recording at every TIP
  tip_wakeup = true;
}



// SD ON AND OFF FUNCTIONS

void SDon(){
  pinMode(SDpin,OUTPUT); // Seemed to have forgotten between loops... ?
  // Initialize logger
  digitalWrite(SDpin,HIGH); // Turn on SD card before writing to it
                            // Delay required after this??
  delay(10);
  if (!sd.begin(CSpin, SPI_HALF_SPEED)) {
    // Just use Serial.println: don't kill batteries by aborting code 
    // on error
    Serial.println("Error initializing SD card for writing");
    Serial.println(" ");
  delay(10);
  }
}

void SDoff(){
  pinMode(SDpin,OUTPUT); // Seemed to have forgotten between loops... ?
  // Initialize logger
  digitalWrite(SDpin,LOW); // Turn off SD card after writing to it
                            // Delay required after this??
}



// LOG functions

void log_time(){
  SDon(); // turns SD on
  // open the file for write at end like the Native SD library
  if (!datafile.open(filename, O_WRITE | O_CREAT | O_AT_END)) {
    // Just use Serial.println: don't kill batteries by aborting code 
    // on error
    Serial.print("Opening ");
    Serial.print(filename);
    Serial.println(" for write failed");
    Serial.println(" ");
  delay(10);
  }
  
  instant_t = RTC.now(); // get actual time on RTC Clock
  // SD
  datafile.println(instant_t.unixtime()); // write time on file in SD
  
  delay(20);

  datafile.close();
  
  delay(20);
      
  SDoff(); // turns SD off
  
  // Echo to serial
  Serial.println(instant_t.unixtime());   
}



void log_laser(){
  SDon(); // turns SD on

  if (!laserdatafile.open(laserfilename, O_WRITE | O_CREAT | O_AT_END)) {
    Serial.print("Opening ");
    Serial.print(laserfilename);
    Serial.println(" for write failed");
    Serial.println(" ");
  delay(10);
  }

  instant_t = RTC.now();
  
  // the logger is sensitive in the range 0 - 3.3 V   
  val1 = analogRead(LaserPinP);
  val2 = analogRead(LaserPinM);
  tension = abs(val1-val2)*5.0/1023.0;

  // Record in SD card
  laserdatafile.print(instant_t.unixtime());
  laserdatafile.print("\t");
  laserdatafile.println(tension); // write laser value on file in SD
  delay(20);

  laserdatafile.close();
  delay(20);
      
  SDoff(); // turns SD off
  
  // Echo in serial  
  Serial.print(instant_t.unixtime());
  Serial.print("\t");
  Serial.println(tension);
  }


void log_laser_start(){
  SDon();
  if (!laserstartfile.open(startfilename, O_WRITE | O_CREAT | O_AT_END)) {
    Serial.print("Opening ");
    Serial.print(startfilename);
    Serial.println(" for write failed");
    Serial.println(" ");
  delay(10);
  }

  // Datestamp the line
  instant_t = RTC.now();
  // SD
  laserstartfile.println(instant_t.unixtime());
    
  delay(20);

  laserstartfile.close();
  
  delay(20);
      
  SDoff(); // turns SD off
  
  // Echo to serial
  Serial.print("laser start: ");
  Serial.println(instant_t.unixtime());
  Serial.println(" ");

}


void log_laser_stop(){
  SDon();
  if (!laserstopfile.open(stopfilename, O_WRITE | O_CREAT | O_AT_END)) {
    Serial.print("Opening ");
    Serial.print(stopfilename);
    Serial.println(" for write failed");
  delay(10);
  }

  // Datestamp the line
  instant_t = RTC.now();
  // SD
  laserstopfile.println(instant_t.unixtime());
  
  delay(20);

  laserstopfile.close();
  
  delay(20);
      
  SDoff(); // turns SD off
  
  // Echo to serial
  Serial.print("laser stop: ");
  Serial.println(instant_t.unixtime());
}


void LEDgood()
{
  // Peppy blinky pattern to show that the logger has successfully initialized
  digitalWrite(LEDpin,HIGH);
  delay(1000);
  digitalWrite(LEDpin,LOW);
  delay(300);
  digitalWrite(LEDpin,HIGH);
  delay(100);
  digitalWrite(LEDpin,LOW);
  delay(100);
  digitalWrite(LEDpin,HIGH);
  delay(100);
  digitalWrite(LEDpin,LOW);
}
