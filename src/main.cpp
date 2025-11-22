#include <Arduino.h>
#include <VoltageDividerSensor.h>
#include <CurrentSensor.h>
#include <ESC.h>
#include <BLEManager.h>
#include <hardware/watchdog.h>  // For hardware reset via watchdog

// Disable serial output for maximum performance
#define ENABLE_SERIAL_DEBUG 1

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

// Serial output rate (1 Hz for debugging)
constexpr unsigned long SERIAL_UPDATE_INTERVAL_MS = 1000;

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
uint8_t current_esc_mode = 0;   // 0 = PWM, 1 = DSHOT

// Battery protection state
BLEManager::BatteryState battery_state = BLEManager::BatteryState::NORMAL;
float cutoff_voltage = 12.8f;  // Will be calculated from config
float warning_voltage = 13.6f; // Will be calculated from config
bool battery_state_sent = false; // Track if initial state has been sent
unsigned long last_battery_status_send = 0;  // Rate limit battery status updates

// Battery state debouncing (prevent transient dips from triggering state changes)
constexpr unsigned long BATTERY_STATE_DEBOUNCE_MS = 1000;  // Must stay below threshold for 1000ms
constexpr float BATTERY_HYSTERESIS_MV = 100.0f;  // 0.1V hysteresis for recovery (in millivolts, will be converted)
unsigned long battery_below_warning_time = 0;  // Time when voltage first dropped below warning
unsigned long battery_below_cutoff_time = 0;   // Time when voltage first dropped below cutoff
bool battery_below_warning = false;
bool battery_below_cutoff = false;

// Timing variables
unsigned long last_serial_update = 0;

void setup()
{
#if ENABLE_SERIAL_DEBUG
  Serial.begin(115200);
  delay(2000); // Wait for serial connection
  Serial.println("Smart RC Power Meter - Initializing...");
#endif

  // Configure sensors
  inputCurrent.useAverage(true);
  inputVoltage.useAverage(true);

  // Initialize BLE
  if (bleManager.begin())
  {
#if ENABLE_SERIAL_DEBUG
    Serial.println("BLE: Started successfully");
#endif
  }
  else
  {
#if ENABLE_SERIAL_DEBUG
    Serial.println("BLE: Failed to start");
#endif
  }

  // Set default ESC configuration
  // Note: ESC will not output any signals until connect() is called
  esc.setMode(ESC::escMode::PWM);
  esc.setEscType(ESC::escType::UNIDIRECTIONAL);

#if ENABLE_SERIAL_DEBUG
  Serial.println("Initialization complete");
  Serial.println("ESC: Disconnected - waiting for user to connect");
#endif
}

void loop()
{
  unsigned long current_time = millis();

  // Update BLE stack
  bleManager.update();

  // Handle BLE disconnect - stop ESC for safety
  static bool was_connected = false;
  static unsigned long last_ble_activity = millis();
  bool is_connected = bleManager.isConnected();
  
  // Reset activity timer on new connection
  if (!was_connected && is_connected)
  {
    last_ble_activity = millis();
#if ENABLE_SERIAL_DEBUG
    Serial.println("BLE: New connection - reset activity timer");
#endif
  }
  
  // Activity watchdog - detect stale connections that stop sending data
  // Heartbeat keeps connection alive (updates last_ble_activity every 2s)
  if (is_connected && (millis() - last_ble_activity > 4000))
  {
#if ENABLE_SERIAL_DEBUG
    Serial.println("BLE WATCHDOG: No activity for 4s - forcing disconnect for safety!");
#endif
    
    // Disconnect ESC immediately for safety
    if (esc.isConnected())
    {
      esc.disconnect();
      esc_running = false;
#if ENABLE_SERIAL_DEBUG
      Serial.println("BLE WATCHDOG: ESC disconnected and stopped");
#endif
    }
    
    // Force BLE disconnect
    bleManager.forceDisconnect();
    is_connected = false;
    
#if ENABLE_SERIAL_DEBUG
    Serial.println("BLE WATCHDOG: Triggering hardware reset to clear BTstack state");
    Serial.flush();
#endif
    
    delay(100);  // Brief delay for serial output
    
    // Trigger hardware reset via watchdog (1ms timeout)
    watchdog_enable(1, false);
    while(1);  // Wait for watchdog reset
  }
  
  if (was_connected && !is_connected)
  {
    // Connection lost - disconnect ESC immediately for safety
    // This stops the motor AND stops all signal output (no signal sent to ESC)
    if (esc.isConnected())
    {
      esc.disconnect();
      esc_running = false;
#if ENABLE_SERIAL_DEBUG
      Serial.println("BLE disconnected - ESC disconnected and stopped for safety");
#endif
    }
  }
  was_connected = is_connected;

  // Check for new ESC configuration
  if (bleManager.hasNewConfig())
  {
    last_ble_activity = millis(); // Update watchdog timer
    
    const ESCConfigPacket& config = bleManager.getESCConfig();

    // Store current mode for packet selection
    current_esc_mode = config.mode;

    // Update ESC settings
    esc.setMode(config.mode == 0 ? ESC::escMode::PWM : ESC::escMode::DSHOT);
    esc.setEscType(config.esc_type == 0 ? ESC::escType::UNIDIRECTIONAL : ESC::escType::BIDIRECTIONAL);
    esc.setMotorPoles(config.motor_poles);
    esc.setThrottleRange(config.throttle_min, config.throttle_max);
    esc.setRampRates(config.ramp_up_rate, config.ramp_down_rate, config.ramp_up_enabled == 1, config.ramp_down_enabled == 1);

    // Calculate battery protection thresholds
    cutoff_voltage = config.battery_cells * (config.battery_cutoff_mv / 1000.0f);
    warning_voltage = config.battery_cells * ((config.battery_cutoff_mv + config.battery_warning_delta_mv) / 1000.0f);
    
    // Reset battery state and debounce timers when config changes
    battery_state_sent = false;
    battery_below_warning = false;
    battery_below_cutoff = false;
    battery_below_warning_time = 0;
    battery_below_cutoff_time = 0;

#if ENABLE_SERIAL_DEBUG
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
    Serial.print("Ramp Up: ");
    Serial.print(config.ramp_up_enabled ? "Enabled" : "Disabled");
    Serial.print(" (");
    Serial.print(config.ramp_up_rate);
    Serial.print(" %/s), Ramp Down: ");
    Serial.print(config.ramp_down_enabled ? "Enabled" : "Disabled");
    Serial.print(" (");
    Serial.print(config.ramp_down_rate);
    Serial.println(" %/s)");
#endif

    bleManager.clearConfigFlag();
    
    // Note: Initial battery status will be sent on first battery check in main loop
    // The battery_state_sent flag was reset above when thresholds were recalculated
  }

  // Check for new ESC commands
  if (bleManager.hasNewCommand())
  {
    last_ble_activity = millis(); // Update watchdog timer (includes PING heartbeat)
    
    const ESCCommandPacket& command = bleManager.getESCCommand();
    const ESCConfigPacket& config = bleManager.getESCConfig();

    if (command.command == 2) // CONNECT
    {
      esc.connect();
#if ENABLE_SERIAL_DEBUG
      Serial.println("ESC: Connected");
#endif
    }
    else if (command.command == 3) // DISCONNECT
    {
      esc_running = false;
      esc.disconnect();
#if ENABLE_SERIAL_DEBUG
      Serial.println("ESC: Disconnected");
#endif
    }
    else if (command.command == 4) // PING (heartbeat from web app)
    {
      // Just acknowledge - no action needed, this keeps BLE watchdog alive
      // Don't log to avoid flooding serial output
    }
    else if (command.command == 1) // START or throttle update
    {
      // Reject START command if not connected to ESC
      if (!esc.isConnected())
      {
#if ENABLE_SERIAL_DEBUG
        Serial.println("ESC: Start rejected - ESC not connected");
#endif
        bleManager.clearCommandFlag();
        return;
      }
      
      // Reject START command if battery protection is enabled and in cutoff state
      if (!esc_running && config.battery_protection_enabled && battery_state == BLEManager::BatteryState::CUTOFF)
      {
#if ENABLE_SERIAL_DEBUG
        Serial.println("ESC: Start rejected - battery cutoff protection active");
#endif
        bleManager.clearCommandFlag();
        return;  // Skip this command
      }

      if (!esc_running) 
      {
        // Starting from stopped - reset actual throttle to 0 for clean ramp-up
        esc_running = true;
        esc.resetThrottle();
#if ENABLE_SERIAL_DEBUG
        Serial.print("ESC: Started at ");
        Serial.print(command.throttle);
        Serial.println("% (ramping from 0)");
#endif
      }
      else
      {
        // Already running - just update throttle (will ramp to new value)
#if ENABLE_SERIAL_DEBUG
        Serial.print("ESC: Throttle updated to ");
        Serial.print(command.throttle);
        Serial.println("%");
#endif
      }
      
      current_throttle = command.throttle;
      esc.setThrottle(current_throttle);
    }
    else // STOP
    {
      esc_running = false;
      esc.stop();
      current_throttle = 0.0f;
#if ENABLE_SERIAL_DEBUG
      Serial.println("ESC: Stopped");
#endif
    }

    bleManager.clearCommandFlag();
  }

  // Handle DSHOT special commands (beeps, direction, settings, etc.)
  if (bleManager.hasNewDSHOTCommand())
  {
    last_ble_activity = millis(); // Update watchdog timer
    
    uint8_t command = bleManager.getDSHOTCommand();
    
#if ENABLE_SERIAL_DEBUG
    Serial.print("DSHOT: Sending special command: ");
    Serial.println(command);
#endif
    
    // Send command 6 times as per DSHOT protocol
    if (esc.isConnected())
    {
      esc.sendDshotCommand((ESC::dshotCommand)command, 6);
      bleManager.sendDSHOTResponse(0, nullptr, 0); // Send ACK
    }
    else
    {
#if ENABLE_SERIAL_DEBUG
      Serial.println("DSHOT: Command rejected - ESC not connected");
#endif
    }
    
    bleManager.clearDSHOTCommandFlag();
  }

  // Read sensors at maximum rate
  sensor_current = inputCurrent.read();
  sensor_voltage = inputVoltage.read();

  // Update ESC ramp (if enabled, gradually changes throttle)
  esc.updateRamp();
  
  // CRITICAL: DSHOT requires continuous throttle packets (>500Hz recommended)
  // ESC will timeout and disarm if packets stop for ~10ms
  // Send current throttle (or 0 if stopped) on EVERY loop iteration
  if (esc.isConnected() && current_esc_mode == 1) // DSHOT mode
  {
    // Debug: Calculate actual loop rate
    static unsigned long last_loop_time = 0;
    static unsigned long loop_count = 0;
    static unsigned long rate_check_time = 0;
    
    loop_count++;
    unsigned long now = micros();
    
    // Print loop rate every second
    if (now - rate_check_time >= 1000000)
    {
      float actual_rate = loop_count / ((now - rate_check_time) / 1000000.0f);
      Serial.print("DSHOT Loop Rate: ");
      Serial.print(actual_rate, 0);
      Serial.print(" Hz (avg period: ");
      Serial.print(1000000.0f / actual_rate, 0);
      Serial.println(" us)");
      
      loop_count = 0;
      rate_check_time = now;
    }
    
    // Always send throttle to keep ESC alive (even if stopped)
    esc.sendThrottle();
    
    // Read telemetry every loop for bidirectional DSHOT
    esc.readTelemetry();
    
    // Add small delay to maintain proper DSHOT timing (~5kHz rate = 200us)
    delayMicroseconds(200);
  }

  // Battery protection monitoring
  const ESCConfigPacket& config = bleManager.getESCConfig();
  if (config.battery_protection_enabled)
  {
    BLEManager::BatteryState new_state = BLEManager::BatteryState::NORMAL;

#if ENABLE_SERIAL_DEBUG
    // Periodic debug output every 2 seconds
    static unsigned long last_battery_debug = 0;
    if (millis() - last_battery_debug > 2000)
    {
      last_battery_debug = millis();
      Serial.print("Battery: V=");
      Serial.print(sensor_voltage, 2);
      Serial.print("V  Cutoff=");
      Serial.print(cutoff_voltage, 2);
      Serial.print("V  Warning=");
      Serial.print(warning_voltage, 2);
      Serial.print("V  State=");
      if (battery_state == BLEManager::BatteryState::CUTOFF)
        Serial.println("CUTOFF");
      else if (battery_state == BLEManager::BatteryState::WARNING)
        Serial.println("WARNING");
      else
        Serial.println("NORMAL");
    }
#endif

    // Calculate hysteresis recovery thresholds (add 0.1V to prevent rapid state changes)
    float hysteresis_v = BATTERY_HYSTERESIS_MV / 1000.0f;
    float cutoff_recovery = cutoff_voltage + hysteresis_v;
    float warning_recovery = warning_voltage + hysteresis_v;
    
    // Check for voltage drops with debouncing
    if (sensor_voltage < cutoff_voltage)
    {
      if (!battery_below_cutoff)
      {
        // First time below cutoff - start timer
        battery_below_cutoff = true;
        battery_below_cutoff_time = millis();
        // Maintain current state during debounce period
        new_state = battery_state;
      }
      else if (millis() - battery_below_cutoff_time >= BATTERY_STATE_DEBOUNCE_MS)
      {
        // Sustained below cutoff for debounce period - trigger cutoff
        new_state = BLEManager::BatteryState::CUTOFF;
        if (battery_state != BLEManager::BatteryState::CUTOFF)
        {
          // Stop ESC if running
          if (esc_running)
          {
            esc.stop();
            esc_running = false;
#if ENABLE_SERIAL_DEBUG
            Serial.println("Battery protection: Cutoff reached (debounced) - ESC stopped");
#endif
          }
        }
      }
      else
      {
        // Still debouncing - maintain current state
        new_state = battery_state;
      }
    }
    else if (sensor_voltage < cutoff_recovery)
    {
      // Between cutoff and recovery threshold - maintain CUTOFF state
      // This prevents flickering when voltage hovers in hysteresis zone
      if (battery_state == BLEManager::BatteryState::CUTOFF)
      {
        new_state = BLEManager::BatteryState::CUTOFF;
      }
      else
      {
        // If not yet in CUTOFF state, maintain current state (don't change)
        new_state = battery_state;
      }
      battery_below_cutoff = false;  // Reset debounce timer
    }
    else
    {
      // Above cutoff recovery threshold
      battery_below_cutoff = false;
    }
    
    // Check warning threshold (if not already in cutoff)
    if (battery_state != BLEManager::BatteryState::CUTOFF && new_state != BLEManager::BatteryState::CUTOFF)
    {
      if (sensor_voltage < warning_voltage)
      {
        if (!battery_below_warning)
        {
          // First time below warning - start timer
          battery_below_warning = true;
          battery_below_warning_time = millis();
          // Maintain current state during debounce period
          new_state = battery_state;
        }
        else if (millis() - battery_below_warning_time >= BATTERY_STATE_DEBOUNCE_MS)
        {
          // Sustained below warning for debounce period
          new_state = BLEManager::BatteryState::WARNING;
          if (battery_state != BLEManager::BatteryState::WARNING)
          {
#if ENABLE_SERIAL_DEBUG
            Serial.println("Battery protection: Warning threshold reached (debounced)");
#endif
          }
        }
        else
        {
          // Still debouncing - maintain current state
          new_state = battery_state;
        }
      }
      else if (sensor_voltage < warning_recovery)
      {
        // Between warning and recovery threshold - maintain WARNING state
        // This prevents flickering when voltage hovers in hysteresis zone
        if (battery_state == BLEManager::BatteryState::WARNING)
        {
          new_state = BLEManager::BatteryState::WARNING;
        }
        else
        {
          // If not yet in WARNING state, maintain current state (don't change)
          new_state = battery_state;
        }
        battery_below_warning = false;  // Reset debounce timer
      }
      else
      {
        // Above warning recovery threshold - normal
        battery_below_warning = false;
        if (battery_state != BLEManager::BatteryState::NORMAL)
        {
#if ENABLE_SERIAL_DEBUG
          Serial.println("Battery protection: Voltage recovered to normal");
#endif
        }
      }
    }

    // Update state immediately, but rate-limit BLE notifications
    if (new_state != battery_state || !battery_state_sent)
    {
      battery_state = new_state;
      
      // Send notification if enough time has passed since last send (rate limiting)
      if (!battery_state_sent || (millis() - last_battery_status_send >= 200))
      {
        battery_state_sent = true;
        last_battery_status_send = millis();
        bleManager.sendBatteryStatus(battery_state, sensor_voltage);
#if ENABLE_SERIAL_DEBUG
        Serial.print("Battery status SENT: ");
        if (battery_state == BLEManager::BatteryState::CUTOFF)
          Serial.print("CUTOFF");
        else if (battery_state == BLEManager::BatteryState::WARNING)
          Serial.print("WARNING");
        else
          Serial.print("NORMAL");
        Serial.print(" at ");
        Serial.print(sensor_voltage, 2);
        Serial.println("V");
#endif
      }
    }
  }
  else
  {
    // Battery protection disabled - always report normal
    if (battery_state != BLEManager::BatteryState::NORMAL || !battery_state_sent)
    {
      battery_state = BLEManager::BatteryState::NORMAL;
      battery_state_sent = true;
      // Send immediately on state change or initial state
      bleManager.sendBatteryStatus(battery_state, sensor_voltage);
#if ENABLE_SERIAL_DEBUG
      Serial.println("Battery protection: Disabled - status set to NORMAL");
#endif
    }
  }

  // Send BLE data as fast as possible (no throttling)
  if (bleManager.isConnected())
  {
    // Get actual throttle from ESC (accounts for ramping)
    float actual_throttle = esc.getActualThrottle();
    
    if (current_esc_mode == 0) // PWM mode
    {
      bleManager.sendPWMData(sensor_voltage, sensor_current, actual_throttle, battery_state);
    }
    else // DSHOT mode
    {
      ESC::telemData telemetry = esc.getTelemetry();
      bleManager.sendDSHOTData(
        sensor_voltage,
        sensor_current,
        actual_throttle,
        telemetry.rpm,
        telemetry.voltage,
        telemetry.current,
        telemetry.temp,
        telemetry.lastStatus,
        telemetry.stress,
        battery_state
      );
    }
  }

#if ENABLE_SERIAL_DEBUG
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
    Serial.print(esc.getActualThrottle(), 1);
    Serial.print("%  BLE: ");
    Serial.print(bleManager.isConnected() ? "Connected" : "Disconnected");
    
    // Battery protection debug info
    const ESCConfigPacket& config = bleManager.getESCConfig();
    if (config.battery_protection_enabled)
    {
      Serial.print("  Batt: ");
      if (battery_state == BLEManager::BatteryState::CUTOFF)
        Serial.print("CUTOFF");
      else if (battery_state == BLEManager::BatteryState::WARNING)
        Serial.print("WARNING");
      else
        Serial.print("NORMAL");
      Serial.print(" (cutoff<");
      Serial.print(cutoff_voltage, 2);
      Serial.print("V, warn<");
      Serial.print(warning_voltage, 2);
      Serial.print("V)");
    }
    Serial.println();
  }
#endif
}
