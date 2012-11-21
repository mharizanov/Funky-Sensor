//--------------------------------------------------------------------------------------
// Internal temperature measurement of Attiny84 based TinySensor
// harizanov.com
// GNU GPL V3
//--------------------------------------------------------------------------------------


#include <JeeLib.h> // https://github.com/jcw/jeelib
#include "pins_arduino.h"

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

#define myNodeID 18      // RF12 node ID in the range 1-30
#define network 210      // RF12 Network group
#define freq RF12_868MHZ // Frequency of RFM12B module

#define RETRY_PERIOD 5    // How soon to retry (in seconds) if ACK didn't come in
#define RETRY_LIMIT 5     // Maximum number of times to retry
#define ACK_TIME 10       // Number of milliseconds to wait for an ack


//#define LEDpin PB0 new revisions use this pin as CLKI
#define LEDpin 10

#define TEMPERATURE_ADJUSTMENT 26-43
#define EXTREMES_RATIO 5
#define MAXINT 32767
#define MININT -32767

int offset=TEMPERATURE_ADJUSTMENT;
float coefficient=1;
int readings[30];
int pos=0;


//########################################################################################################################
//Data Structure to be sent
//########################################################################################################################

 typedef struct {
  	  int temp;	// Temperature reading
  	  int supplyV;	// Supply voltage
 } Payload;

 Payload temptx;


void setup() {

  pinMode(LEDpin,OUTPUT);
  digitalWrite(LEDpin,LOW);
  delay(1000);
  digitalWrite(LEDpin,HIGH);

  rf12_initialize(myNodeID,freq,network); // Initialize RFM12 with settings defined above 
  // Adjust low battery voltage to 2.2V
  rf12_control(0xC040);
  rf12_sleep(0);                          // Put the RFM12 to sleep
  PRR = bit(PRTIM1); // only keep timer 0 going
}

void loop() {
  pinMode(LEDpin,OUTPUT);
  digitalWrite(LEDpin,LOW);  
  bitClear(PRR, PRADC); // power up the ADC
  ADCSRA |= bit(ADEN); // enable the ADC  
  Sleepy::loseSomeTime(16); // Allow 10ms for the sensor to be ready

  int_sensor_init();
//  sprint();
  temptx.temp = in_c() * 100; // Convert temperature to an integer, reversed at receiving end
  temptx.supplyV = readVcc(); // Get supply voltage

  ADCSRA &= ~ bit(ADEN); // disable the ADC
  bitSet(PRR, PRADC); // power down the ADC

  digitalWrite(LEDpin,HIGH);  

  rfwrite(); // Send data via RF 

  for(int j = 0; j < 1; j++) {    // Sleep for 5 minutes
    Sleepy::loseSomeTime(6000); //JeeLabs power save function: enter low power mode for 60 seconds (valid range 16-65000 ms)
  }
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


void sprint() {
  Serial.print( "> R:" );
  Serial.print( raw(), DEC );
  Serial.print( " L:" );
  Serial.print( in_lsb(), DEC );
  Serial.print( " K:" );
  Serial.print( in_k(), DEC );
  Serial.print( " C:" );
  Serial.print( in_c(), DEC );
  Serial.print( " F:" );
  Serial.print( in_f(), DEC );
  Serial.println( " # " );
}

void int_sensor_init() {

  //analogReference( INTERNAL1V1 );
  // Configure ADMUX

  ADMUX = B00100010;                // Select temperature sensor
  ADMUX &= ~_BV( ADLAR );       // Right-adjust result
  ADMUX |= _BV( REFS1 );                      // Set Ref voltage
  ADMUX &= ~( _BV( REFS0 ) );  // to 1.1V
  // Configure ADCSRA
  ADCSRA &= ~( _BV( ADATE ) |_BV( ADIE ) ); // Disable autotrigger, Disable Interrupt
  ADCSRA |= _BV(ADEN);                      // Enable ADC
  ADCSRA |= _BV(ADSC);          // Start first conversion
  // Seed samples
  int raw_temp;
  while( ( ( raw_temp = raw() ) < 0 ) );
  for( int i = 0; i < 30; i++ ) {
    readings[i] = raw_temp;
  }
}

int in_lsb() {
  int readings_dup[30];
  int raw_temp;
  // remember the sample
  if( ( raw_temp = raw() ) > 0 ) {
    readings[pos] = raw_temp;
    pos++;
    pos %= 30;
  }
  // copy the samples
  for( int i = 0; i < 30; i++ ) {
    readings_dup[i] = readings[i];
  }
  // bubble extremes to the ends of the array
  int extremes_count = 6;
  int swap;
  for( int i = 0; i < extremes_count; ++i ) { // percent of iterations of bubble sort on small N works faster than Q-sort
    for( int j = 0;j<29;j++ ) {
      if( readings_dup[i] > readings_dup[i+1] ) { 
        swap = readings_dup[i];
        readings_dup[i] = readings_dup[i+1];
        readings_dup[i+1] = swap;
      }
    }
  }
  // average the middle of the array
  int sum_temp = 0;
  for( int i = extremes_count; i < 30 - extremes_count; i++ ) {
    sum_temp += readings_dup[i];
  }
  return sum_temp / ( 30 - extremes_count * 2 );
}


int in_c() {
  return in_k() - 273;
}

int in_f() {
  return in_c() * 9 / 5 + 32;
}

int in_k() {
  return in_lsb() + offset; // for simplicty I'm using k=1, use the next line if you want K!=1.0
  //return (int)( in_lsb() * coefficient ) + offset;
}

int raw() {
  if( ADCSRA & _BV( ADSC ) ) {
    return -1;
  } else {
    int ret = ADCL | ( ADCH << 8 );   // Get the previous conversion result
    ADCSRA |= _BV(ADSC);              // Start new conversion
    return ret;
  }
}

