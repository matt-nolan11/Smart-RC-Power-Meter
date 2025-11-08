#pragma once

/*
 * Data structures for BLE communication
 * Author: Matthew Nolan
 * Date: November 8, 2025
 * 
 * These structures define the binary-packed data format for efficient BLE transmission.
 * Using packed structs allows atomic updates and reduces BLE overhead compared to 
 * individual characteristics per data item.
 */

#include <cstdint>

// Ensure structs are tightly packed (no padding bytes)
#pragma pack(push, 1)

/// @brief PWM mode data packet
/// @details Contains voltage, current, and throttle command
/// Total size: 12 bytes
struct PWMDataPacket
{
    float voltage;      // Battery voltage in volts (4 bytes)
    float current;      // Current in amps (4 bytes)
    float throttle;     // Commanded throttle as percentage: 0-100% uni, -100 to +100% bi (4 bytes)
};

/// @brief DSHOT mode data packet
/// @details Contains all PWM data plus ESC telemetry
/// Total size: 30 bytes
struct DSHOTDataPacket
{
    // Base PWM data
    float voltage;      // Battery voltage in volts (4 bytes)
    float current;      // Current in amps (4 bytes)
    float throttle;     // Commanded throttle as percentage: 0-100% uni, -100 to +100% bi (4 bytes)
    
    // ESC Telemetry data
    uint32_t rpm;       // Motor RPM (calculated from ERPM) (4 bytes)
    float esc_voltage;  // ESC reported voltage in volts (4 bytes)
    uint32_t esc_current; // ESC reported current in amps (4 bytes)
    uint16_t esc_temp;  // ESC temperature in Celsius (2 bytes)
    uint16_t esc_status; // ESC status code (2 bytes)
    uint16_t esc_stress; // ESC stress level (2 bytes)
};

/// @brief ESC configuration packet
/// @details Settings sent from web app to device
/// Total size: 18 bytes
struct ESCConfigPacket
{
    uint8_t mode;           // 0 = PWM, 1 = DSHOT (1 byte)
    uint8_t esc_type;       // 0 = Unidirectional, 1 = Bidirectional (1 byte)
    uint16_t throttle_min;  // Minimum throttle in microseconds (2 bytes)
    uint16_t throttle_max;  // Maximum throttle in microseconds (2 bytes)
    uint16_t ramp_up_rate;  // Ramp up rate in μs/second (2 bytes)
    uint16_t ramp_down_rate; // Ramp down rate in μs/second (2 bytes)
    uint8_t ramp_enabled;   // 0 = disabled, 1 = enabled (1 byte)
    uint8_t battery_cells;  // Number of battery cells (1S-12S) (1 byte)
    uint16_t battery_cutoff_mv; // Cutoff voltage per cell in millivolts (2 bytes)
    uint16_t battery_warning_delta_mv; // Warning delta per cell in millivolts (2 bytes)
    uint8_t battery_protection_enabled; // 0 = disabled, 1 = enabled (1 byte)
    uint8_t motor_poles;    // Number of motor poles for RPM calculation (1 byte)
};

/// @brief Battery status packet
/// @details Battery protection state reported to web app
/// Total size: 5 bytes
struct BatteryStatusPacket
{
    uint8_t state;      // 0 = NORMAL, 1 = WARNING, 2 = CUTOFF (1 byte)
    float voltage;      // Current battery voltage in volts (4 bytes)
};

/// @brief ESC command packet
/// @details Commands sent from web app to control ESC
/// Total size: 5 bytes
struct ESCCommandPacket
{
    uint8_t command;    // 0 = STOP, 1 = START (1 byte)
    float throttle;     // Throttle value as percentage: 0-100% uni, -100 to +100% bi (4 bytes)
};

#pragma pack(pop)

// Verify struct sizes at compile time
static_assert(sizeof(PWMDataPacket) == 12, "PWMDataPacket must be 12 bytes");
static_assert(sizeof(DSHOTDataPacket) == 30, "DSHOTDataPacket must be 30 bytes");
static_assert(sizeof(ESCConfigPacket) == 18, "ESCConfigPacket must be 18 bytes");
static_assert(sizeof(BatteryStatusPacket) == 5, "BatteryStatusPacket must be 5 bytes");
static_assert(sizeof(ESCCommandPacket) == 5, "ESCCommandPacket must be 5 bytes");
