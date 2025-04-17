#include <Arduino.h>        // Arduino core library
#include <U8g2lib.h>        // OLED display library
#include <Wire.h>           // I2C communication library
#include <DHT20.h>         // DHT20 temperature and humidity sensor library
#include <Adafruit_TinyUSB.h>  // for Serial
#include <IRremote.h>      // Infrared receiver library
#include <RadioLib.h>      // Wireless communication library

// Display Setup
U8G2_SSD1306_128X64_NONAME_1_SW_I2C u8g2(U8G2_R0, /* clock=*/ 7, /* data=*/ 6, /* reset=*/ U8X8_PIN_NONE); // OLED display (128x64 pixels), using software I2C interface, clock pin 7, data pin 6

DHT20 dht20;

// Sensor variables
float temperature = 0;
float humidity = 0;

// LSM6DS3TR register addresses
// Defines the register addresses and sensitivity constants related to the LSM6DS3TR accelerometer for subsequent register read and write operations
#define LSM6DS3TR_ADDR 0x6B // Accelerometer I2C address
#define CTRL1_XL 0x10       // Accelerometer control register address
#define CTRL2_G  0x11       // Gyroscope control register address
#define OUTX_L_XL 0x28      // X-axis acceleration data address
#define ACCEL_SENSITIVITY 0.000122 // Accelerometer sensitivity (4g range)

// Write to a register
// This function is used to write an 8-bit value to the specified register of the LSM6DS3TR accelerometer.
// Communicates via the I2C bus. First, it starts the transmission, specifies the device address, then writes the register address and the value to be set, and finally ends the transmission.
void writeRegister(uint8_t reg, uint8_t value) {
  // 1. Start the I2C transmission and specify the accelerometer device address (0x6B)
  Wire.beginTransmission(LSM6DS3TR_ADDR);
  
  // 2. Write the address of the register to be operated on
  Wire.write(reg);
  
  // 3. Write the value to be set
  Wire.write(value);
  
  // 4. End the transmission (actually perform the write operation) - Actually perform the I2C write operation and release the I2C bus
  Wire.endTransmission();
}

// Read from a register
/*
This method is called a "repeated start condition" and is common in I2C read operations.
First, set the read pointer position, then read the data. This is a typical I2C read process.
*/
void readRegister(uint8_t reg, uint8_t *data, uint8_t length) {
  // 1. Start the I2C transmission and specify the accelerometer device address
  Wire.beginTransmission(LSM6DS3TR_ADDR);
  
  // 2. Write the address of the register to start reading from
  Wire.write(reg);
  
  // 3. End the transmission but do not release the bus (false parameter): The false parameter means to send a stop signal but do not release the bus
  Wire.endTransmission(false);
  
  // 4. Request to read the specified length of data from the device
  Wire.requestFrom(LSM6DS3TR_ADDR, length);
  
  // 5. Read the data byte by byte
  for (int i = 0; i < length; i++) {
    data[i] = Wire.read();
  }
}

// New: Define the sensor data structure
struct SensorData {
  float temperature;
  float humidity;
  float accelX;
  float accelY;
  float accelZ;
};

// Constants for button and LED control
// //This is the pin of the external device
const int buttonPin = 12;  // Button pin
const int ledPin = 26;     // LED pin
volatile bool ledState = false; // LED state flag

// IR Receiver Setup
const uint8_t IR_RECEIVER_PIN = 24;    // Infrared receiver pin

// Button and LED control setup (for debounce and state toggle)
// This is the button and LED on the board
int Button = 32;  // Button connection pin
int LED = 41;     // LED connection pin
int state = 0;    // LED state 0: OFF, 1: ON
unsigned long lastDebounceTime = 0;  // Time of the last button press
unsigned long debounceDelay = 50;    // Debounce delay (50 milliseconds)
unsigned long lastButtonPressTime = 0;
unsigned long debounceDelay2 = 200;  // 200ms debounce delay

// LoRa Setup
// Radio Setup (SX1262)
// Wireless module pins    
SX1262 radio = new Module(44, 38, 42, 43); // 44：LR_NSS   38: LR_DIO1    42:LR_NRESET    43 :LR_BUSY
volatile bool transmittedFlag = false;     // Transmission completion flag
int transmissionState = RADIOLIB_ERR_NONE; // Transmission state


// Radio transmission flag function
void setFlag(void) {
  // Automatically called when the wireless module finishes sending a packet.
  // The main loop detects that transmittedFlag is true → Processes the sending result.
  transmittedFlag = true;  // Set the transmission completion flag
}

void setup() {
  // Initialize the display
  u8g2.begin(); // Use software I2C communication (pins 7-SCL, 6-SDA), set the display resolution to 128x64 pixels

  Serial1.setPins(28, 29); // Specify pins 28 and 29 for the Serial1 communication interface. Here, it is connected to the GPS positioning module
  // Initialize serial communication
  Serial.begin(9600);    // Communicate with the computer
  Serial1.begin(9600); // Initialize Serial1 for communication    Communicate with the GPS module

  // IR_RECEIVER_PIN is the infrared receiver pin
  IrReceiver.begin(IR_RECEIVER_PIN);

  // Initialize DHT20 sensor
  Wire.setPins(13, 14);  // Set the I2C pins SDA=13, SCL=14
  Wire.begin();          // Initialize the I2C bus
  while (dht20.begin()) {  // Try to initialize the DHT20
    Serial.println("Initialize DHT20 sensor failed");
    delay(1000);
  }

  // Initialize button and LED pins
  // This group is for the external button and bulb
  pinMode(buttonPin, INPUT);      // Set the button pin as an input
  pinMode(ledPin, OUTPUT);        // Set the LED pin as an output
  // Interrupt setup: Connect the button pin (12) to an interrupt, specify the interrupt service function as toggleLED, and the trigger condition is RISING (rising edge, i.e., when the button is pressed).
  attachInterrupt(digitalPinToInterrupt(buttonPin), toggleLED, RISING); 

  // Initialize button and LED pins
  // This group is for the button and LED on the nRF1262 board
  pinMode(Button, INPUT);
  pinMode(LED, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(Button), buttonISR, FALLING);


  // initialize LR1262 with default settings
  Serial.print(F("[LR1262] Initializing ... "));
  // Initialize LoRa
  // Set the SPI pins
  SPI.setPins(47, 45, 46);  // SCK=47, MISO=45, MOSI=46
  SPI.begin();
  // Initialize the wireless module
  int state = radio.begin(
    868.0,       // Frequency: 868MHz
    125.0,       // Bandwidth: 125kHz
    7,           // Spreading factor: 7
    7,           // Coding rate: 4/7
    RADIOLIB_SX126X_SYNC_WORD_PUBLIC,  // Sync word
    22,          // Output power: 22dBm
    8,           // Preamble length: 8 symbols
    3.3,         // Use TCXO
    false        // Do not use an external RF switch
  );
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("LoRa Initialized successfully!"));
  } else {
    Serial.print(F("LoRa initialization failed, code "));
    Serial.println(state);
    while (true) { delay(10); }
  }

  // Other configurations
  radio.setCurrentLimit(140.0);      // Set the current limit to 140mA
  radio.setPacketSentAction(setFlag); // Set the callback function to be called after the LoRa module finishes sending a packet to setFlag.

  Serial.println("IR Receiver and LoRa Setup complete.");

  transmissionState = radio.startTransmit("Hello World!"); // Start sending test data

}

void loop() {
  // // Get temperature and humidity from DHT20 sensor
  // 1. Collect sensor data
  SensorData data;
  data.temperature = dht20.getTemperature();
  data.humidity = dht20.getHumidity() * 100; // Convert to percentage
  
  // Display sensor readings on the screen
  u8g2.firstPage(); // Start a new page
  do {
    u8g2.setFont(u8g2_font_6x13_tf); // Set the font
    u8g2.setCursor(0, 30); // Set the text position
    u8g2.print("Temp: "); // Output text and data
    u8g2.print(data.temperature);
    u8g2.print("C");

    u8g2.setCursor(0, 50);
    u8g2.print("Humidity: ");
    u8g2.print(data.humidity);
    u8g2.print(" %RH");
  } while (u8g2.nextPage()); // Refresh the display page until all pages are displayed.

  // Output temperature and humidity to the Serial monitor
  Serial.print("Temperature: ");
  Serial.print(data.temperature);
  Serial.print("C");
  Serial.print("  Humidity: ");
  Serial.print(data.humidity);
  Serial.println(" %RH");

  // Read accelerometer data 
  uint8_t accelData[6];
  writeRegister(CTRL1_XL, 0x40);
  readRegister(OUTX_L_XL, accelData, 6);
  data.accelX = (int16_t)(accelData[0] | (accelData[1] << 8)) * ACCEL_SENSITIVITY * 9.80;
  data.accelY = (int16_t)(accelData[2] | (accelData[3] << 8)) * ACCEL_SENSITIVITY * 9.80;
  data.accelZ = (int16_t)(accelData[4] | (accelData[5] << 8)) * ACCEL_SENSITIVITY * 9.80;

  // 2. Build the sending string
  String payload = 
    "Temp:" + String(data.temperature, 1) + 
    ",Hum:" + String(data.humidity, 1) + 
    ",X:" + String(data.accelX, 2) +
    ",Y:" + String(data.accelY, 2) + 
    ",Z:" + String(data.accelZ, 2);
  
  // Handle Serial1 communication
  if (Serial1.available()) {
    while (Serial1.available()) {
      char incomingByte = Serial1.read();
      Serial.print(incomingByte);
    }
  }
  if (Serial.available()) {
    Serial1.write(Serial.read());
  }

  // Handle infrared signals
  if (IrReceiver.decode()) {  // Check if a valid infrared signal is received. If so, return true.
    handleIRCommand(); // Call this function to process the received infrared signal, such as printing the signal's protocol, command, and address.
    IrReceiver.resume(); // Resume() prepares to receive the next signal
  }

  // LoRa transmission logic
  // Check if the LoRa module has completed a data transmission.
  // transmittedFlag is set in the setFlag() callback function, which is triggered when the LoRa module finishes sending a packet.
  if (transmittedFlag) { // Check the transmission completion flag
    transmittedFlag = false; // If the transmission is completed, reset transmittedFlag to false.

    if (transmissionState == RADIOLIB_ERR_NONE) { // Report the transmission status (success/failure)
      Serial.println(F("transmission finished!"));
    } else {
      Serial.print(F("LoRa transmission failed, code "));
      Serial.println(transmissionState);
    }

    // 3. Send the data
    radio.finishTransmit();
    transmissionState = radio.startTransmit(payload);
    Serial.println("Sent: " + payload); // Debug output

  }
  delay(2000); // Change the sending interval to 2 seconds
}

// This function is an interrupt service function for handling button press events.
// ----This is the button on the board----
// When the button connected to the specified pin is pressed (triggering a falling edge interrupt), this function is called to toggle the LED state and handle the button debounce issue.
void buttonISR() {
  unsigned long currentMillis = millis();
  // Check if the difference between the current time and the time of the last button press is greater than the debounce delay debounceDelay (here, 50 milliseconds).
  // If the difference is greater than the debounce delay, it means the button has been pressed stably and is not a false trigger caused by jitter.
  if (currentMillis - lastDebounceTime > debounceDelay) {
    state = 1 - state; // Toggle the LED state flag variable state. If state was originally 0 (LED off), it becomes 1 (LED on), and vice versa.
    if(state == 1) {
      digitalWrite(LED, HIGH);
    } else {
      digitalWrite(LED, LOW);
    }
    lastDebounceTime = currentMillis;
  }
}

// ----This is the interrupt for the external button press----
void toggleLED() {
  unsigned long currentMillis2 = millis();
  if (currentMillis2 - lastButtonPressTime > debounceDelay2) {
    ledState = !ledState;
    digitalWrite(ledPin, ledState ? HIGH : LOW);  // Set the LED state
    lastButtonPressTime = currentMillis2;  // Update the last press time
  }
}

void handleIRCommand() {
  // Print raw IR data
  // 1. Print the title of the raw IR data
  // Print IR data only when a signal is detected
  Serial.println("\n===== IR Command Detected =====");

  // 2. Print the protocol type
  Serial.print("Protocol: ");
  Serial.println(getProtocolString(IrReceiver.decodedIRData.protocol));

  // 3. Print the command value (in hexadecimal)
  Serial.print("Hex Command: 0x");
  Serial.println(IrReceiver.decodedIRData.command, HEX);

  // 4. Print the address value (in hexadecimal)
  Serial.print("Address: 0x");
  Serial.println(IrReceiver.decodedIRData.address, HEX);

  // Print raw IR data for debugging
  // 5. Print the raw waveform data
  Serial.println("Raw IR Data:");
  IrReceiver.printIRResultRawFormatted(&Serial, true);

}