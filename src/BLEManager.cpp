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

void BLEManager::update()
{
    if (!_ble_initialized)
    {
        return;
    }

    // BTstack runs its own event loop
    // Connection status is updated via callbacks
    BTstack.loop();
}

bool BLEManager::isConnected()
{
    return _connected;
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
    _connection_handle = conn_handle;
    _connected = (status == 0);
    
    // Request low-latency connection parameters for high-throughput data streaming
    if (_connected) {
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
        Serial.print("  Interval: 7.5-15ms, Latency: 0, Timeout: 4000ms");
    }
}

void BLEManager::onConfigWrite(uint16_t conn_handle, uint8_t* data, uint16_t len)
{
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
    if (BLEManager::_instance && status == BLE_STATUS_OK && device)
    {
        Serial.println("BLE: Device connected");
        BLEManager::_instance->onConnectionStatusChanged(device->getHandle(), 0);
    }
}

static void deviceDisconnectedCallback(BLEDevice *device)
{
    if (BLEManager::_instance && device)
    {
        Serial.println("BLE: Device disconnected");
        BLEManager::_instance->onConnectionStatusChanged(device->getHandle(), 1);
    }
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
    Serial.print("BLE: Write callback called - Handle: ");
    Serial.print(characteristic_handle);
    Serial.print(", size: ");
    Serial.println(size);
    
    if (!BLEManager::_instance || !buffer)
    {
        Serial.println("BLE: Write callback - instance or buffer is null!");
        return 1; // Error
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
