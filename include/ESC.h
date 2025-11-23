#pragma once

/*
 * Class for Electronic Speed Controller (ESC) communication using either DSHOT or PWM
 * Author: Matthew Nolan
 * Date: October 27, 2025
 * 
 * Refer to this example for dshot usage: https://github.com/bastian2001/pico-bidir-dshot/blob/main/examples/4_Full_Pro_Bidir/4_Full_Pro_Bidir.ino
 */

#include <Arduino.h>
#include <PIO_DShot.h>
#include <Servo.h>
#include <hardware/gpio.h>  // For explicit GPIO control during cleanup

class ESC
{
public:
    enum escMode
    {
        DSHOT,
        PWM
    };

    enum escType
    {
        UNIDIRECTIONAL,
        BIDIRECTIONAL
    };

    enum dshotSpeed
    {
        DSHOT150 = 150,
        DSHOT300 = 300,
        DSHOT600 = 600,
        DSHOT1200 = 1200,
        DSHOT2400 = 2400
    };

    // DSHOT special commands (values 0-47 are reserved)
    enum dshotCommand
    {
        DSHOT_CMD_MOTOR_STOP = 0,
        DSHOT_CMD_BEEP1 = 1,
        DSHOT_CMD_BEEP2 = 2,
        DSHOT_CMD_BEEP3 = 3,
        DSHOT_CMD_BEEP4 = 4,
        DSHOT_CMD_BEEP5 = 5,
        DSHOT_CMD_ESC_INFO = 6,
        DSHOT_CMD_SPIN_DIRECTION_1 = 7,
        DSHOT_CMD_SPIN_DIRECTION_2 = 8,
        DSHOT_CMD_3D_MODE_OFF = 9,
        DSHOT_CMD_3D_MODE_ON = 10,
        DSHOT_CMD_SETTINGS_REQUEST = 11,
        DSHOT_CMD_SAVE_SETTINGS = 12,
        DSHOT_CMD_SPIN_DIRECTION_NORMAL = 20,
        DSHOT_CMD_SPIN_DIRECTION_REVERSED = 21,
        DSHOT_CMD_LED0_ON = 22,
        DSHOT_CMD_LED1_ON = 23,
        DSHOT_CMD_LED2_ON = 24,
        DSHOT_CMD_LED3_ON = 25,
        DSHOT_CMD_LED0_OFF = 26,
        DSHOT_CMD_LED1_OFF = 27,
        DSHOT_CMD_LED2_OFF = 28,
        DSHOT_CMD_LED3_OFF = 29,
        DSHOT_CMD_AUDIO_STREAM_MODE = 30,
        DSHOT_CMD_SILENT_MODE = 31,
        DSHOT_CMD_SIGNAL_LINE_TELEMETRY_DISABLE = 32,
        DSHOT_CMD_SIGNAL_LINE_CONTINUOUS_ERPM = 33
    };

    struct telemData
    {
        float throttle;      // Throttle percentage (0-100% or -100 to +100%)
        uint32_t rpm;
        uint32_t temp;
        float voltage;
        uint32_t current;
        uint32_t lastStatus;
        uint32_t stress;
    };

    /// @brief Constructor for the ESC class
    /// @param signal_pin The pin used for ESC signal output
    ESC(int signal_pin);

    /// @brief Set the throttle value as a percentage
    /// @param percent Throttle percentage: 0-100% for unidirectional, -100 to +100% for bidirectional
    void setThrottle(float percent);

    /// @brief Stop the ESC (mode and type dependent)
    void stop();

    /// @brief Connect to ESC - initializes signal output and sends stop command
    void connect();

    /// @brief Disconnect from ESC - stops all signal output
    void disconnect();

    /// @brief Check if ESC is connected
    /// @return True if ESC is connected and outputting signals
    bool isConnected() const;

    /// @brief Getter method for the internally stored telemetry data
    telemData getTelemetry(); // Returns the telemetry data struct

    /// @brief Get the current actual throttle percentage (after ramping)
    /// @return Current throttle percentage (0-100% or -100 to +100%)
    float getActualThrottle() const;

    void setMode(escMode mode); // Sets the ESC communication mode (DSHOT or PWM)
    void setEscType(escType type); // Sets the ESC type (unidirectional or bidirectional)
    void setMotorPoles(uint8_t poles); // Sets the number of motor poles for telemetry calculations
    void setDshotSpeed(dshotSpeed speed); // Sets the DSHOT speed (only applicable in DSHOT mode)
    void setThrottleRange(uint16_t min, uint16_t max); // Sets the throttle range (min/max microseconds)
    void setRampRates(uint16_t up_rate, uint16_t down_rate, bool up_enabled, bool down_enabled); // Sets ramp up/down rates (%/second) and enable states
    void updateRamp(); // Updates ramp state - must be called regularly in main loop
    void resetThrottle(); // Resets actual throttle to 0 (for ramping from stopped state)
    
    /// @brief Send current throttle to ESC (DSHOT: must be called continuously >500Hz to keep ESC alive)
    void sendThrottle(); // Public wrapper for sendThrottleInternal()
    
    /// @brief Read telemetry from ESC (DSHOT bidirectional mode only)
    /// Should be called regularly in main loop to update internal telemetry data
    void readTelemetry(); // Public wrapper for updateTelemetry()

    /// @brief Send DSHOT special command (beeps, direction, settings, etc.)
    /// @param command The DSHOT special command to send (0-47)
    /// @param repeat_count Number of times to repeat the command (default 6)
    void sendDshotCommand(dshotCommand command, uint8_t repeat_count = 6);


private:
    // Static constants (not user-configurable)
    const int _signal_pin;

    // Configuration variables
    escMode _mode;
    escType _esc_type;
    bool _connected;
    uint8_t _motor_poles;
    dshotSpeed _dshot_speed;
    uint16_t _throttle_min;
    uint16_t _throttle_max;

    // Ramp/slew rate limiting
    uint16_t _ramp_up_rate;    // % per second
    uint16_t _ramp_down_rate;  // % per second
    bool _ramp_up_enabled;
    bool _ramp_down_enabled;
    float _commanded_throttle; // Target throttle percentage from user
    float _actual_throttle;    // Current throttle percentage being sent to ESC
    unsigned long _last_ramp_update; // Timestamp for ramp calculations

    telemData _telemetry; // Telemetry struct

    // ESC communication objects
    BidirDShotX1* _dshot; // DSHOT communication object (dynamically allocated)
    Servo _pwm; // Servo object for PWM control

    // Internal conversion methods
    uint16_t percentToPwmMicroseconds(float percent); // Convert percentage to PWM microseconds
    uint16_t percentToDshotValue(float percent);      // Convert percentage to DSHOT value (48-2047)
    void sendThrottleInternal();                      // Actually send throttle to hardware

    void updateTelemetry(); // Reads telemetry data from the ESC and stores it internally

};