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

    /// @brief Force disconnect (for watchdog/error conditions)
    void forceDisconnect();

    /// @brief Send PWM mode data packet
    /// @param voltage Battery voltage in volts
    /// @param current Current in amps
    /// @param throttle Throttle percentage (0-100% uni, -100 to +100% bi)
    /// @param battery_state Battery protection state (NORMAL, WARNING, CUTOFF)
    void sendPWMData(float voltage, float current, float throttle, BatteryState battery_state);

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
    /// @param battery_state Battery protection state (NORMAL, WARNING, CUTOFF)
    void sendDSHOTData(float voltage, float current, float throttle,
                       uint32_t rpm, float esc_voltage, uint32_t esc_current,
                       uint16_t esc_temp, uint16_t esc_status, uint16_t esc_stress,
                       BatteryState battery_state);

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

    /// @brief Send DSHOT special command response
    /// @param type Response type (0=ack, 1=info, 2=settings)
    /// @param data Optional data bytes
    /// @param length Length of data
    void sendDSHOTResponse(uint8_t type, uint8_t* data, uint16_t length);

    /// @brief Get the current DSHOT special command
    /// @return The DSHOT command value (0-47)
    uint8_t getDSHOTCommand() const { return _dshot_command; }

    /// @brief Check if new DSHOT command has been received
    /// @return true if new DSHOT command available
    bool hasNewDSHOTCommand() const { return _new_dshot_command_available; }

    /// @brief Clear the new DSHOT command flag after processing
    void clearDSHOTCommandFlag() { _new_dshot_command_available = false; }

    /// @brief Get the config write characteristic handle
    /// @return The handle for the config write characteristic
    uint16_t getConfigWriteHandle() const { return _config_write_handle; }

    /// @brief Get the command write characteristic handle
    /// @return The handle for the command write characteristic
    uint16_t getCommandWriteHandle() const { return _command_write_handle; }

    /// @brief Get the DSHOT command write characteristic handle
    /// @return The handle for the DSHOT command write characteristic
    uint16_t getDSHOTCommandWriteHandle() const { return _dshot_command_write_handle; }
    
    /// @brief Check if BTstack restart is in progress
    /// @return true if restart sequence is active
    bool isRestartInProgress() const { return _restart_in_progress; }
    
    /// @brief Set notification enabled state for a characteristic
    /// @param type 0=data, 1=battery, 2=dshot_response
    /// @param enabled true to enable, false to disable
    void setNotificationState(uint8_t type, bool enabled);
    
    /// @brief Check if all notifications are disabled (graceful disconnect signal)
    /// @return true if all notifications are disabled
    bool allNotificationsDisabled() const;

    // Internal methods for BLE callbacks (public for callback access)
    void onConnectionStatusChanged(uint16_t conn_handle, uint8_t status);
    void onConfigWrite(uint16_t conn_handle, uint8_t* data, uint16_t len);
    void onCommandWrite(uint16_t conn_handle, uint8_t* data, uint16_t len);
    void onDSHOTCommandWrite(uint16_t conn_handle, uint8_t* data, uint16_t len);
    
    /// @brief Initiate graceful disconnect sequence (public for callback access)
    void initiateGracefulDisconnect();

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

    // DSHOT special command
    uint8_t _dshot_command;
    bool _new_dshot_command_available;

    // BLE connection handle
    uint16_t _connection_handle;

    // Connection watchdog - detect stale connections
    unsigned long _last_activity_ms;
    static constexpr unsigned long CONNECTION_TIMEOUT_MS = 30000; // 30 seconds
    
    // Connection initialization timeout - detect incomplete connections
    unsigned long _connection_start_ms;
    bool _initialization_complete;
    static constexpr unsigned long INIT_TIMEOUT_MS = 5000; // 5 seconds
    
    // Graceful disconnect flag - set when all CCCDs disabled, cleared when HCI disconnect completes
    bool _graceful_disconnect_pending;
    
    // Forced disconnect flag - set when watchdog forces disconnect (incomplete/stale connection)
    bool _forced_disconnect_pending;
    
    // Awaiting disconnect callback flag - set when gap_disconnect called, cleared when callback fires
    // This is separate from _connected because we clear _connected immediately to prevent watchdog loops
    bool _awaiting_disconnect_callback;
    unsigned long _disconnect_initiated_ms;  // Timestamp when gap_disconnect() was called
    static constexpr unsigned long DISCONNECT_CALLBACK_TIMEOUT_MS = 500;  // Max wait for callback
    
    // Flag to prevent accepting connections during restart (avoids race conditions)
    bool _restart_in_progress;
    
    // CCCD (notification) tracking for graceful disconnect detection
    bool _data_notifications_enabled;
    bool _battery_notifications_enabled;
    bool _dshot_response_notifications_enabled;

    // GATT attribute handles
    uint16_t _data_notification_handle;
    uint16_t _battery_status_handle;
    uint16_t _config_write_handle;
    uint16_t _command_write_handle;
    uint16_t _dshot_command_write_handle;
    uint16_t _dshot_response_handle;
    
    // Internal cleanup method
    void resetConnectionState();
    
    // Internal GATT setup method (called by begin() and during reset)
    void setupGATTServices();
};
