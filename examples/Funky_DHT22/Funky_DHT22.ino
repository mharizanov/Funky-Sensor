//--------------------------------------------------------------------------------------
// DHT22 temperature measurement of Attiny84 based Funky step-up
// based on Nathan's code https://github.com/nathanchantrell/TinyTX/blob/master/TinyTX_DHT22/TinyTX_DHT22.ino
// harizanov.com
// GNU GPL V3
//--------------------------------------------------------------------------------------

#include <DHT22.h> // https://github.com/nathanchantrell/Arduino-DHT22
#include <JeeLib.h> // https://github.com/jcw/jeelib
#include "pins_arduino.h"

#define PowerPin 7      // DHT Power pin is connected on pin 
#define DHT22Pin 3      // DHT Data pin

DHT22 dht(DHT22Pin); // Setup the DHT

#define USE_ACK           // Enable ACKs, comment out to disable
#define RETRY_PERIOD 1    // How soon to retry (in seconds) if ACK didn't come in
#define RETRY_LIMIT 5     // Maximum number of times to retry
#define ACK_TIME 15       // Number of milliseconds to wait for an ack


ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

#define myNodeID 28      // RF12 node ID in the range 1-30
#define network 210      // RF12 Network group
#define freq RF12_868MHZ // Frequency of RFM12B module

#define LEDpin 10 //#0 on early v1 revisions


//########################################################################################################################
//Data Structure to be sent
//########################################################################################################################

 typedef struct {
  	  int temp;	// Temperature reading
//  	  int supplyV;	// Supply voltage
          int humidity;
 } Payload;

 Payload temptx;

static void setPrescaler (uint8_t mode) {
    cli();
    CLKPR = bit(CLKPCE);
    CLKPR = mode;
    sei();
}
 
void setup() {

//  setPrescaler(1);    // div 1, i.e. 4 MHz
  pinMode(LEDpin,OUTPUT);
  digitalWrite(LEDpin,LOW);    
  Sleepy::loseSomeTime(1024); 
  digitalWrite(LEDpin,HIGH);

  rf12_initialize(myNodeID,freq,network); // Initialize RFM12 with settings defined above 
  rf12_control(0xC040);
  rf12_sleep(0);                          // Put the RFM12 to sleep
  PRR = bit(PRTIM1); // only keep timer 0 going
}

void loop() {
  pinMode(LEDpin,OUTPUT);
  digitalWrite(LEDpin,LOW);

  pinMode(PowerPin,OUTPUT);
  digitalWrite(PowerPin,HIGH);
 
  bitClear(PRR, PRADC); // power up the ADC
  ADCSRA |= bit(ADEN); // enable the ADC    
  
  Sleepy::loseSomeTime(50); // Allow for the LED blink to become visible
  digitalWrite(LEDpin,HIGH);    //LED off
    
  Sleepy::loseSomeTime(2000); // Allow for the sensor to be ready

  DHT22_ERROR_t errorCode;  
  errorCode = dht.readData(); // read data from sensor
  digitalWrite(PowerPin,LOW);   //Power DHT22 off

  if (errorCode == DHT_ERROR_NONE) { // data is good  
    temptx.temp = (dht.getTemperatureC()*100); // Get temperature reading and convert to integer, reversed at receiving end
//  temptx.supplyV = readVcc(); // Get supply voltage (no need in the step-up Funkya as it is always 3.3V
    temptx.humidity = (dht.getHumidity()*100); // Get humidity reading and convert to integer, reversed at receiving end
    rfwrite(); // Send data via RF   
} else { // Error reading the DHT22
 for(int i=0;i<5;i++){
  digitalWrite(LEDpin,LOW);    //LED on
  Sleepy::loseSomeTime(35); // Allow for the LED blink to become visible
  digitalWrite(LEDpin,HIGH);    //LED off
  Sleepy::loseSomeTime(35); // Allow for the LED blink to become visible
 }
}
  
  
  ADCSRA &= ~ bit(ADEN); // disable the ADC
  bitSet(PRR, PRADC); // power down the ADC

  for(int j = 0; j < 2; j++) {    // Sleep for 2 minutes
    Sleepy::loseSomeTime(60000); //JeeLabs power save function: enter low power mode for 60 seconds (valid range 16-65000 ms)
  }
}


// Wait a few milliseconds for proper ACK
 #ifdef USE_ACK
  static byte waitForAck() {
   MilliTimer ackTimer;
   while (!ackTimer.poll(ACK_TIME)) {
     if (rf12_recvDone() && rf12_crc == 0 &&
        rf12_hdr == (RF12_HDR_DST | RF12_HDR_CTL | myNodeID))
        return 1;
     }
   return 0;
  }
 #endif

//--------------------------------------------------------------------------------------------------
// Send payload data via RF
//-------------------------------------------------------------------------------------------------
 static void rfwrite(){
   bitClear(PRR, PRUSI); // enable USI h/w
   #ifdef USE_ACK
   for (byte i = 0; i <= RETRY_LIMIT; ++i) {  // tx and wait for ack up to RETRY_LIMIT times
     rf12_sleep(-1);              // Wake up RF module
      while (!rf12_canSend())
      rf12_recvDone();
      rf12_sendStart(RF12_HDR_ACK, &temptx, sizeof temptx); 
      rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
      byte acked = waitForAck();  // Wait for ACK
      rf12_sleep(0);              // Put RF module to sleep
      if (acked) {       
        bitSet(PRR, PRUSI); // disable USI h/w 
        return; 
      }      // Return if ACK received
  
   Sleepy::loseSomeTime(RETRY_PERIOD * 1000);     // If no ack received wait and try again
   }
  #else
     rf12_sleep(-1);              // Wake up RF module
     while (!rf12_canSend())
     rf12_recvDone();
     rf12_sendStart(0, &temptx, sizeof temptx); 
     rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
     rf12_sleep(0);              // Put RF module to sleep
     bitSet(PRR, PRUSI); // disable USI h/w
     return;
  #endif
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


