/*
 * BLE Manager class implementation
 * Author: Matthew Nolan
 * Date: November 8, 2025
 * 
 * This implementation uses the BTstack library provided by the 
 * earlephilhower Arduino core for RP2040.
 */

#include <BLEManager.h>
#include <BTstackLib.h>
#include <hardware/watchdog.h>  // For watchdog_enable() to trigger MCU reset

// Include BTstack att_server for notifications and gap for connection parameters
extern "C" {
#include "ble/att_server.h"
#include "bluetooth.h"
#include "gap.h"
}

// Custom UUIDs for the RC Power Meter service
// Base UUID: 12345678-1234-5678-1234-56789abcdef0
#define SERVICE_UUID                    "12345678-1234-5678-1234-56789abcdef0"
#define DATA_NOTIFICATION_CHAR_UUID     "12345678-1234-5678-1234-56789abcdef1"
#define BATTERY_STATUS_CHAR_UUID        "12345678-1234-5678-1234-56789abcdef2"
#define CONFIG_WRITE_CHAR_UUID          "12345678-1234-5678-1234-56789abcdef3"
#define COMMAND_WRITE_CHAR_UUID         "12345678-1234-5678-1234-56789abcdef4"
#define DSHOT_COMMAND_CHAR_UUID         "12345678-1234-5678-1234-56789abcdef5"
#define DSHOT_RESPONSE_CHAR_UUID        "12345678-1234-5678-1234-56789abcdef6"

// Static instance pointer for callbacks
BLEManager* BLEManager::_instance = nullptr;

// Characteristic IDs for dynamic characteristics
#define DATA_NOTIFICATION_CHAR_ID       1
#define BATTERY_STATUS_CHAR_ID          2
#define CONFIG_WRITE_CHAR_ID            3
#define COMMAND_WRITE_CHAR_ID           4
#define DSHOT_COMMAND_CHAR_ID           5
#define DSHOT_RESPONSE_CHAR_ID          6

// Forward declarations for BTstack callbacks
static void deviceConnectedCallback(BLEStatus status, BLEDevice *device);
static void deviceDisconnectedCallback(BLEDevice *device);
static uint16_t gattReadCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size);
static int gattWriteCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t size);

BLEManager::BLEManager(const char* device_name)
    : _device_name(device_name),
      _connected(false),
      _ble_initialized(false),
      _new_config_available(false),
      _new_command_available(false),
      _dshot_command(0),
      _new_dshot_command_available(false),
      _connection_handle(0),
      _last_activity_ms(0),
      _connection_start_ms(0),
      _initialization_complete(false),
      _graceful_disconnect_pending(false),
      _forced_disconnect_pending(false),
      _awaiting_disconnect_callback(false),
      _disconnect_initiated_ms(0),
      _restart_in_progress(false),
      _data_notifications_enabled(false),
      _battery_notifications_enabled(false),
      _dshot_response_notifications_enabled(false),
      _data_notification_handle(0),
      _battery_status_handle(0),
      _config_write_handle(0),
      _command_write_handle(0),
      _dshot_command_write_handle(0),
      _dshot_response_handle(0)
{
    _instance = this;

    // Initialize ESC config with defaults
    _esc_config.mode = 0; // PWM
    _esc_config.esc_type = 0; // Unidirectional
    _esc_config.throttle_min = 1000;
    _esc_config.throttle_max = 2000;
    _esc_config.ramp_up_rate = 500;
    _esc_config.ramp_down_rate = 1000;
    _esc_config.ramp_up_enabled = 1;
    _esc_config.ramp_down_enabled = 1;
    _esc_config.battery_cells = 4;
    _esc_config.battery_cutoff_mv = 3200; // 3.2V per cell
    _esc_config.battery_warning_delta_mv = 200; // 0.2V delta
    _esc_config.battery_protection_enabled = 1;
    _esc_config.motor_poles = 14;

    // Initialize ESC command with defaults
    _esc_command.command = 0; // STOP
    _esc_command.throttle = 1000;
}

bool BLEManager::begin()
{
    Serial.println("BLE: Initializing...");

    // Set callbacks
    BTstack.setBLEDeviceConnectedCallback(deviceConnectedCallback);
    BTstack.setBLEDeviceDisconnectedCallback(deviceDisconnectedCallback);
    BTstack.setGATTCharacteristicRead(gattReadCallback);
    BTstack.setGATTCharacteristicWrite(gattWriteCallback);

    // Setup GATT Database
    setupGATTServices();

    // Initialize BTstack
    BTstack.setup(_device_name);
    
    // BTstack should automatically advertise the GATT service UUIDs
    // If Web Bluetooth can't discover the device, it may be because
    // BTstack doesn't include service UUIDs in advertisement by default
    
    BTstack.startAdvertising();

    _ble_initialized = true;
    Serial.println("BLE: Initialization complete");
    Serial.print("BLE: Device name: ");
    Serial.println(_device_name);
    Serial.print("BLE: Service UUID: ");
    Serial.println(SERVICE_UUID);
    Serial.println("BLE: Started advertising");

    return true;
}

void BLEManager::setupGATTServices()
{
    BTstack.addGATTService(new UUID(SERVICE_UUID));
    
    // Data notification characteristic (PWM or DSHOT data packets)
    // Read + Notify properties
    _data_notification_handle = BTstack.addGATTCharacteristicDynamic(
        new UUID(DATA_NOTIFICATION_CHAR_UUID),
        ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY,
        DATA_NOTIFICATION_CHAR_ID
    );
    
    // Battery status characteristic (battery state notifications)
    // Read + Notify properties
    _battery_status_handle = BTstack.addGATTCharacteristicDynamic(
        new UUID(BATTERY_STATUS_CHAR_UUID),
        ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY,
        BATTERY_STATUS_CHAR_ID
    );
    
    // Config write characteristic (ESC configuration from web app)
    // Read + Write properties
    _config_write_handle = BTstack.addGATTCharacteristicDynamic(
        new UUID(CONFIG_WRITE_CHAR_UUID),
        ATT_PROPERTY_READ | ATT_PROPERTY_WRITE,
        CONFIG_WRITE_CHAR_ID
    );
    
    // Command write characteristic (START/STOP commands from web app)
    // Read + Write properties
    _command_write_handle = BTstack.addGATTCharacteristicDynamic(
        new UUID(COMMAND_WRITE_CHAR_UUID),
        ATT_PROPERTY_READ | ATT_PROPERTY_WRITE,
        COMMAND_WRITE_CHAR_ID
    );

    // DSHOT command characteristic (DSHOT special commands from web app)
    // Write property only
    _dshot_command_write_handle = BTstack.addGATTCharacteristicDynamic(
        new UUID(DSHOT_COMMAND_CHAR_UUID),
        ATT_PROPERTY_WRITE,
        DSHOT_COMMAND_CHAR_ID
    );

    // DSHOT response characteristic (responses to web app)
    // Read + Notify properties
    _dshot_response_handle = BTstack.addGATTCharacteristicDynamic(
        new UUID(DSHOT_RESPONSE_CHAR_UUID),
        ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY,
        DSHOT_RESPONSE_CHAR_ID
    );
}

void BLEManager::update()
{
    if (!_ble_initialized)
    {
        return;
    }

    // BTstack runs its own event loop
    // Connection status is updated via callbacks
    BTstack.loop();
    
    // Disconnect callback timeout - if gap_disconnect() was called but callback never fired,
    // manually restart advertising after timeout to recover from BTstack internal failures
    if (_awaiting_disconnect_callback && 
        (millis() - _disconnect_initiated_ms > DISCONNECT_CALLBACK_TIMEOUT_MS)) {
        Serial.println("BLE: ERROR - Disconnect callback timeout (500ms exceeded)");
        Serial.println("BLE: BTstack gap_disconnect() failed or connection already dead");
        Serial.println("BLE: Manually restarting advertising as fallback recovery");
        
        // Clear flags since callback won't fire
        _awaiting_disconnect_callback = false;
        _graceful_disconnect_pending = false;
        
        // Manually restart advertising (callback would have done this)
        BTstack.stopAdvertising();
        delay(50);
        BTstack.startAdvertising();
        Serial.println("BLE: Advertising restarted after callback timeout");
        
        return;  // Exit to prevent further processing this cycle
    }
    
    // Connection initialization timeout - detect incomplete connections
    // If we're "connected" but haven't received the config write within INIT_TIMEOUT_MS,
    // the connection is incomplete (browser connected but failed during characteristic setup).
    // This typically happens when browser Web Bluetooth API connects at GATT level but
    // fails to retrieve services/characteristics, leaving the connection in a broken state.
    // IMPORTANT: Don't fire timeout if graceful disconnect is in progress (waiting for HCI)
    if (_connected && !_initialization_complete && !_graceful_disconnect_pending &&
        (millis() - _connection_start_ms > INIT_TIMEOUT_MS)) {
        Serial.println("BLE: WARNING - Connection initialization timeout (no config within 5s)");
        Serial.println("BLE: Browser likely failed during characteristic setup");
        Serial.println("BLE: Forcing disconnect and cleanup");
        resetConnectionState();
        
        // Exit immediately - advertising restart handled by resetConnectionState()
        return;
    }
    
    // Connection watchdog - DISABLED to allow idle connections
    // The initialization timeout (5s) is sufficient to catch incomplete connections
    // Idle users should be able to stay connected without interaction
    /*
    if (_connected && (millis() - _last_activity_ms > CONNECTION_TIMEOUT_MS)) {
        Serial.println("BLE: WARNING - Connection timeout detected (no activity for 30s)");
        Serial.println("BLE: Forcing disconnect and cleanup");
        
        if (_connection_handle != 0) {
            Serial.print("BLE: Terminating connection handle: ");
            Serial.println(_connection_handle);
            
            // Terminate the connection at the HCI level
            uint8_t result = gap_disconnect(_connection_handle);
            Serial.print("BLE: gap_disconnect result: ");
            Serial.println(result);
        }
        
        // gap_disconnect() doesn't trigger deviceDisconnectedCallback for HCI-initiated disconnects
        // So we need to manually handle cleanup
        
        // Reset all connection state
        _connected = false;
        _connection_handle = 0;
        _new_config_available = false;
        _new_command_available = false;
        _new_dshot_command_available = false;
        Serial.println("BLE: Connection state cleared");
        
        // Wait for disconnect to complete
        delay(100);
        
        // Set flag to restart advertising on next update() cycle
        // This avoids re-entrancy issues with BTstack.loop()
        _restart_advertising_pending = true;
        Serial.println("BLE: Advertising restart scheduled for next cycle");
        
        // Exit immediately - advertising restart will happen at start of next update() call
        return;
    }
    */
}

bool BLEManager::isConnected()
{
    return _connected;
}

void BLEManager::resetConnectionState()
{
    Serial.println("BLE: Resetting connection state...");
    
    // Ensure connection is fully terminated at BTstack level
    if (_connection_handle != 0) {
        Serial.print("BLE: Terminating connection handle: ");
        Serial.println(_connection_handle);
        
        // Store handle for gap_disconnect call
        uint16_t handle_to_disconnect = _connection_handle;
        
        // CRITICAL: Clear _connected flag IMMEDIATELY to prevent watchdog from firing again
        // But KEEP _connection_handle valid so disconnect callback can match it
        // Set _awaiting_disconnect_callback so callback knows to restart advertising
        _connected = false;
        _awaiting_disconnect_callback = true;  // Callback will restart advertising
        _forced_disconnect_pending = true;  // Mark as forced disconnect (will trigger reset)
        // DO NOT clear _connection_handle yet - callback needs it to match
        _initialization_complete = false;
        _graceful_disconnect_pending = false;
        _data_notifications_enabled = false;
        _battery_notifications_enabled = false;
        _dshot_response_notifications_enabled = false;
        _new_config_available = false;
        _new_command_available = false;
        _new_dshot_command_available = false;
        
        Serial.println("BLE: State cleared immediately (preventing watchdog loop)");
        
        // Call gap_disconnect() which will trigger deviceDisconnectedCallback
        // That callback will handle advertising restart properly
        gap_disconnect(handle_to_disconnect);
        _disconnect_initiated_ms = millis();  // Track when we initiated disconnect
        
        Serial.println("BLE: Disconnect initiated - waiting for callback");
    } else {
        Serial.println("BLE: No active connection to reset");
    }
}

void BLEManager::setNotificationState(uint8_t type, bool enabled)
{
    switch (type) {
        case 0:
            _data_notifications_enabled = enabled;
            Serial.print("BLE: Data notifications ");
            Serial.println(enabled ? "enabled" : "disabled");
            break;
        case 1:
            _battery_notifications_enabled = enabled;
            Serial.print("BLE: Battery notifications ");
            Serial.println(enabled ? "enabled" : "disabled");
            break;
        case 2:
            _dshot_response_notifications_enabled = enabled;
            Serial.print("BLE: DSHOT response notifications ");
            Serial.println(enabled ? "enabled" : "disabled");
            break;
    }
}

bool BLEManager::allNotificationsDisabled() const
{
    return !_data_notifications_enabled && 
           !_battery_notifications_enabled && 
           !_dshot_response_notifications_enabled;
}

void BLEManager::initiateGracefulDisconnect()
{
    if (!_connected) return;
    
    Serial.println("BLE: All notifications disabled - graceful disconnect pending");
    
    // Set flag to mark graceful disconnect in progress
    _graceful_disconnect_pending = true;
    
    // Store handle for gap_disconnect call
    uint16_t handle_to_disconnect = _connection_handle;
    
    // CRITICAL: Clear _connected flag IMMEDIATELY to prevent watchdog from firing
    // But KEEP _connection_handle valid so disconnect callback can match it
    // Set _awaiting_disconnect_callback so callback knows to restart advertising
    _connected = false;
    _awaiting_disconnect_callback = true;  // Callback will restart advertising
    // DO NOT clear _connection_handle yet - callback needs it to match
    _initialization_complete = false;
    _data_notifications_enabled = false;
    _battery_notifications_enabled = false;
    _dshot_response_notifications_enabled = false;
    _new_config_available = false;
    _new_command_available = false;
    _new_dshot_command_available = false;
    
    Serial.println("BLE: State cleared immediately (graceful disconnect)");
    
    // Initiate disconnect from peripheral side for fast cleanup
    // This is safe because browser has already disabled notifications (cleanup complete)
    Serial.println("BLE: Initiating disconnect from peripheral...");
    if (handle_to_disconnect != 0) {
        gap_disconnect(handle_to_disconnect);
        _disconnect_initiated_ms = millis();  // Track when we initiated disconnect
    }
}

void BLEManager::forceDisconnect()
{
    if (!_connected) return;
    
    Serial.println("BLE: Force disconnect requested");
    resetConnectionState();
    Serial.println("BLE: Force disconnect complete");
}

void BLEManager::sendPWMData(float voltage, float current, float throttle, BatteryState battery_state)
{
    if (!isConnected())
    {
        return;
    }

    PWMDataPacket packet;
    packet.voltage = voltage;
    packet.current = current;
    packet.throttle = throttle;
    packet.battery_state = static_cast<uint8_t>(battery_state);

    // Send notification via BTstack att_server
    int result = att_server_notify(_connection_handle, _data_notification_handle, 
                                   (uint8_t*)&packet, sizeof(PWMDataPacket));
    
    // Ignore return value - queue overflow (error 87) is normal during high-speed streaming
    (void)result;
}

void BLEManager::sendDSHOTData(float voltage, float current, float throttle,
                                uint32_t rpm, float esc_voltage, uint32_t esc_current,
                                uint16_t esc_temp, uint16_t esc_status, uint16_t esc_stress,
                                BatteryState battery_state)
{
    if (!isConnected())
    {
        return;
    }

    DSHOTDataPacket packet;
    packet.voltage = voltage;
    packet.current = current;
    packet.throttle = throttle;
    packet.rpm = rpm;
    packet.esc_voltage = esc_voltage;
    packet.esc_current = esc_current;
    packet.esc_temp = esc_temp;
    packet.esc_status = esc_status;
    packet.esc_stress = esc_stress;
    packet.battery_state = static_cast<uint8_t>(battery_state);

    // Send notification via BTstack att_server
    int result = att_server_notify(_connection_handle, _data_notification_handle,
                                   (uint8_t*)&packet, sizeof(DSHOTDataPacket));
    
    // Ignore return value - queue overflow (error 87) is normal during high-speed streaming
    (void)result;
}

void BLEManager::sendBatteryStatus(BatteryState state, float voltage)
{
    if (!isConnected())
    {
        return;
    }

    BatteryStatusPacket packet;
    packet.state = static_cast<uint8_t>(state);
    packet.voltage = voltage;

    // Send notification via BTstack att_server
    uint8_t result = att_server_notify(_connection_handle, _battery_status_handle,
                     (uint8_t*)&packet, sizeof(BatteryStatusPacket));
    
#if ENABLE_SERIAL_DEBUG
    if (result == 0)
    {
        Serial.println("  -> Battery notification SENT successfully");
    }
    else
    {
        Serial.print("  -> Battery notification FAILED, error: ");
        Serial.println(result);
    }
#else
    (void)result;
#endif
}

void BLEManager::onConnectionStatusChanged(uint16_t conn_handle, uint8_t status)
{
    Serial.print("BLE: Connection status changed - Handle: ");
    Serial.print(conn_handle);
    Serial.print(", Status: ");
    Serial.println(status);
    
    if (status == 0) {
        // Connection established
        _connection_handle = conn_handle;
        _connected = true;
        
        // Reset activity watchdog
        _last_activity_ms = millis();
        
        // Start initialization timer - expect config write within INIT_TIMEOUT_MS
        _connection_start_ms = millis();
        _initialization_complete = false;
        
        // Reset notification tracking for new connection
        _data_notifications_enabled = false;
        _battery_notifications_enabled = false;
        _dshot_response_notifications_enabled = false;
        
        // Request low-latency parameters
        // Connection interval: 7.5ms min, 15ms max (6-12 units, 1 unit = 1.25ms)
        // Slave latency: 0 (process every connection event)
        // Supervision timeout: 4000ms (400 units, 1 unit = 10ms)
        uint16_t conn_interval_min = 6;   // 7.5ms
        uint16_t conn_interval_max = 12;  // 15ms
        uint16_t slave_latency = 0;
        uint16_t supervision_timeout = 400; // 4000ms
        
        gap_request_connection_parameter_update(conn_handle, 
                                               conn_interval_min, 
                                               conn_interval_max,
                                               slave_latency, 
                                               supervision_timeout);
        
        Serial.println("BLE: Requested low-latency connection parameters");
        Serial.println("  Interval: 7.5-15ms, Latency: 0, Timeout: 4000ms");
    } else {
        // Disconnect event - handle ALL disconnects (status != 0)
        Serial.println("BLE: Device disconnected");
        
        // Reset application connection state
        // DO NOT clear _connection_handle here - let BTstack manage it
        // Clearing it too early might interfere with BTstack's cleanup
        _connected = false;
        _initialization_complete = false;
        _new_config_available = false;
        _new_command_available = false;
        _new_dshot_command_available = false;
        
        bool was_graceful = _graceful_disconnect_pending;
        bool was_forced = _forced_disconnect_pending;
        _graceful_disconnect_pending = false;
        _forced_disconnect_pending = false;
        _awaiting_disconnect_callback = false;  // Clear the flag
        
        if (was_graceful) {
            Serial.println("BLE: Graceful disconnect complete (browser-initiated)");
        } else if (was_forced) {
            Serial.println("BLE: Forced disconnect complete (watchdog timeout)");
        } else {
            Serial.println("BLE: Abnormal disconnect (browser GATT discovery failure)");
        }
        
        // Trigger hardware reset for graceful and forced disconnects to clear BTstack GATT corruption
        // Only skip reset for abnormal disconnects (browser retrying immediately)
        if (was_graceful || was_forced) {
            Serial.println("BLE: Triggering hardware reset to clear BTstack GATT corruption");
            Serial.flush();  // Ensure message is printed before reset
            delay(100);  // Brief delay for serial output
            
            // Trigger hardware reset via watchdog (1ms timeout)
            // This completely resets the MCU and BTstack, clearing all GATT state
            watchdog_enable(1, false);
            while(1);  // Wait for watchdog reset
        } else {
            Serial.println("BLE: Ready for reconnection (advertising continues automatically)");
        }
    }
}

void BLEManager::onConfigWrite(uint16_t conn_handle, uint8_t* data, uint16_t len)
{
    if (!isConnected()) {
        Serial.println("BLE: WARNING - Ignoring config write, not connected");
        return;
    }
    
    // Update activity timer
    _last_activity_ms = millis();
    
    // Mark initialization as complete (first config received)
    if (!_initialization_complete) {
        _initialization_complete = true;
        Serial.println("BLE: Connection initialization complete (config received)");
    }
    
    if (len == sizeof(ESCConfigPacket))
    {
        memcpy(&_esc_config, data, sizeof(ESCConfigPacket));
        _new_config_available = true;

        Serial.println("BLE: Received ESC configuration");
        Serial.print("  Mode: ");
        Serial.println(_esc_config.mode == 0 ? "PWM" : "DSHOT");
        Serial.print("  ESC Type: ");
        Serial.println(_esc_config.esc_type == 0 ? "Unidirectional" : "Bidirectional");
        Serial.print("  Throttle Range: ");
        Serial.print(_esc_config.throttle_min);
        Serial.print(" - ");
        Serial.println(_esc_config.throttle_max);
    }
}

void BLEManager::onCommandWrite(uint16_t conn_handle, uint8_t* data, uint16_t len)
{
    if (!isConnected()) {
        #if ENABLE_SERIAL_DEBUG
            Serial.println("BLE: WARNING - Ignoring command write, not connected");
        #endif
        return;
    }
    
    // Update activity timer
    _last_activity_ms = millis();
    
    if (len == sizeof(ESCCommandPacket))
    {
        memcpy(&_esc_command, data, sizeof(ESCCommandPacket));
        _new_command_available = true;

        #if ENABLE_SERIAL_DEBUG
            Serial.println("BLE: Received ESC command");
            Serial.print("  Command: ");
            Serial.println(_esc_command.command == 0 ? "STOP" : "START");
            Serial.print("  Throttle: ");
            Serial.println(_esc_command.throttle);
        #endif
    }
}

void BLEManager::onDSHOTCommandWrite(uint16_t conn_handle, uint8_t* data, uint16_t len)
{
    if (!isConnected()) {
        #if ENABLE_SERIAL_DEBUG
            Serial.println("BLE: WARNING - Ignoring DSHOT command write, not connected");
        #endif
        return;
    }
    
    // Update activity timer
    _last_activity_ms = millis();
    
    if (len == 1)
    {
        _dshot_command = data[0];
        _new_dshot_command_available = true;

        #if ENABLE_SERIAL_DEBUG
            Serial.println("BLE: Received DSHOT command");
            Serial.print("  Command: ");
            Serial.println(_dshot_command);
        #endif
    }
}

void BLEManager::sendDSHOTResponse(uint8_t type, uint8_t* data, uint16_t length)
{
    if (!isConnected())
    {
        return;
    }

    // Create response packet: [type, data...]
    uint8_t response[32]; // Max packet size
    response[0] = type;
    
    uint16_t total_length = 1;
    if (data && length > 0 && length < 31)
    {
        memcpy(response + 1, data, length);
        total_length += length;
    }

    // Send notification via BTstack att_server
    att_server_notify(_connection_handle, _dshot_response_handle,
                     response, total_length);

    #if ENABLE_SERIAL_DEBUG
        Serial.print("BLE: DSHOT response sent, type: ");
        Serial.println(type);
    #endif
}

// BTstack callback implementations
static void deviceConnectedCallback(BLEStatus status, BLEDevice *device)
{
    if (!BLEManager::_instance) {
        Serial.println("BLE: ERROR - Instance is null in connect callback");
        return;
    }
    
    // Reject connections during restart sequence to prevent race conditions
    if (BLEManager::_instance->isRestartInProgress()) {
        Serial.println("BLE: WARNING - Rejecting connection during restart sequence");
        if (device) {
            gap_disconnect(device->getHandle());
        }
        return;
    }
    
    if (status == BLE_STATUS_OK && device)
    {
        Serial.println("BLE: Device connected");
        BLEManager::_instance->onConnectionStatusChanged(device->getHandle(), 0);
    }
}

static void deviceDisconnectedCallback(BLEDevice *device)
{
    if (!BLEManager::_instance) {
        Serial.println("BLE: ERROR - Instance is null in disconnect callback");
        // Try to restart advertising anyway
        BTstack.stopAdvertising();
        delay(100);
        BTstack.startAdvertising();
        Serial.println("BLE: Advertising restarted (instance was null)");
        return;
    }
    
    if (!device) {
        Serial.println("BLE: ERROR - Device is null in disconnect callback");
        // Try to restart advertising anyway
        BTstack.stopAdvertising();
        delay(100);
        BTstack.startAdvertising();
        Serial.println("BLE: Advertising restarted (device was null)");
        return;
    }
    
    Serial.println("BLE: Device disconnected");
    Serial.print("BLE: Handle: ");
    Serial.println(device->getHandle());
    
    // Notify connection status change - this will set _restart_advertising_pending flag
    // Advertising restart will be handled in update() loop for clean separation
    BLEManager::_instance->onConnectionStatusChanged(device->getHandle(), 1);
}

static uint16_t gattReadCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size)
{
    if (!BLEManager::_instance)
    {
        return 0;
    }

    // Handle reads for different characteristics
    switch (characteristic_id)
    {
        case DATA_NOTIFICATION_CHAR_ID:
            // Data notification characteristic - return empty for now
            return 0;
            
        case BATTERY_STATUS_CHAR_ID:
            // Battery status characteristic - return empty for now
            return 0;
            
        case CONFIG_WRITE_CHAR_ID:
            // Config write characteristic - return current config
            if (buffer && buffer_size >= sizeof(ESCConfigPacket))
            {
                memcpy(buffer, &BLEManager::_instance->getESCConfig(), sizeof(ESCConfigPacket));
                return sizeof(ESCConfigPacket);
            }
            return sizeof(ESCConfigPacket);
            
        case COMMAND_WRITE_CHAR_ID:
            // Command write characteristic - return current command
            if (buffer && buffer_size >= sizeof(ESCCommandPacket))
            {
                memcpy(buffer, &BLEManager::_instance->getESCCommand(), sizeof(ESCCommandPacket));
                return sizeof(ESCCommandPacket);
            }
            return sizeof(ESCCommandPacket);
            
        default:
            return 0;
    }
}

static int gattWriteCallback(uint16_t characteristic_handle, uint8_t *buffer, uint16_t size)
{
    // Ignore writes to handle 0 or with size 0 (invalid/cleanup writes during disconnect)
    if (characteristic_handle == 0 || size == 0) {
        return 0; // Success - silently ignore
    }
    
    Serial.print("BLE: Write callback called - Handle: ");
    Serial.print(characteristic_handle);
    Serial.print(", size: ");
    Serial.println(size);
    
    if (!BLEManager::_instance)
    {
        Serial.println("BLE: Write callback - instance is null!");
        return 1; // Error
    }
    
    if (!buffer)
    {
        Serial.println("BLE: Write callback - buffer is null!");
        return 1; // Error
    }
    
    // Handle CCCD (Client Characteristic Configuration Descriptor) writes
    // These are 2-byte writes to enable/disable notifications (0x0001 = enable, 0x0000 = disable)
    // The web app disables notifications before disconnecting - this is our graceful disconnect signal
    if (size == 2) {
        uint16_t value = buffer[0] | (buffer[1] << 8);
        if (value == 0x0001 || value == 0x0000) {
            bool enable = (value == 0x0001);
            
            // Track which characteristic's notifications are being enabled/disabled
            // Handle 4, 7, 16 are CCCDs for data, battery, and dshot response notifications
            if (characteristic_handle == 4) {
                BLEManager::_instance->setNotificationState(0, enable);
            } else if (characteristic_handle == 7) {
                BLEManager::_instance->setNotificationState(1, enable);
            } else if (characteristic_handle == 16) {
                BLEManager::_instance->setNotificationState(2, enable);
            }
            
            // If all notifications are disabled, the web app is about to disconnect
            // Initiate graceful disconnect sequence BEFORE the actual HCI disconnect
            if (BLEManager::_instance->allNotificationsDisabled() && 
                BLEManager::_instance->isConnected()) {
                BLEManager::_instance->initiateGracefulDisconnect();
            }
            
            return 0; // Success
        }
    }

    // Compare against handles, not IDs
    if (characteristic_handle == BLEManager::_instance->getConfigWriteHandle())
    {
        Serial.println("BLE: Routing to onConfigWrite");
        BLEManager::_instance->onConfigWrite(0, buffer, size);
        return 0; // Success
    }
    else if (characteristic_handle == BLEManager::_instance->getCommandWriteHandle())
    {
        Serial.println("BLE: Routing to onCommandWrite");
        BLEManager::_instance->onCommandWrite(0, buffer, size);
        return 0; // Success
    }
    else if (characteristic_handle == BLEManager::_instance->getDSHOTCommandWriteHandle())
    {
        Serial.println("BLE: Routing to onDSHOTCommandWrite");
        BLEManager::_instance->onDSHOTCommandWrite(0, buffer, size);
        return 0; // Success
    }
    else
    {
        Serial.print("BLE: Unknown characteristic handle: ");
        Serial.println(characteristic_handle);
        return 1; // Error - characteristic not writable
    }
}
