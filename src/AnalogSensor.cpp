/*
 * Base class for analog sensor measurements
 * Author: Matthew Nolan
 * Date: October 25, 2025
 */

#include <AnalogSensor.h>
#include <Arduino.h>

AnalogSensor::AnalogSensor(unsigned int analog_pin,
                           float scale_factor,
                           float offset,
                           unsigned int avg_window_length) : _pin(analog_pin),
                                                             _scale_factor(scale_factor),
                                                             _offset(offset),
                                                             _avg_window_length(avg_window_length)
{
    pinMode(_pin, INPUT);
    analogReadResolution(12); // Set ADC resolution to 12 bits (defaults to 10 bits otherwise)
}

void AnalogSensor::setAvgWindowLength(unsigned int length)
{
    if (length > 0)
    {
        _avg_window_length = length;
    }
}

void AnalogSensor::useAverage(bool use_avg)
{
    _use_avg = use_avg;
}

void AnalogSensor::setScaleFactor(float scale_factor)
{
    _scale_factor = scale_factor;
}

void AnalogSensor::setOffset(float offset)
{
    _offset = offset;
}
