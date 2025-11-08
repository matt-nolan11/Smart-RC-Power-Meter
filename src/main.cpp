#include <Arduino.h>
#include <VoltageDividerSensor.h>
#include <CurrentSensor.h>
#include <ESC.h>
#include <BLEManager.h>

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

// BLE update rate (200 Hz = 5ms interval)
constexpr unsigned long BLE_UPDATE_INTERVAL_MS = 5;

// Serial output rate (10 Hz for debugging)
constexpr unsigned long SERIAL_UPDATE_INTERVAL_MS = 100;

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

// BLE Manager
BLEManager bleManager("RC Power Meter");

// Global variables to hold sensor readings
float sensor_current = 0;
float sensor_voltage = 0;
float current_throttle = 0.0f;  // Throttle as percentage
bool esc_running = false;

// Battery protection state
BLEManager::BatteryState battery_state = BLEManager::BatteryState::NORMAL;
float cutoff_voltage = 12.8f;  // Will be calculated from config
float warning_voltage = 13.6f; // Will be calculated from config

// Timing variables
unsigned long last_ble_update = 0;
unsigned long last_serial_update = 0;

void setup()
{
  Serial.begin(115200);
  delay(2000); // Wait for serial connection
  Serial.println("Smart RC Power Meter - Initializing...");

  // Configure sensors
  inputCurrent.useAverage(true);
  inputVoltage.useAverage(true);

  // Initialize BLE
  if (bleManager.begin())
  {
    Serial.println("BLE: Started successfully");
  }
  else
  {
    Serial.println("BLE: Failed to start");
  }

  // Set default ESC configuration
  esc.setMode(ESC::escMode::PWM);
  esc.setEscType(ESC::escType::UNIDIRECTIONAL);
  esc.stop(); // Ensure ESC is stopped at startup

  Serial.println("Initialization complete");
}

void loop()
{
  unsigned long current_time = millis();

  // Update BLE stack
  bleManager.update();

  // Check for new ESC configuration
  if (bleManager.hasNewConfig())
  {
    const ESCConfigPacket& config = bleManager.getESCConfig();

    // Update ESC settings
    esc.setMode(config.mode == 0 ? ESC::escMode::PWM : ESC::escMode::DSHOT);
    esc.setEscType(config.esc_type == 0 ? ESC::escType::UNIDIRECTIONAL : ESC::escType::BIDIRECTIONAL);
    esc.setMotorPoles(config.motor_poles);
    esc.setThrottleRange(config.throttle_min, config.throttle_max);
    esc.setRampRates(config.ramp_up_rate, config.ramp_down_rate, config.ramp_enabled == 1);

    // Calculate battery protection thresholds
    cutoff_voltage = config.battery_cells * (config.battery_cutoff_mv / 1000.0f);
    warning_voltage = config.battery_cells * ((config.battery_cutoff_mv + config.battery_warning_delta_mv) / 1000.0f);

    Serial.println("Applied new ESC configuration");
    Serial.print("Cutoff voltage: ");
    Serial.print(cutoff_voltage, 2);
    Serial.print("V, Warning voltage: ");
    Serial.print(warning_voltage, 2);
    Serial.println("V");
    Serial.print("Throttle range: ");
    Serial.print(config.throttle_min);
    Serial.print(" - ");
    Serial.print(config.throttle_max);
    Serial.println(" μs");
    Serial.print("Ramp: ");
    Serial.print(config.ramp_enabled ? "Enabled" : "Disabled");
    Serial.print(" (Up: ");
    Serial.print(config.ramp_up_rate);
    Serial.print(" μs/s, Down: ");
    Serial.print(config.ramp_down_rate);
    Serial.println(" μs/s)");

    bleManager.clearConfigFlag();
  }

  // Check for new ESC commands
  if (bleManager.hasNewCommand())
  {
    const ESCCommandPacket& command = bleManager.getESCCommand();

    if (command.command == 1) // START
    {
      esc_running = true;
      current_throttle = command.throttle;  // Throttle is now a percentage
      esc.setThrottle(current_throttle);
      Serial.print("ESC: Started at ");
      Serial.print(current_throttle);
      Serial.println("%");
    }
    else // STOP
    {
      esc_running = false;
      esc.stop();
      current_throttle = 0.0f;
      Serial.println("ESC: Stopped");
    }

    bleManager.clearCommandFlag();
  }

  // Read sensors at maximum rate
  sensor_current = inputCurrent.read();
  sensor_voltage = inputVoltage.read();

  // Update ESC ramp (if enabled, gradually changes throttle)
  esc.updateRamp();

  // Battery protection monitoring
  const ESCConfigPacket& config = bleManager.getESCConfig();
  if (config.battery_protection_enabled && esc_running)
  {
    BLEManager::BatteryState new_state = BLEManager::BatteryState::NORMAL;

    if (sensor_voltage < cutoff_voltage)
    {
      new_state = BLEManager::BatteryState::CUTOFF;
      if (battery_state != BLEManager::BatteryState::CUTOFF)
      {
        // Just entered cutoff state - stop ESC
        esc.stop();
        esc_running = false;
        Serial.println("Battery protection: Cutoff reached - ESC stopped");
      }
    }
    else if (sensor_voltage < warning_voltage)
    {
      new_state = BLEManager::BatteryState::WARNING;
      if (battery_state != BLEManager::BatteryState::WARNING)
      {
        Serial.println("Battery protection: Warning threshold reached");
      }
    }

    if (new_state != battery_state)
    {
      battery_state = new_state;
      bleManager.sendBatteryStatus(battery_state, sensor_voltage);
    }
  }

  // Send BLE data at 200 Hz (5ms interval)
  if (current_time - last_ble_update >= BLE_UPDATE_INTERVAL_MS)
  {
    last_ble_update = current_time;

    if (bleManager.isConnected())
    {
      if (config.mode == 0) // PWM mode
      {
        bleManager.sendPWMData(sensor_voltage, sensor_current, current_throttle);
      }
      else // DSHOT mode
      {
        ESC::telemData telemetry = esc.getTelemetry();
        bleManager.sendDSHOTData(
          sensor_voltage,
          sensor_current,
          current_throttle,
          telemetry.rpm,
          telemetry.voltage,
          telemetry.current,
          telemetry.temp,
          telemetry.lastStatus,
          telemetry.stress
        );
      }
    }
  }

  // Serial output at 10 Hz for debugging (100ms interval)
  if (current_time - last_serial_update >= SERIAL_UPDATE_INTERVAL_MS)
  {
    last_serial_update = current_time;

    Serial.print("V: ");
    Serial.print(sensor_voltage, 2);
    Serial.print("V  I: ");
    Serial.print(sensor_current, 2);
    Serial.print("A  P: ");
    Serial.print(sensor_voltage * sensor_current, 1);
    Serial.print("W  Throttle: ");
    Serial.print(current_throttle, 1);
    Serial.print("%  BLE: ");
    Serial.println(bleManager.isConnected() ? "Connected" : "Disconnected");
  }
}
