#include <Arduino.h>

#define POT_INPUT_PIN PA0
#define INPUT_RESOLUTION 12

void startBlink(){
  digitalWrite(PC13, LOW);
  delay(100);
  digitalWrite(PC13, HIGH);
}

void setup() {
  Serial.begin(115200);
  pinMode(POT_INPUT_PIN, INPUT_ANALOG);
  pinMode(PC13, OUTPUT);

  startBlink();
}

void loop() {
  analogReadResolution(INPUT_RESOLUTION);
  Serial.print(analogRead(POT_INPUT_PIN));
  Serial.print("\n");
  delay(50);
}