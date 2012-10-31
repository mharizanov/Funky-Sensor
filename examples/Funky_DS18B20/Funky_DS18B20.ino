//--------------------------------------------------------------------------------------
// Funky Wireless Temperature Sensor Node
// To use with DS18B20, a 4K7 resistor is needed between pads 4 and 5 of the Funky (Vdd and DQ)
// GNU GPL V3
//--------------------------------------------------------------------------------------

#include <JeeLib.h> // https://github.com/jcw/jeelib
#include "pins_arduino.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#define TEMPERATURE_PRECISION 9
 
#define ONE_WIRE_BUS 3  // pad 5 of the Funky
#define tempPower 7     // Power pin is connected pad 4 on the Funky


// Setup a oneWire instance to communicate with any OneWire devices 
// (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
 
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

//#define LEDpin PB0 new revisions use this pin as CLKI
#define LEDpin 10


ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

#define myNodeID 9      // RF12 node ID in the range 1-30
#define network 210      // RF12 Network group
#define freq RF12_868MHZ // Frequency of RFM12B module


//########################################################################################################################
//Data Structure to be sent
//########################################################################################################################

 typedef struct {
    int supplyV;	// Supply voltage
	int temp;	// Temperature reading
  	int temp2;	// Temperature 2 reading
 } Payload;

 Payload temptx;

 static int numSensors;
 
//########################################################################################################################

void setup() {

  pinMode(LEDpin,OUTPUT);
  digitalWrite(LEDpin,LOW);  // LED on
  Sleepy::loseSomeTime(1000); 
  digitalWrite(LEDpin,HIGH);   // LED off
  
  PRR = bit(PRTIM1); // only keep timer 0 going
  ADCSRA &= ~ bit(ADEN); // Disable the ADC
  bitSet (PRR, PRADC);   // Power down ADC
  bitClear (ACSR, ACIE); // Disable comparitor interrupts
  bitClear (ACSR, ACD);  // Power down analogue comparitor

  rf12_initialize(myNodeID,freq,network); // Initialize RFM12 with settings defined above 
  // Adjust low battery voltage to 2.2V
  
  rf12_control(0xC040);
  rf12_sleep(0);                          // Put the RFM12 to sleep
  
  pinMode(tempPower, OUTPUT); // set power pin for DS18B20 to output
  digitalWrite(tempPower, HIGH); // turn sensor power on
  Sleepy::loseSomeTime(50); // Allow 50ms for the sensor to be ready
  // Start up the library
  sensors.begin(); 
  numSensors=sensors.getDeviceCount(); 
}

void loop() {
  
  digitalWrite(LEDpin,LOW);  
  digitalWrite(tempPower, HIGH); // turn DS18B20 sensor on
  Sleepy::loseSomeTime(50); // Allow 50ms for the sensor to be ready
  
  sensors.requestTemperatures(); // Send the command to get temperatures  
//  sensors.getTempCByIndex(0);
  
  temptx.temp=(sensors.getTempCByIndex(0)*100); // read sensor 1
  if (numSensors>1) temptx.temp2=(sensors.getTempCByIndex(1)*100); // read second sensor.. you may have multiple and count them upon startup but I only need two

  digitalWrite(tempPower, LOW); // turn DS18B20 sensor off
  
  bitClear(PRR, PRADC); // power up the ADC
  ADCSRA |= bit(ADEN); // enable the ADC  
  Sleepy::loseSomeTime(50); 
  temptx.supplyV = readVcc(); // Get supply voltage
  ADCSRA &= ~ bit(ADEN); // disable the ADC
  bitSet(PRR, PRADC); // power down the ADC

  digitalWrite(LEDpin,HIGH);  
  
  rfwrite(); // Send data via RF 

  digitalWrite(tempPower, LOW);
  pinMode(tempPower, INPUT); // set power pin for DS18B20 to input before sleeping, saves power
   
  for(byte j = 0; j < 5; j++) {    // Sleep for 5 minutes
    Sleepy::loseSomeTime(60000); //JeeLabs power save function: enter low power mode for 60 seconds (valid range 16-65000 ms)
  }

  pinMode(tempPower, OUTPUT); // set power pin for DS18B20 to output  
}

//--------------------------------------------------------------------------------------------------
// Send payload data via RF
//--------------------------------------------------------------------------------------------------
 static void rfwrite(){
   bitClear(PRR, PRUSI); // enable USI h/w  
   rf12_sleep(-1);     //wake up RF module
   while (!rf12_canSend())
   rf12_recvDone();
   rf12_sendStart(0, &temptx, sizeof temptx); 
   rf12_sendWait(2);    //wait for RF to finish sending while in standby mode
   rf12_sleep(0);    //put RF module to sleep
   bitSet(PRR, PRUSI); // disable USI h/w
}

//--------------------------------------------------------------------------------------------------
// Read current supply voltage
//--------------------------------------------------------------------------------------------------

 long readVcc() {
   long result;
   // Read 1.1V reference against Vcc
   ADMUX = _BV(MUX5) | _BV(MUX0);
   delay(2); // Wait for Vref to settle
   ADCSRA |= _BV(ADSC); // Convert
   while (bit_is_set(ADCSRA,ADSC));
   result = ADCL;
   result |= ADCH<<8;
   result = 1126400L / result; // Back-calculate Vcc in mV
   return result;
}
