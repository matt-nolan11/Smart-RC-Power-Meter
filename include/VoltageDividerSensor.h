#pragma once

/*
 * Class to interface with a voltage divider circuit
 * Author: Matthew Nolan
 * Date: October 25, 2025
 */

#include <AnalogSensor.h>

/// @brief Voltage Divider Sensor class
/// @param analog_pin which analog pin the sensor is connected to
/// @param r1 resistance of the first resistor (ohms)
/// @param r2 resistance of the second resistor (ohms)
/// @param scale_factor scaling factor to calibrate the output (default is 1.0)
/// @param offset offset to calibrate zero-reading (default is 0.0f)
/// @param avg_window_length length of the moving average window (default is 10)
class VoltageDividerSensor : public AnalogSensor
{
public:
    /// @brief Class constructor
    /// @param analog_pin which analog pin the sensor is connected to
    /// @param r1 resistance of the first resistor (ohms)
    /// @param r2 resistance of the second resistor (ohms)
    /// @param scale_factor scaling factor to calibrate the output (default is 1.0)
    /// @param offset offset to calibrate zero-reading (default is 0.0f)
    /// @param avg_window_length length of the moving average window (default is 10)
    VoltageDividerSensor(unsigned int analog_pin, unsigned int r1, unsigned int r2, float scale_factor = 1.0f, float offset = 0.0f, unsigned int avg_window_length = 10);

    /// @brief Read the voltage from the sensor
    /// @return the voltage measurement in volts
    float read();

private:
    unsigned int _r1; // First resistor value (ohms)
    unsigned int _r2; // Second resistor value (ohms)
};
