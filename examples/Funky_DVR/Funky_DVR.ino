//--------------------------------------------------------------------------------------
// Intelligent DVR sensor
// harizanov.com
// GNU GPL V3
//--------------------------------------------------------------------------------------

#include <avr/sleep.h>

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

#include <JeeLib.h> // https://github.com/jcw/jeelib
#include "pins_arduino.h"

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

#define myNodeID 6      // RF12 node ID in the range 1-30
#define network 210      // RF12 Network group
#define freq RF12_868MHZ // Frequency of RFM12B module

#define RETRY_PERIOD 5    // How soon to retry (in seconds) if ACK didn't come in
#define RETRY_LIMIT 5     // Maximum number of times to retry
#define ACK_TIME 20       // Number of milliseconds to wait for an ack


#define LEDpin 10


//########################################################################################################################
//Data Structure to be sent
//########################################################################################################################

 typedef struct {
  	  int state;	// State
  	  int supplyV;	// Supply voltage
 } Payload;

static Payload temptx;
static int gotPCI;
static unsigned int counter;
 
 ISR(PCINT0_vect) {
  temptx.state=bitRead(PINA,1);
  gotPCI=true;
  counter++;
 }

void setup() {

  pinMode(LEDpin,OUTPUT);
  digitalWrite(LEDpin,LOW);// LED on  
  Sleepy::loseSomeTime(1000);
  digitalWrite(LEDpin,HIGH);// LED off

  pinMode(9,INPUT);
  
  rf12_initialize(myNodeID,freq,network); // Initialize RFM12 with settings defined above 
  // Adjust low battery voltage to 2.2V
  rf12_control(0xC040);
  rf12_sleep(0);                          // Put the RFM12 to sleep

  PRR = bit(PRTIM1); // only keep timer 0 going
  gotPCI=false;
}

void loop() {
  
  digitalWrite(LEDpin,LOW);      // LED on  

  Sleepy::loseSomeTime(16); 

  digitalWrite(LEDpin,HIGH);  //LED off
  
  if(gotPCI && counter > 10) {  //How did we get here? Thru PinChangeInterrupt or wait expired?)
    temptx.state=1;
    bitClear(PRR, PRADC); // power up the ADC
    ADCSRA |= bit(ADEN); // enable the ADC  
    Sleepy::loseSomeTime(16); // Allow 10ms for the sensor to be ready

    temptx.supplyV = readVcc(); // Get supply voltage

    ADCSRA &= ~ bit(ADEN); // disable the ADC
    bitSet(PRR, PRADC); // power down the ADC
    
    rfwrite(); // Send data via RF   
    
    Sleepy::loseSomeTime(65000); //JeeLabs power save function: enter low power mode for 60 seconds (valid range 16-65000 ms)
    temptx.state=0;  // Set the status to off
    counter=0;    
    rfwrite(); // Send data via RF     
  }  
  Sleepy::loseSomeTime(500);  //sleep some more to debounce
  
  sbi(GIMSK,PCIE0); // Turn on Pin Change interrupt
  sbi(PCMSK0,PCINT1); // Which pins are affected by the interrupt
  
  gotPCI=false;
  for(int j = 1; ((j < 30) && (gotPCI==false)); j++) {    // Sleep for 30 minutes
    Sleepy::loseSomeTime(60000); //JeeLabs power save function: enter low power mode for 60 seconds (valid range 16-65000 ms)
    if(gotPCI==false) counter=0;  // Reset the counter, if no waking up for the last 1 min
  }
  
//  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Set sleep mode
//  sleep_mode(); // System sleeps here

  cbi(GIMSK,PCIE0); // Turn off Pin Change interrupt

}

//--------------------------------------------------------------------------------------------------
// Send payload data via RF
//--------------------------------------------------------------------------------------------------
static void rfwrite(){
   bitClear(PRR, PRUSI); // enable USI h/w
   
  //http://nathan.chantrell.net/20120827/tinytx-ack-support-added/
  for (byte i = 0; i <= RETRY_LIMIT; ++i) {  // tx and wait for ack up to RETRY_LIMIT times
     rf12_sleep(-1);              // Wake up RF module
      while (!rf12_canSend())
      rf12_recvDone();
      rf12_sendStart(RF12_HDR_ACK, &temptx, sizeof temptx); 
      rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
      byte acked = waitForAck();
      rf12_sleep(0);              // Put RF module to sleep
      if (acked) { return; }
  
   Sleepy::loseSomeTime(RETRY_PERIOD * 1000);     // If no ack received wait and try again
   }   
   bitSet(PRR, PRUSI); // disable USI h/w
}

// Wait a few milliseconds for proper ACK
 static byte waitForAck() {
   MilliTimer ackTimer;
   while (!ackTimer.poll(ACK_TIME)) {
     if (rf12_recvDone() && rf12_crc == 0 &&
        rf12_hdr == (RF12_HDR_DST | RF12_HDR_CTL | myNodeID))
        return 1;
     }
   return 0;
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




