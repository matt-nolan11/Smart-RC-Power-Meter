#pragma once

/*
 * BLE Manager class for Raspberry Pi Pico W
 * Author: Matthew Nolan
 * Date: November 8, 2025
 * 
 * Manages BLE GATT server, connection handling, and data notifications
 * for the Smart RC Power Meter project.
 * 
 * Uses BTstack library (included with earlephilhower Arduino core)
 */

#include <Arduino.h>
#include <BLEDataStructures.h>

class BLEManager
{
public:
    /// @brief Battery protection states
    enum BatteryState
    {
        NORMAL = 0,
        WARNING = 1,
        CUTOFF = 2
    };

    /// @brief Constructor
    /// @param device_name The BLE device name (max 29 characters)
    BLEManager(const char* device_name = "RC Power Meter");

    /// @brief Initialize BLE stack and GATT services
    /// @return true if initialization succeeded
    bool begin();

    /// @brief Update BLE (must be called regularly in main loop)
    void update();

    /// @brief Check if a client is connected
    /// @return true if connected
    bool isConnected();

    /// @brief Send PWM mode data packet
    /// @param voltage Battery voltage in volts
    /// @param current Current in amps
    /// @param throttle Throttle percentage (0-100% uni, -100 to +100% bi)
    void sendPWMData(float voltage, float current, float throttle);

    /// @brief Send DSHOT mode data packet
    /// @param voltage Battery voltage in volts
    /// @param current Current in amps
    /// @param throttle Throttle percentage (0-100% uni, -100 to +100% bi)
    /// @param rpm Motor RPM
    /// @param esc_voltage ESC voltage in volts
    /// @param esc_current ESC current in amps
    /// @param esc_temp ESC temperature in Celsius
    /// @param esc_status ESC status code
    /// @param esc_stress ESC stress level
    void sendDSHOTData(float voltage, float current, float throttle,
                       uint32_t rpm, float esc_voltage, uint32_t esc_current,
                       uint16_t esc_temp, uint16_t esc_status, uint16_t esc_stress);

    /// @brief Send battery status update
    /// @param state Battery state (NORMAL, WARNING, CUTOFF)
    /// @param voltage Current battery voltage
    void sendBatteryStatus(BatteryState state, float voltage);

    /// @brief Get the current ESC configuration
    /// @return Reference to the ESC configuration packet
    const ESCConfigPacket& getESCConfig() const { return _esc_config; }

    /// @brief Check if new ESC configuration has been received
    /// @return true if new config available
    bool hasNewConfig() const { return _new_config_available; }

    /// @brief Clear the new config flag after processing
    void clearConfigFlag() { _new_config_available = false; }

    /// @brief Get the current ESC command
    /// @return Reference to the ESC command packet
    const ESCCommandPacket& getESCCommand() const { return _esc_command; }

    /// @brief Check if new ESC command has been received
    /// @return true if new command available
    bool hasNewCommand() const { return _new_command_available; }

    /// @brief Clear the new command flag after processing
    void clearCommandFlag() { _new_command_available = false; }

    // Internal methods for BLE callbacks (public for callback access)
    void onConnectionStatusChanged(uint16_t conn_handle, uint8_t status);
    void onConfigWrite(uint16_t conn_handle, uint8_t* data, uint16_t len);
    void onCommandWrite(uint16_t conn_handle, uint8_t* data, uint16_t len);

    // Singleton instance for callbacks (needs public access for callbacks)
    static BLEManager* _instance;

private:
    const char* _device_name;
    bool _connected;
    bool _ble_initialized;

    // Configuration and command packets
    ESCConfigPacket _esc_config;
    ESCCommandPacket _esc_command;
    bool _new_config_available;
    bool _new_command_available;

    // BLE connection handle
    uint16_t _connection_handle;

    // GATT attribute handles
    uint16_t _data_notification_handle;
    uint16_t _battery_status_handle;
    uint16_t _config_write_handle;
    uint16_t _command_write_handle;
};
