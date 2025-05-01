#include <avr/sleep.h>
#include <avr/interrupt.h>

// Pin definitions for ATTIny 85 Port B
#define SDA_PIN        0  // PB0, Pin  (OLED SDA)
#define SCL_PIN        2  // PB2, Pin 7 (OLED SCL)
#define TEMP_PIN        A2 // PB4, ADC2 (LM60 Vout)
#define TEMP_POWER_PIN 1  // PB1, Pin 6 (LM60 VCC)
#define SWITCH_PIN     A3 // PB3, Pin 2 (Switch)

//I2C port alias
//PORTB starts out as 0x00, 00000000
#define SDA_PORT PORTB
#define SCL_PORT PORTB

// SSD1306 I2C address
#define OLED_ADDRESS 0x3C // SSD1306 I2C address

//Timing Constants
#define I2C_DELAY_US       15    // Microseconds for I2C timing
#define oledInit_DELAY    500   // Milliseconds for OLED initialization
#define OLED_CHAR_DELAY    10    // Milliseconds between OLED characters
#define TEMP_WARMUP_MS     20    // Milliseconds for LM60 stabilization
#define SWITCH_DEBOUNCE_MS 100   //Delay to capture switch push response
#define DISPLAY_TIMEOUT_MS 60000 // 60 seconds in milliseconds
#define LOOP_DELAY_MS      100   // Main loop delay for responsiveness

// Temperature Sensor Limits
#define TEMP_MAX_C 120  // Maximum temperature (°C)
#define TEMP_MIN_C -30  // Minimum temperature (°C)

//Temperature Array (for Average) size
#define TEMP_WINDOW_SIZE 20

// Font Table (5x7, 0-9, ., space, C, T, E, M, P, -, :, F, A, X, I, N)
const uint8_t font5x7[] PROGMEM = {
  0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
  0x00, 0x42, 0x7F, 0x40, 0x00, // 1
  0x42, 0x61, 0x51, 0x49, 0x46, // 2
  0x21, 0x41, 0x45, 0x4B, 0x31, // 3
  0x18, 0x14, 0x12, 0x7F, 0x10, // 4
  0x27, 0x45, 0x45, 0x45, 0x39, // 5
  0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
  0x01, 0x71, 0x09, 0x05, 0x03, // 7
  0x36, 0x49, 0x49, 0x49, 0x36, // 8
  0x06, 0x49, 0x49, 0x29, 0x1E, // 9
  0x00, 0x60, 0x60, 0x00, 0x00, // .
  0x00, 0x00, 0x00, 0x00, 0x00, // space
  0x3E, 0x41, 0x41, 0x41, 0x22, // C
  0x01, 0x01, 0x7F, 0x01, 0x01, // T 
  0x7F, 0X49, 0X49, 0X49, 0X41, // E
  0X7F, 0X02, 0X04, 0X02, 0X7F, // M
  0X7F, 0X09, 0X09, 0X06, 0X00, // P
  0x08, 0x08, 0x08, 0x08, 0x08, // -
  0x00, 0x66, 0x66, 0x00, 0x00, // :
  0x7F, 0x09, 0x09, 0x09, 0x01, // F
  0x7C, 0x12, 0x11, 0x12, 0x7C, // A
  0x63, 0x14, 0x08, 0x14, 0x63, // X
  0x41, 0x41, 0x7F, 0x41, 0x41, // I
  0x7F, 0x04, 0x08, 0x10, 0x7F  // N
};

// Global Variables
enum TempUnit {Celsius, Fahrenheit};
struct {
  TempUnit unit = Fahrenheit; // Current Temperature unit
  float maxTempC = TEMP_MIN_C;  // Maximum temperature (°C)
  float minTempC = TEMP_MAX_C; // Minimum temperature (°C)
  float maxTempF; // Maximum temperature (°F)
  float minTempF; // Minimum temperature (°F)
  float averageTempC; // Average temperature (°C)
  float averageTempF;  // Average temperature (°F)
  float arrayTemps[TEMP_WINDOW_SIZE]; // Array of temperatures (°C)
} tempState;

struct {
  bool active = false;  // Display on/off state
  unsigned long startTime = 0; // Time display was activated
} displayState;

struct {
  int current = LOW; // Current switch state
  int last = LOW; // Previous switch state
  unsigned long lastDebounceTime = 0; // Last debounce time
} switchState;

//I2C Functions
void i2cStart() {
  // |= is an bitwise or | and assignment =, 
  // << bit shifts 1 by SDA_PIN amount
  // SDA_PIN = 0, or 0000
  // Shift 1 (0001) left by SDA_PIN positions -> 0001 = 2
  // SDAPORT = PORTB
  // then a bitwise or happens, where if either bit is 1, the result is 1
  // if both are zero, then zero
  //then SDA_Port = the bitwise or result
  //result = SDA(PB)) is set high. Idle state for i2c.
  SDA_PORT |= (1 << SDA_PIN);
  SCL_PORT |= (1 << SCL_PIN);
  delayMicroseconds(5); // Ensure timing
  SDA_PORT &= ~(1 << SDA_PIN);
  SCL_PORT &= ~(1 << SCL_PIN);
  delayMicroseconds(I2C_DELAY_US); // Ensure timing
}

void i2cStop() {
  SDA_PORT &= ~(1 << SDA_PIN);
  SCL_PORT |= (1 << SCL_PIN);
  SDA_PORT |= (1 << SDA_PIN);
  delayMicroseconds(I2C_DELAY_US); // Ensure timing
}

void i2cWrite(uint8_t data) {
  for (uint8_t i = 8; i > 0; i--) {
    if (data & 0x80) SDA_PORT |= (1 << SDA_PIN);
    else SDA_PORT &= ~(1 << SDA_PIN);
    delayMicroseconds(I2C_DELAY_US); // Ensure timing
    SCL_PORT |= (1 << SCL_PIN);
    delayMicroseconds(I2C_DELAY_US); // Ensure timing
    SCL_PORT &= ~(1 << SCL_PIN);
    data <<= 1;
  }
  SDA_PORT |= (1 << SDA_PIN); // Release SDA for ACK
  delayMicroseconds(I2C_DELAY_US); // Ensure timing
  SCL_PORT |= (1 << SCL_PIN);
  delayMicroseconds(I2C_DELAY_US); // Ensure timing
  SCL_PORT &= ~(1 << SCL_PIN);
}

void oledCommand(uint8_t cmd) {
  i2cStart();
  i2cWrite(OLED_ADDRESS << 1);
  i2cWrite(0x00); // Command byte
  i2cWrite(cmd);
  i2cStop();
}

void oledData(uint8_t data) {
  i2cStart();
  i2cWrite(OLED_ADDRESS << 1);
  i2cWrite(0x40); // Data byte
  i2cWrite(data);
  i2cStop();
}

void oledInit() {
  DDRB |= (1 << SDA_PIN) | (1 << SCL_PIN); // Set SDA and SCL as outputs
  PORTB |= (1 << SDA_PIN) | (1 << SCL_PIN); // Pull high
  delay(100); // Wait for OLED to power up
  oledCommand(0xAE); // Display off
  oledCommand(0xD5); oledCommand(0x80); // Clock div
  oledCommand(0xA8); oledCommand(0x3F); // Multiplex
  oledCommand(0xD3); oledCommand(0x00); // Offset
  oledCommand(0x40); // Start line
  oledCommand(0x8D); oledCommand(0x14); // Charge pump
  oledCommand(0x20); oledCommand(0x00); // Memory mode
  oledCommand(0xA1); // Seg remap
  oledCommand(0xC8); // COM scan dir
  oledCommand(0xDA); oledCommand(0x12); // COM pins
  oledCommand(0x81); oledCommand(0xCF); // Contrast
  oledCommand(0xD9); oledCommand(0xF1); // Precharge
  oledCommand(0xDB); oledCommand(0x40); // VCOMH
  oledCommand(0xA4); // Entire display on
  oledCommand(0xA6); // Normal display
  oledCommand(0xAF); // Display on
  delay(oledInit_DELAY);// Delay after initialization
}

void oledSetCursor(uint8_t x, uint8_t page) {
  oledCommand(0xB0 | page); // Set page (0-7 for 128x64)
  oledCommand(x & 0x0F);    // Lower column
  oledCommand(0x10 | (x >> 4)); // Upper column
}

void oledClear() {
  for (uint8_t p = 0; p < 8; p++) {
    oledSetCursor(0, p);
    for (uint8_t i = 0; i < 128; i++) oledData(0x00);
  }
}

void oledPrintChar(char c) {
  uint8_t index;
  if (c >= '0' && c <= '9') index = (c - '0') * 5;      // 0-9
  else if (c == '.') index = 10 * 5;                    // .
  else if (c == ' ') index = 11 * 5;                    // space
  else if (c == 'C') index = 12 * 5;                    // C
  else if (c == 'T') index = 13 * 5;                    // T
  else if (c == 'E') index = 14 * 5;                    // E
  else if (c == 'M') index = 15 * 5;                    // M
  else if (c == 'P') index = 16 * 5;                    // P
  else if (c == '-') index = 17 * 5;                    // -
  else if (c == ':') index = 18 * 5;                    // :
  else if (c == 'F') index = 19 * 5;                    // F
  else if (c == 'A') index = 20 * 5;                    // A
  else if (c == 'X') index = 21 * 5;                    // X
  else if (c == 'I') index = 22 * 5;                    // I
  else if (c == 'N') index = 23 * 5;                    // N
  else return;

  for (uint8_t i = 0; i < 5; i++) {
    oledData(pgm_read_byte(&font5x7[index + i]));
  }
  oledData(0x00); // Space between characters
}

void oledPrintString(const char* str) {
  while (*str) {
    oledPrintChar(*str++);
    delay(OLED_CHAR_DELAY);
  }
}

void oledDrawBar(uint8_t x_start, uint8_t page, uint8_t length) {
  oledSetCursor(x_start, page); // Set starting position
  for (uint8_t i = 0; i < length; i++) {
    oledData(0xFF); // Solid 8-pixel-tall bar segment
  }
  // Fill the rest of the row with empty space (optional)
  for (uint8_t i = length; i < 128 - x_start; i++) {
    oledData(0x00);
  }
}

void oledDrawHorizontalDivider(uint8_t page, uint8_t row) {
  oledSetCursor(0, page); // Set to start of the specified page
  uint8_t pattern = 1 << row; // Bitwise shift to set the specific row (0-7) within the page
  for (uint8_t i = 0; i < 128; i++) {
    oledData(pattern); // Draw a 1-pixel-tall line across all 128 columns
  }
}
//TODO: oledDrawVerticalDivider not used
void oledDrawVerticalDivider(uint8_t column) {
  for (uint8_t page = 0; page < 8; page++) {
    oledSetCursor(column, page); // Set to the specified column on each page
    oledData(0xFF); // Turn on all 8 pixels in this column for this page
  }
}

// Temperature Sensor Functions
void powerTempSensor(bool enable) {
  if (enable) {
    PORTB |= (1 << TEMP_POWER_PIN); // Power on LM60
  } else {
    PORTB &= ~(1 << TEMP_POWER_PIN); // Power off LM60
  }
}

float readTemperatureC(uint8_t pin) {
  powerTempSensor(true);
  delay(TEMP_WARMUP_MS);
  int analogValue = analogRead(pin);
  float voltage = (analogValue * 5.0) / 1023;
  float tempC = (voltage * 1000 - 424) / 6.25; // LM60 formula
  powerTempSensor(false);
  return tempC;
}

float celsiusToFahrenheit(float tempC) {
  return (tempC * 1.8) + 32.0; // F = (C * 9/50) +32
}

void tempToString(float temp, char* str, TempUnit unit) {
  int tempInt = 0;
  str[0] = (temp < 0) ? '-' : ' ';
  if (temp < 0) temp = -temp;
  tempInt = temp * 10;
  str[1] = (tempInt / 100) ? (tempInt / 100) + '0' : ' ';
  str[2] = ((tempInt / 10) % 10) + '0';
  str[3] = '.';
  str[4] = (tempInt % 10) + '0';
  str[5] = (unit == Celsius) ? 'C' : 'F';
  str[6] = '\0';
}

// Low-Power Functions
void enterLowPowerMode() {
  powerTempSensor(false);
  oledCommand(0xAE); // Display off
  ADCSRA &= ~(1 << ADEN); // Disable ADC
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_mode();
  sleep_disable();
  ADCSRA |= (1 << ADEN); // Re-enable ADC
}

void wakeFromLowPowerMode() {
  oledCommand(0xAF); // Display on
  oledClear();
  oledDrawHorizontalDivider(0, 2);
  oledSetCursor(0, 1);
  oledPrintString("TEMP: ");
  oledDrawHorizontalDivider(2, 1);
  oledSetCursor(0, 3);
  oledPrintString("MAXT: ");
  oledSetCursor(0, 4);
  oledPrintString("MINT: ");
  oledDrawHorizontalDivider(7, 2);
}

// Interrupt Handler (Service Routine)
volatile bool wakeUp = false;
ISR(PCINT0_vect) {
  wakeUp = true;
}

// Hardware Initialization
void initPorts() {
  // Step 1: Set data direction (DDRB)
  // 1 = output, 0 = input
  // PB0 (SDA) and PB2 (SCL) as outputs, PB4 (temp sensor) as input
  DDRB = (1 << SDA_PIN) | (1 << SCL_PIN) | (1 << TEMP_POWER_PIN); // Outputs: PB0, PB2, PB1
  // Binary: 00000101 (PB2 and PB0 are outputs)

  // Step 2: Set initial states (PORTB)
  // For outputs: 1 = high, 0 = low
  // For inputs: 1 = pull-up enabled, 0 = no pull-up
  // SDA and SCL high (I2C idle), no pull-up on SWITCH_PIN (PB3) unless desired
  PORTB = (1 << SDA_PIN) | (1 << SCL_PIN); // High: PB0, PB2 (I2C idle)
  // Binary: 00000101 (PB2 and PB0 high, others low)
  // Optional: Enable pull-up on SWITCH_PIN (PB3) if your switch is active-low
  // PORTB |= (1 << SWITCH_PIN); // Uncomment this if you need a pull-up
}

void initInterrupts() {
  GIMSK |= (1 << PCIE); // Enable pin change interrupts
  PCMSK |= (1 << PCINT3); // Enable PCINT3 (PB3)
  sei(); // Enable global interrupts
}

// Switch Handling
bool handleSwitch() {
  static int lastSwitchVal = LOW;
  // Read PB3 (SWITCH_PIN) directly from PINB
  int switchVal = digitalRead(SWITCH_PIN); // HIGH if PB3 is 1, LOW if 0
  bool switchPressed = false;

  if (switchVal != lastSwitchVal) {
    switchState.lastDebounceTime = millis();
  }

  if ((millis() - switchState.lastDebounceTime) > SWITCH_DEBOUNCE_MS) {
    if (switchVal != switchState.current) {
      switchState.current = switchVal;
      if (switchState.current == HIGH) {
        switchPressed = true;
      }
    }
  }
  lastSwitchVal = switchVal;
  return switchPressed;
}

void updateTemperatureAverage(float tempC){
  float sumForAverageTempC = 0.0;
  for (int i = (TEMP_WINDOW_SIZE - 1); i >= 0; i--){
    if (i == 0){
      tempState.arrayTemps[i] = tempC;
    }
    else{
      tempState.arrayTemps[i] = tempState.arrayTemps[i-1];
    }
    sumForAverageTempC = sumForAverageTempC + tempState.arrayTemps[i];
  }
  tempState.averageTempC = sumForAverageTempC / TEMP_WINDOW_SIZE;
}

// Temperature and Display Update
void updateTemperatureAndDisplay() {
  float tempReadingC = readTemperatureC(TEMP_PIN);
  updateTemperatureAverage(tempReadingC);
  float tempC = tempState.averageTempC;
  float tempF = celsiusToFahrenheit(tempC);

  if (tempC > tempState.maxTempC) {
    tempState.maxTempC = tempC;
    tempState.maxTempF = tempF;
  }
  if (tempC < tempState.minTempC) {
    tempState.minTempC = tempC;
    tempState.minTempF = tempF;
  }

  char tempStr[7], maxTempStr[7], minTempStr[7];
  tempToString(tempC, tempStr, Celsius);
  tempToString(tempState.maxTempC, maxTempStr, Celsius);
  tempToString(tempState.minTempC, minTempStr, Celsius);

  if (tempState.unit == Fahrenheit) {
    tempToString(tempF, tempStr, Fahrenheit);
    tempToString(tempState.maxTempF, maxTempStr, Fahrenheit);
    tempToString(tempState.minTempF, minTempStr, Fahrenheit);
  }

  oledSetCursor(36, 1);
  oledPrintString(tempStr);
  oledSetCursor(36, 3);
  oledPrintString(maxTempStr);
  oledSetCursor(36, 4);
  oledPrintString(minTempStr);
}

void setup() {
  tempState.maxTempF = celsiusToFahrenheit(tempState.maxTempC);
  tempState.minTempF = celsiusToFahrenheit(tempState.minTempC);
  tempState.averageTempC = readTemperatureC(TEMP_PIN);
  tempState.averageTempF = celsiusToFahrenheit(tempState.averageTempC);

  for (int i = 0;i < TEMP_WINDOW_SIZE; i++){
    tempState.arrayTemps[i] =  tempState.averageTempC;
  }

  initPorts();
  analogReference(DEFAULT);
  oledInit();
  oledClear();
  oledCommand(0xAE); // Start with display off
  initInterrupts();
}

void loop() {
  if (wakeUp && !displayState.active) {
    wakeUp = false;
    wakeFromLowPowerMode();
    displayState.active = true;
    displayState.startTime = millis();
  }

  if (handleSwitch()) {
    if (!displayState.active) {
      wakeFromLowPowerMode();
      displayState.active = true;
      displayState.startTime = millis();
    }
    tempState.unit = (tempState.unit == Celsius) ? Fahrenheit : Celsius;
  }

  if (displayState.active) {
    updateTemperatureAndDisplay();
    if (millis() - displayState.startTime >= DISPLAY_TIMEOUT_MS) {
      enterLowPowerMode();
      displayState.active = false;
    }
  }

  delay(LOOP_DELAY_MS);
}