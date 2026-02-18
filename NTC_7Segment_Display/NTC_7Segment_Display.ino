#include <EEPROM.h>
#include <TM1637Display.h>
#include <math.h>

/* ------------ TM1637 PINS ------------ */
#define CLK 2
#define DIO 3

TM1637Display display(CLK, DIO);

/* ------------ NTC CONFIG ------------ */
#define NTC_PIN A3

#define SERIES_RESISTOR     10000.0
#define NOMINAL_RESISTANCE  10000.0
#define NOMINAL_TEMP        25.0
#define BETA                3950.0

/* ------------ EEPROM ------------ */
#define EEPROM_MAGIC   0
#define EEPROM_OFFSET  4
#define EEPROM_SCALE   8
#define MAGIC_KEY      123

float tempOffset = 0.0;
float tempScale  = 1.0;

/* ------------ FILTER ------------ */
#define FILTER_SIZE 20

float tempBuffer[FILTER_SIZE];
int bufIndex = 0;
bool bufferFilled = false;

float filteredTemp = 0;
bool filterInitialized = false;

/* ------------ READ NTC (FIXED) ------------ */
float readNTC() {

  long sum = 0;

  for (int i = 0; i < 10; i++) {
    sum += analogRead(NTC_PIN);
    delay(5);
  }

  float adc = sum / 10.0;

  if (adc < 1) adc = 1;
  if (adc > 1022) adc = 1022;

  // CORRECT FORMULA FOR YOUR WIRING
  float resistance = SERIES_RESISTOR * ((1023.0 / adc) - 1.0);

  float steinhart = resistance / NOMINAL_RESISTANCE;
  steinhart = log(steinhart);
  steinhart /= BETA;
  steinhart += 1.0 / (NOMINAL_TEMP + 273.15);
  steinhart = 1.0 / steinhart;
  steinhart -= 273.15;

  return steinhart;
}

/* ------------ CALIBRATED TEMP ------------ */
float getTemp() {

  float raw = readNTC();

  return (raw * tempScale) + tempOffset;
}

/* ------------ FILTER ------------ */
float filterTemp(float newValue) {

  if (!filterInitialized) {

    filteredTemp = newValue;

    for (int i = 0; i < FILTER_SIZE; i++) {
      tempBuffer[i] = newValue;
    }

    bufferFilled = true;
    filterInitialized = true;

    return filteredTemp;
  }

  tempBuffer[bufIndex] = newValue;
  bufIndex++;

  if (bufIndex >= FILTER_SIZE) {
    bufIndex = 0;
  }

  float sum = 0;

  for (int i = 0; i < FILTER_SIZE; i++) {
    sum += tempBuffer[i];
  }

  float avg = sum / FILTER_SIZE;

  float alpha = 0.08;

  filteredTemp = (alpha * avg) + (1 - alpha) * filteredTemp;

  return filteredTemp;
}

/* ------------ DISPLAY TT:DD ------------ */
void showTemp(float temp) {

  if (temp < 0) temp = 0;
  if (temp > 99.99) temp = 99.99;

  int value = round(temp * 100);

  int intPart = value / 100;
  int decPart = value % 100;

  int d1 = intPart / 10;
  int d2 = intPart % 10;

  int d3 = decPart / 10;
  int d4 = decPart % 10;

  uint8_t data[4];

  data[0] = display.encodeDigit(d1);
  data[1] = display.encodeDigit(d2) | 0x80; // Colon ON
  data[2] = display.encodeDigit(d3);
  data[3] = display.encodeDigit(d4);

  display.setSegments(data);
}

/* ------------ EEPROM ------------ */
void saveCal() {

  EEPROM.put(EEPROM_OFFSET, tempOffset);
  EEPROM.put(EEPROM_SCALE, tempScale);

  EEPROM.write(EEPROM_MAGIC, MAGIC_KEY);

  Serial.println("Calibration saved");
}

bool loadCal() {

  if (EEPROM.read(EEPROM_MAGIC) == MAGIC_KEY) {

    EEPROM.get(EEPROM_OFFSET, tempOffset);
    EEPROM.get(EEPROM_SCALE, tempScale);

    return true;
  }

  return false;
}

/* ------------ SERIAL INPUT ------------ */
float readFloat() {

  while (!Serial.available());
  return Serial.parseFloat();
}

String readString() {

  while (!Serial.available());
  return Serial.readStringUntil('\n');
}

/* ------------ CALIBRATION ------------ */
void calibrate() {

  Serial.println("\n--- CALIBRATION MODE ---");
  Serial.println("Enter actual temperature (C):");

  float realTemp = readFloat();

  delay(1000);

  float raw = readNTC();

  tempScale = realTemp / raw;
  tempOffset = realTemp - (raw * tempScale);

  float check = getTemp();

  Serial.print("Measured: ");
  Serial.println(check, 2);

  Serial.println("Is calibration OK? (yes/no)");

  String ans = readString();

  ans.trim();
  ans.toLowerCase();

  if (ans == "yes") {

    saveCal();
    filterInitialized = false;

    Serial.println("Calibration Complete");

  } else {

    Serial.println("Recalibrating...");
    calibrate();
  }
}

/* ------------ SETUP ------------ */
void setup() {

  Serial.begin(9600);

  display.setBrightness(7);
  display.clear();

  Serial.println("NTC Temperature System Started");

  if (!loadCal()) {

    Serial.println("No Calibration Found");
    calibrate();

  } else {

    Serial.println("Calibration Loaded");
  }

  // Initialize filter immediately
  float first = getTemp();
  filterTemp(first);
}

/* ------------ LOOP ------------ */
void loop() {

  float rawTemp = getTemp();

  float stableTemp = filterTemp(rawTemp);

  Serial.print("Temperature: ");
  Serial.print(stableTemp, 2);
  Serial.println(" C");

  showTemp(stableTemp);

  delay(500);
}
