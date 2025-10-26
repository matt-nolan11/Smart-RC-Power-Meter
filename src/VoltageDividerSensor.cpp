/*
 * Class to interface with a voltage divider circuit
 * Author: Matthew Nolan
 * Date: October 25, 2025
 */

#include <VoltageDividerSensor.h>
#include <Arduino.h>
#include <numeric> // needed for std::accumulate

VoltageDividerSensor::VoltageDividerSensor(unsigned int analog_pin,
                                           unsigned int r1,
                                           unsigned int r2,
                                           float scale_factor,
                                           float offset,
                                           unsigned int avg_window_length) : AnalogSensor(analog_pin, scale_factor, offset, avg_window_length),
                                                                             _r1(r1),
                                                                             _r2(r2)
{
}

float VoltageDividerSensor::read(bool avg)
{
    unsigned int adc = analogRead(_pin);                                    // Read the ADC value from the analog pin
    float midpoint_voltage = (adc / 4095.0f) * 3.3f;                        // Convert ADC value to voltage at midpoint
    float input_voltage = midpoint_voltage * ((_r1 + _r2) / _r2);           // Calculate input voltage using voltage divider formula
    input_voltage *= _scale_factor;                                         // Apply scaling factor
    input_voltage += _offset;                                               // Apply offset

    if (!avg)
    {
        return input_voltage;
    }

    // Update rolling window for moving average
    _rolling_window.push_back(input_voltage);
    if (_rolling_window.size() > _avg_window_length)
    {
        _rolling_window.pop_front();
    }

    // Calculate and return moving average
    return std::accumulate(_rolling_window.begin(), _rolling_window.end(), 0.0f) / _rolling_window.size();
}
