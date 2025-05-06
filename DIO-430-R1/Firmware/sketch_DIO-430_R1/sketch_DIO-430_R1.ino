#include <Adafruit_PCF8574.h>
#include <ArduinoRS485.h> // ArduinoModbus depends on the ArduinoRS485 library
#include <ArduinoModbus.h>

Adafruit_PCF8574 pcf;

const byte onboardRelay1 = 6;
const byte onboardRelay2 = 8;
const byte onboardRelay3 = 9;

const byte LED1 = 7;
const byte LED2 = 6;
const byte LED3 = 5;

const byte DI1 = 10;
const byte DI2 = 11;
const byte DI3 = 12;
const byte DI4 = 13;

const byte PcfButtonLedPin1 = 3;
const byte PcfButtonLedPin2 = 2;
const byte PcfButtonLedPin3 = 1;

#define SHORT_PRESS_TIME 200 // 500 milliseconds
#define LONG_PRESS_TIME  1000 // 3000 milliseconds

int lastState1 = LOW;  // the previous state from the input pin
int currentState1;     // the current reading from the input pin
int lastState2 = LOW;  // the previous state from the input pin
int currentState2;     // the current reading from the input pin
int lastState3 = LOW;  // the previous state from the input pin
int currentState3;     // the current reading from the input pin

int Relay1Manual = LOW;
int Relay2Manual = LOW;
int Relay3Manual = LOW;

int DI1laststate = LOW;
int DI1currentstate;
int DI2laststate = LOW;
int DI2currentstate;
int DI3laststate = LOW;
int DI3currentstate;
int DI4laststate = LOW;
int DI4currentstate;

unsigned long pressedTime1  = 0;
unsigned long releasedTime1 = 0;
unsigned long pressedTime2  = 0;
unsigned long releasedTime2 = 0;
unsigned long pressedTime3  = 0;
unsigned long releasedTime3 = 0;

void setup()
{
  Serial.begin(115200);
  Serial.println(__FILE__);
  
  if (!pcf.begin(0x38, &Wire)) {
    Serial.println("Couldn't find PCF8574");
    while (1);
  }
  
  pinMode(onboardRelay1, OUTPUT);
  pinMode(onboardRelay2, OUTPUT);
  pinMode(onboardRelay3, OUTPUT);
  pinMode(DI1, INPUT);
  pinMode(DI2, INPUT);
  pinMode(DI3, INPUT);
  pinMode(DI4, INPUT);
  pcf.pinMode(LED1, OUTPUT);
  pcf.pinMode(LED2, OUTPUT);
  pcf.pinMode(LED3, OUTPUT);
  pcf.pinMode(PcfButtonLedPin1, INPUT);
  pcf.pinMode(PcfButtonLedPin2, INPUT);
  pcf.pinMode(PcfButtonLedPin3, INPUT);
  Serial.println("Modbus RTU Slave");
  // start the Modbus RTU server, with (slave) id 1
  if (!ModbusRTUServer.begin(1, 115200)) {
    Serial.println("Failed to start Modbus RTU Slave!");
    while (1);
  }
  // configure a single coil at address 0x01
  ModbusRTUServer.configureCoils(0x01, 9);
}


void loop() {
 int currentState1 = pcf.digitalRead(PcfButtonLedPin1);
 int currentState2 = pcf.digitalRead(PcfButtonLedPin2);
 int currentState3 = pcf.digitalRead(PcfButtonLedPin3);

 int DI1currentstate = digitalRead(DI1);
 int DI2currentstate = digitalRead(DI2);
 int DI3currentstate = digitalRead(DI3);
 int DI4currentstate = digitalRead(DI4);


 int packetReceived = ModbusRTUServer.poll();
  if (DI1currentstate != DI1laststate){       // DI changed
    Serial.println("DI1");
  }
  if (DI2currentstate != DI2laststate){       // DI changed
    Serial.println("DI2");
  }
  if (DI3currentstate != DI3laststate){       // DI changed
    Serial.println("DI3");
  }
  if (DI4currentstate != DI4laststate){       // DI changed
//    Serial.println("DI4");
  }
  if(packetReceived) {
    // read the current value of the coil
    int Relay1 = ModbusRTUServer.coilRead(0x01);
    int Relay2 = ModbusRTUServer.coilRead(0x02);
    int Relay3 = ModbusRTUServer.coilRead(0x03);
    ModbusRTUServer.coilWrite(0x04, digitalRead(DI1));
    ModbusRTUServer.coilWrite(0x05, digitalRead(DI2));
    ModbusRTUServer.coilWrite(0x06, digitalRead(DI3));
    ModbusRTUServer.coilWrite(0x07, Relay1Manual);
    ModbusRTUServer.coilWrite(0x08, Relay2Manual);
    ModbusRTUServer.coilWrite(0x09, Relay3Manual);
    if (Relay1 && !Relay1Manual) {
      // coil value set, turn LED on
      digitalWrite(onboardRelay1, HIGH);
    } else if (!Relay1Manual) {
      // coil value clear, turn LED off
      digitalWrite(onboardRelay1, LOW);
    }
    if (Relay2) {
      // coil value set, turn LED on
      digitalWrite(onboardRelay2, HIGH);
    } else {
      // coil value clear, turn LED off
      digitalWrite(onboardRelay2, LOW);
    }
    if (Relay3) {
      // coil value set, turn LED on
      digitalWrite(onboardRelay3, HIGH);
    } else {
      // coil value clear, turn LED off
      digitalWrite(onboardRelay3, LOW);
    }
  }
  if (lastState1 == HIGH && currentState1 == LOW){       // button is pressed
    pressedTime1 = millis();
  }
  else if (lastState1 == LOW && currentState1 == HIGH) { // button is released
    releasedTime1 = millis();
    long pressDuration1 = releasedTime1 - pressedTime1;
    if ( pressDuration1 < SHORT_PRESS_TIME && Relay1Manual ){
      digitalWrite(onboardRelay1, !digitalRead(onboardRelay1));
      Serial.print("Relay1");
      Serial.print(digitalRead(onboardRelay1));
    }
    else if ( pressDuration1 > LONG_PRESS_TIME ){
      Relay1Manual = !Relay1Manual;
      Serial.print("Relay1 Manual mode");
      Serial.println(Relay1Manual);
      Serial.println(pressDuration1);
      pcf.digitalWrite(LED1, !pcf.digitalRead(LED1));
    }
   } 
  // save the the last state
  DI1laststate = DI1currentstate;
  DI2laststate = DI2currentstate;
  DI3laststate = DI3currentstate;
  DI4laststate = DI4currentstate;
  lastState1 = currentState1;
  lastState2 = currentState2;
  lastState3 = currentState3; 
}