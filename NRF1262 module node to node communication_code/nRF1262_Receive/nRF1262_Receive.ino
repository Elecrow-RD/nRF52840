// include the library
#include <RadioLib.h>

SX1262 radio = new Module(44, 38, 42, 43);

// flag to indicate that a packet was received
volatile bool receivedFlag = false;

// this function is called when a complete packet
// is received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
#if defined(nRF1262) || defined(nRF1268)
  ICACHE_RAM_ATTR
#endif

void setup() {
  
  pinMode(41, OUTPUT);
  digitalWrite(41, LOW);

  Serial.begin(115200);
  while (!Serial); // Wait for serial to be initialised
  delay(5000);  // Give time to switch to the serial monitor
  // initialize LR1262 with default settings
  Serial.print(F("[LR1262] Initializing ... "));
  SPI.setPins(47, 45, 46);
  SPI.begin();
  int state = radio.begin(868.0, 125.0, 7, 7,RADIOLIB_SX126X_SYNC_WORD_PUBLIC   , 22, 8, 3.3, false);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true) { delay(10); }
  }

  radio.setRxBoostedGainMode(true);

  // set the function that will be called
  // when new packet is received
  radio.setPacketReceivedAction(setFlag);

  // start listening for LoRa packets
  Serial.print(F("[SX1262/SX1268] Starting to listen ... "));
  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true) { delay(10); }
  }
}

void setFlag(void) {
  // we got a packet, set the flag
  receivedFlag = true;
}

void loop() {
  if(receivedFlag) {
    receivedFlag = false;
    String str;
    int state = radio.readData(str);

    if (state == RADIOLIB_ERR_NONE) {
      // Parse and format output
      Serial.println("\n=== Sensor Data ===");
      Serial.println("Raw Data: " + str);
      
      // Parse key-value pairs
      parseSensorData(str);
      
      // Signal quality
      Serial.print("RSSI: ");
      Serial.print(radio.getRSSI());
      Serial.println(" dBm");
      Serial.print("SNR: ");
      Serial.print(radio.getSNR());
      Serial.println(" dB");
    }
  }
}

// Function to parse sensor data
void parseSensorData(String rawData) {
  // Example: Temp:25.5,Hum:60.0,X:0.12,Y:-0.03,Z:9.81
  int start = 0;
  while (start < rawData.length()) {
    int end = rawData.indexOf(',', start);
    if (end == -1) end = rawData.length();
    
    String pair = rawData.substring(start, end);
    int colon = pair.indexOf(':');
    if (colon != -1) {
      String key = pair.substring(0, colon);
      String value = pair.substring(colon + 1);
      
      Serial.print(key);
      Serial.print(": ");
      Serial.print(value);
      if (key == "Temp") Serial.print(" °C");
      else if (key == "Hum") Serial.print(" %");
      else if (key.startsWith("X") || key.startsWith("Y") || key.startsWith("Z")) Serial.print(" m/s²");
      Serial.println();
    }
    start = end + 1;
  }
}