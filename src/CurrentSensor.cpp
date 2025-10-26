/*
 * Class to interface with an analog output current sensor (ACS72981KLRATR-150U3)
 * Author: Matthew Nolan
 * Date: October 25, 2025
 */

#include <CurrentSensor.h>
#include <Arduino.h>
#include <numeric> // needed for std::accumulate

CurrentSensor::CurrentSensor(unsigned int analog_pin,
                             float scale_factor,
                             float offset,
                             unsigned int avg_window_length) : AnalogSensor(analog_pin, scale_factor, offset, avg_window_length)
{
}

float CurrentSensor::read(bool avg)
{
    unsigned int adc = analogRead(_pin);             // Read the ADC value from the analog pin
    float current = 187.5f * (adc / 4095.0f - 0.1f); // Convert ADC value to current in amps (from https://www.pololu.com/product/5279)
    current *= _scale_factor;                        // Apply scaling factor
    current += _offset;                              // Apply offset

    if (!avg)
    {
        return current;
    }

    // Update rolling window for moving average
    _rolling_window.push_back(current);
    if (_rolling_window.size() > _avg_window_length)
    {
        _rolling_window.pop_front();
    }

    // Calculate and return moving average
    return std::accumulate(_rolling_window.begin(), _rolling_window.end(), 0.0f) / _rolling_window.size();
}
