#include <PCF8574.h>
#include <ModbusSerial.h>
#include <Wire.h>
#define SDA 6
#define SCL 7
#define TX2 4
#define RX2 5
#include <ModbusSerial.h>

const int TxenPin = -1; // -1 disables the feature, change that if you are using an RS485 driver, this pin would be connected to the DE and /RE pins of the driver.
const byte SlaveId = 1;
// Modbus Registers Offsets (0-9999)
const int Lamp1Coil = 0;
const int Lamp2Coil = 1;
const int Lamp3Coil = 2;
const int Lamp4Coil = 3;
const int Relay1Coil = 4;
const int Relay2Coil = 5;
const int Relay3Coil = 6;

const int SwitchIsts = 11;


// ModbusSerial object
ModbusSerial mb (Serial2, SlaveId, TxenPin);

PCF8574 pcf20(0x20, &Wire1);
PCF8574 pcf21(0x21, &Wire1);
PCF8574 pcf23(0x23, &Wire1);
PCF8574 pcf27(0x27, &Wire1);



void setup()
{
  Serial.begin(9600);
  Serial.println("\nI2C 8574");
  Serial.print("PCF8574_LIB_VERSION: ");
  Serial.println(PCF8574_LIB_VERSION);
  Wire1.setSDA(SDA);  // setup SDA and SCL pins
  Wire1.setSCL(SCL);
  Wire1.begin();
  pcf20.begin();
  pcf21.begin();
  pcf23.begin();
  pcf27.begin();

  Serial2.setTX(TX2);  
  Serial2.setRX(RX2);
  Serial2.begin(19200); // UART1 // works on all boards but the configuration is 8N1 which is incompatible with the MODBUS standard
  // prefer the line below instead if possible
  // MySerial.begin (Baudrate, MB_PARITY_EVEN);
  
  mb.config (19200);
  mb.setAdditionalServerData ("LAMP"); // for Report Server ID function (0x11)
  //mb.setAdditionalServerData ("SWITCH"); // for Report Server ID function (0x11)

  // Set LedPin mode
 // pinMode (LedPin, OUTPUT);
  // Add Lamp1Coil register - Use addCoil() for digital outputs
  mb.addCoil (Lamp1Coil);
  mb.addCoil (Lamp2Coil);
  mb.addCoil (Lamp3Coil);
  mb.addCoil (Lamp4Coil);
  mb.addCoil (Relay1Coil);
  mb.addCoil (Relay2Coil);
  mb.addCoil (Relay3Coil);
//  mb.addIsts (SwitchIsts);
  mb.addCoil (SwitchIsts);
}


void loop() {
//  Serial.println(pcf20.read8(), BIN);
//  Serial.println(pcf21.read8(), BIN);
//  Serial.println(pcf23.read8(), BIN);
//  Serial.println(pcf27.read8(), BIN);
  //pcf23.toggle(3);
//  pcf23.toggle(7);
//  pcf23.toggle(6);
//  Serial.println(pcf20.read(1)); 
  mb.task();
  mb.setCoil (SwitchIsts, pcf20.read(1));
  pcf23.write(3, mb.Coil (Relay1Coil));
  pcf23.write(7, mb.Coil (Relay2Coil));
  pcf23.write(6, mb.Coil (Relay3Coil));
  pcf27.write(7, mb.Coil (Lamp1Coil));
  pcf27.write(6, mb.Coil (Lamp2Coil));
  pcf27.write(5, mb.Coil (Lamp3Coil));
  pcf27.write(4, mb.Coil (Lamp4Coil));
  Serial.println(pcf20.read(1));
}


//  -- END OF FILE --

