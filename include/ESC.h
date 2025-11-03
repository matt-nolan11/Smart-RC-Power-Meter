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

class ESC
{
public:
    enum escMode
    {
        DSHOT,
        PWM
    };

    enum dshotSpeed
    {
        DSHOT150 = 150,
        DSHOT300 = 300,
        DSHOT600 = 600,
        DSHOT1200 = 1200,
        DSHOT2400 = 2400
    };

    struct telemData
    {
        uint16_t throttle;
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

    /// @brief Set the throttle value for the ESC
    /// @param microseconds The throttle value in microseconds
    void setThrottle(int microseconds);

    /// @brief Getter method for the internally stored telemetry data
    telemData getTelemetry(); // Returns the telemetry data struct

    void setMode(escMode mode); // Sets the ESC communication mode (DSHOT or PWM)
    void setMotorPoles(uint8_t poles); // Sets the number of motor poles for telemetry calculations
    void setDshotSpeed(dshotSpeed speed); // Sets the DSHOT speed (only applicable in DSHOT mode)


private:
    // Static constants (not user-configurable)
    const int _signal_pin;

    // Configuration variables
    escMode _mode;
    uint8_t _motor_poles;
    dshotSpeed _dshot_speed;

    telemData _telemetry; // Telemetry struct

    // ESC communication objects
    BidirDShotX1* _dshot; // DSHOT communication object (dynamically allocated)
    Servo _pwm; // Servo object for PWM control


    void updateTelemetry(); // Reads telemetry data from the ESC and stores it internally

};