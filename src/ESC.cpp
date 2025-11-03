#include <ESC.h>

ESC::ESC(int signal_pin) : _signal_pin(signal_pin),
                           _dshot(nullptr),
                           _mode(escMode::DSHOT),
                           _dshot_speed(dshotSpeed::DSHOT600),
                           _motor_poles(14)
{
    // Initialize the default mode using setMode
    setMode(_mode);
}

void ESC::setThrottle(int microseconds)
{
    if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        _dshot->sendThrottle(microseconds);
        updateTelemetry(); // Update telemetry after sending throttle command
    }
    else if (_mode == escMode::PWM)
    {
        _pwm.writeMicroseconds(microseconds);
    }

    _telemetry.throttle = microseconds;
}

void ESC::setMode(escMode mode)
{
    // Teardown old mode (only if different from new mode)
    if (_mode != mode)
    {
        if (_mode == escMode::PWM)
        {
            _pwm.detach();
        }
        else if (_mode == escMode::DSHOT)
        {
            if (_dshot != nullptr)
            {
                delete _dshot;
                _dshot = nullptr;
            }
        }
    }

    _mode = mode;

    // Setup new mode (only if not already initialized)
    if (_mode == escMode::PWM && !_pwm.attached())
    {
        _pwm.attach(_signal_pin);
    }
    else if (_mode == escMode::DSHOT && _dshot == nullptr)
    {
        _dshot = new BidirDShotX1(_signal_pin, 600);
    }
}

void ESC::setMotorPoles(uint8_t poles)
{
    _motor_poles = poles;
}

void ESC::updateTelemetry()
{
    if (_mode == escMode::PWM)
    {
        // PWM mode does not support telemetry
        return;
    }

    uint32_t returnValue = 0; // Temporary variable to hold the raw telemetry value
    BidirDshotTelemetryType type = _dshot->getTelemetryPacket(&returnValue);
    switch (type)
    {
    case BidirDshotTelemetryType::ERPM:
        _telemetry.rpm = returnValue / (_motor_poles / 2);
        break;
    case BidirDshotTelemetryType::VOLTAGE:
        _telemetry.voltage = (float)returnValue / 4;
        break;
    case BidirDshotTelemetryType::CURRENT:
        _telemetry.current = returnValue;
        break;
    case BidirDshotTelemetryType::TEMPERATURE:
        _telemetry.temp = returnValue;
        break;
    case BidirDshotTelemetryType::STATUS:
        _telemetry.lastStatus = returnValue;
        break;
    case BidirDshotTelemetryType::STRESS:
        _telemetry.stress = returnValue & ESC_STATUS_MAX_STRESS_MASK;
        break;

    // other possible cases are:
    case BidirDshotTelemetryType::DEBUG_FRAME_1:
        // custom ESC telemetry, not used in regular ESCs
    case BidirDshotTelemetryType::DEBUG_FRAME_2:
        // custom ESC telemetry, not used in regular ESCs
    case BidirDshotTelemetryType::CHECKSUM_ERROR:
        // Means the last packet was received, but corrupted. This is not a problem, just ignore it.
    case BidirDshotTelemetryType::NO_PACKET:
        // No telemetry packet available. Either the wait time between writing the last DShot packet and reading the telemetry packet was too short, or the ESC is not powered. This is not a problem, just ignore it.
    default:
        break;
    }
}