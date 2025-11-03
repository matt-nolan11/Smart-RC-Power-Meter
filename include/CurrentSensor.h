#pragma once

/*
 * Class to interface with an analog output current sensor (ACS72981KLRATR-150U3)
 * Author: Matthew Nolan
 * Date: October 25, 2025
 */

#include <AnalogSensor.h>

/// @brief Current Sensor class (specifically for ACS72981KLRATR-150U3)
/// @param analog_pin which analog pin the sensor is connected to
/// @param scale_factor scaling factor to calibrate the output (default is 1.0)
/// @param offset offset to calibrate zero-reading (default is 0.0f)
/// @param avg_window_length length of the moving average window (default is 10)
class CurrentSensor : public AnalogSensor
{
public:
    /// @brief Class constructor
    /// @param analog_pin which analog pin the sensor is connected to
    /// @param scale_factor scaling factor to calibrate the output (default is 1.0)
    /// @param offset offset to calibrate zero-reading (default is 0.0f)
    /// @param avg_window_length length of the moving average window (default is 10)
    CurrentSensor(unsigned int analog_pin, float scale_factor = 1.0f, float offset = 0.0f, unsigned int avg_window_length = 10);

    /// @brief Read the current from the sensor
    /// @return the current measurement in amps
    float read();
};
