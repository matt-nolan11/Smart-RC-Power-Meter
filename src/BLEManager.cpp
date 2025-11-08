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

// Include BTstack att_server for notifications
extern "C" {
#include "ble/att_server.h"
}

// Custom UUIDs for the RC Power Meter service
// Base UUID: 12345678-1234-5678-1234-56789abcdef0
#define SERVICE_UUID                    "12345678-1234-5678-1234-56789abcdef0"
#define DATA_NOTIFICATION_CHAR_UUID     "12345678-1234-5678-1234-56789abcdef1"
#define BATTERY_STATUS_CHAR_UUID        "12345678-1234-5678-1234-56789abcdef2"
#define CONFIG_WRITE_CHAR_UUID          "12345678-1234-5678-1234-56789abcdef3"
#define COMMAND_WRITE_CHAR_UUID         "12345678-1234-5678-1234-56789abcdef4"

// Static instance pointer for callbacks
BLEManager* BLEManager::_instance = nullptr;

// Characteristic IDs for dynamic characteristics
#define DATA_NOTIFICATION_CHAR_ID       1
#define BATTERY_STATUS_CHAR_ID          2
#define CONFIG_WRITE_CHAR_ID            3
#define COMMAND_WRITE_CHAR_ID           4

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
      _connection_handle(0),
      _data_notification_handle(0),
      _battery_status_handle(0),
      _config_write_handle(0),
      _command_write_handle(0)
{
    _instance = this;

    // Initialize ESC config with defaults
    _esc_config.mode = 0; // PWM
    _esc_config.esc_type = 0; // Unidirectional
    _esc_config.throttle_min = 1000;
    _esc_config.throttle_max = 2000;
    _esc_config.ramp_up_rate = 500;
    _esc_config.ramp_down_rate = 1000;
    _esc_config.ramp_enabled = 1;
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

    // Initialize BTstack and start advertising
    BTstack.setup(_device_name);
    BTstack.startAdvertising();

    _ble_initialized = true;
    Serial.println("BLE: Initialization complete");
    Serial.print("BLE: Device name: ");
    Serial.println(_device_name);
    Serial.print("BLE: Service UUID: ");
    Serial.println(SERVICE_UUID);

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

void BLEManager::sendPWMData(float voltage, float current, float throttle)
{
    if (!isConnected())
    {
        return;
    }

    PWMDataPacket packet;
    packet.voltage = voltage;
    packet.current = current;
    packet.throttle = throttle;

    // Send notification via BTstack att_server
    att_server_notify(_connection_handle, _data_notification_handle, 
                     (uint8_t*)&packet, sizeof(PWMDataPacket));
}

void BLEManager::sendDSHOTData(float voltage, float current, float throttle,
                                uint32_t rpm, float esc_voltage, uint32_t esc_current,
                                uint16_t esc_temp, uint16_t esc_status, uint16_t esc_stress)
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

    // Send notification via BTstack att_server
    att_server_notify(_connection_handle, _data_notification_handle,
                     (uint8_t*)&packet, sizeof(DSHOTDataPacket));
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
    att_server_notify(_connection_handle, _battery_status_handle,
                     (uint8_t*)&packet, sizeof(BatteryStatusPacket));
}

void BLEManager::onConnectionStatusChanged(uint16_t conn_handle, uint8_t status)
{
    _connection_handle = conn_handle;
    _connected = (status == 0);
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

        Serial.println("BLE: Received ESC command");
        Serial.print("  Command: ");
        Serial.println(_esc_command.command == 0 ? "STOP" : "START");
        Serial.print("  Throttle: ");
        Serial.println(_esc_command.throttle);
    }
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

static int gattWriteCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t size)
{
    if (!BLEManager::_instance || !buffer)
    {
        return 1; // Error
    }

    // Handle writes for different characteristics
    switch (characteristic_id)
    {
        case CONFIG_WRITE_CHAR_ID:
            BLEManager::_instance->onConfigWrite(0, buffer, size);
            return 0; // Success
            
        case COMMAND_WRITE_CHAR_ID:
            BLEManager::_instance->onCommandWrite(0, buffer, size);
            return 0; // Success
            
        default:
            return 1; // Error - characteristic not writable
    }
}
