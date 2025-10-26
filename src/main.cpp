#include <Arduino.h>
#include <VoltageDividerSensor.h>
#include <CurrentSensor.h>
#include <PIO_DShot.h>

// Current Sensor (ACS72981KLRATR-150U3) parameters
constexpr unsigned int CURRENT_SENSOR_PIN = A0;
constexpr float CURRENT_SENSOR_SCALE = 1.0f;
constexpr float CURRENT_SENSOR_OFFSET = 0.0f;
constexpr unsigned int CURRENT_SENSOR_AVG_WINDOW = 10;

// Voltage Divider parameters
constexpr unsigned int VOLTAGE_DIVIDER_PIN = A1;
constexpr unsigned int VOLTAGE_DIVIDER_R1 = 10000; // 10k ohm
constexpr unsigned int VOLTAGE_DIVIDER_R2 = 10000; // 10k ohm
constexpr float VOLTAGE_DIVIDER_SCALE = 1.0f;
constexpr float VOLTAGE_DIVIDER_OFFSET = 0.0f;
constexpr unsigned int VOLTAGE_DIVIDER_AVG_WINDOW = 10;

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
constexpr uint8_t ESC_PIN = 18; // Pin connected to the ESC signal wire
BidirDShotX1 *esc; // Pointer to BidirDShotX1 instance, dynamically allocated later

// Global variables to hold sensor and esc telemetry readings
float sensor_current = 0;
float sensor_voltage = 0;
uint16_t throttle = 0;
uint32_t rpm = 0;
uint32_t temp = 0;
float esc_voltage = 0;
uint32_t esc_current = 0;
uint32_t lastStatus = 0;
uint32_t stress = 0;

void setup()
{
  esc = new BidirDShotX1(ESC_PIN, 600); // Default use-case is to initialize ESC on specified pin with DShot600 speed
}

void loop()
{
  sensor_current = inputCurrent.read(true); // Read current with moving average
  sensor_voltage = inputVoltage.read(true); // Read voltage with moving average

  Serial.print("Current (A): ");
  Serial.print(sensor_current); // Print averaged current reading
  Serial.print("\tVoltage (V): ");
  Serial.println(sensor_voltage); // Print averaged voltage reading
}
