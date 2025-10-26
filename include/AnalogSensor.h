#pragma once

/*
 * Base class for analog sensor measurements
 * Author: Matthew Nolan
 * Date: October 25, 2025
 */

#include <deque>

class AnalogSensor
{
protected:
    /// @brief Class constructor
    /// @param analog_pin which analog pin the sensor is connected to
    /// @param scale_factor scaling factor to calibrate the output (default is 1.0)
    /// @param offset offset to calibrate zero-reading (default is 0.0f)
    /// @param avg_window_length length of the moving average window (default is 10)
    AnalogSensor(unsigned int analog_pin, float scale_factor = 1.0f, float offset = 0.0f, unsigned int avg_window_length = 10);

public:
    virtual ~AnalogSensor() = default;

    /// @brief Set the length of the moving average window
    /// @param length the new length of the moving average window
    void setAvgWindowLength(unsigned int length);

    /// @brief Set the scaling factor for the measurement
    /// @param scale_factor the new scaling factor
    void setScaleFactor(float scale_factor);

    /// @brief Set the offset for the measurement
    /// @param offset the new offset
    void setOffset(float offset);

protected:
    const unsigned int _pin; // Analog pin the sensor is connected to
    unsigned int _avg_window_length; // Length of the moving average window
    std::deque<float> _rolling_window; // Deque to store the {_avg_window_length} most recent readings
    float _scale_factor; // Scaling factor for the measurement
    float _offset; // Offset for calibration
};