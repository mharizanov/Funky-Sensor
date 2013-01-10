#define LEDpin 10

void setup() {
  pinMode(LEDpin,OUTPUT);
}

void loop() {
  digitalWrite(LEDpin,LOW);
  delay(1000);
  digitalWrite(LEDpin,HIGH);
  delay(1000);
} 
