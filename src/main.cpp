#include <Arduino.h>
#include <VoltageDividerSensor.h>
#include <CurrentSensor.h>
#include <ESC.h>

// Current Sensor (ACS72981KLRATR-150U3) parameters
constexpr unsigned int CURRENT_SENSOR_PIN = A0;
constexpr float CURRENT_SENSOR_SCALE = 1.048;
constexpr float CURRENT_SENSOR_OFFSET = -0.665;
constexpr unsigned int CURRENT_SENSOR_AVG_WINDOW = 200;

// Voltage Divider parameters
constexpr unsigned int VOLTAGE_DIVIDER_PIN = A1;
constexpr unsigned int VOLTAGE_DIVIDER_R1 = 100000; // 100k ohm
constexpr unsigned int VOLTAGE_DIVIDER_R2 = 5600;   // 5.6k ohm
constexpr float VOLTAGE_DIVIDER_SCALE = 1.09;
constexpr float VOLTAGE_DIVIDER_OFFSET = -0.26;
constexpr unsigned int VOLTAGE_DIVIDER_AVG_WINDOW = 200;

CurrentSensor inputCurrent(CURRENT_SENSOR_PIN,
                           CURRENT_SENSOR_SCALE,
                           CURRENT_SENSOR_OFFSET,
                           CURRENT_SENSOR_AVG_WINDOW);

VoltageDividerSensor inputVoltage(VOLTAGE_DIVIDER_PIN,
                                  VOLTAGE_DIVIDER_R1,
                                  VOLTAGE_DIVIDER_R2,
                                  VOLTAGE_DIVIDER_SCALE,
                                  VOLTAGE_DIVIDER_OFFSET,
                                  VOLTAGE_DIVIDER_AVG_WINDOW);

// ESC parameters
constexpr uint8_t ESC_SIGNAL_PIN = 18; // Pin connected to the ESC signal wire
ESC esc(ESC_SIGNAL_PIN);

// Global variables to hold sensor readings
float sensor_current = 0;
float sensor_voltage = 0;

void setup()
{
  inputCurrent.useAverage(true);        // Enable moving average for current sensor
  inputVoltage.useAverage(true);        // Enable moving average for voltage sensor
}

void loop()
{
  sensor_current = inputCurrent.read();
  sensor_voltage = inputVoltage.read();

  Serial.print("Current (A): ");
  Serial.print(sensor_current, 1);
  Serial.print("    Voltage (V): ");
  Serial.println(sensor_voltage, 1);
}
