#include <ESC.h>

ESC::ESC(int signal_pin) : _signal_pin(signal_pin),
                           _dshot(nullptr),
                           _mode(escMode::DSHOT),
                           _esc_type(escType::UNIDIRECTIONAL),
                           _dshot_speed(dshotSpeed::DSHOT600),
                           _motor_poles(14),
                           _throttle_min(1000),
                           _throttle_max(2000),
                           _ramp_up_rate(500),
                           _ramp_down_rate(1000),
                           _ramp_enabled(true),
                           _commanded_throttle(0.0f),
                           _actual_throttle(0.0f),
                           _last_ramp_update(0)
{
    // Initialize the default mode using setMode
    setMode(_mode);
}

void ESC::setThrottle(float percent)
{
    // Clamp percentage to valid range based on ESC type
    if (_esc_type == escType::UNIDIRECTIONAL)
    {
        // Unidirectional: 0 to 100%
        if (percent < 0.0f) percent = 0.0f;
        if (percent > 100.0f) percent = 100.0f;
    }
    else // BIDIRECTIONAL
    {
        // Bidirectional: -100 to +100%
        if (percent < -100.0f) percent = -100.0f;
        if (percent > 100.0f) percent = 100.0f;
    }
    
    // Store commanded throttle for ramp limiting
    _commanded_throttle = percent;
    
    // If ramp is disabled, apply immediately
    if (!_ramp_enabled)
    {
        _actual_throttle = percent;
        sendThrottleInternal();
    }
    // If ramp is enabled, updateRamp() will gradually change _actual_throttle

    _telemetry.throttle = _actual_throttle;
}

uint16_t ESC::percentToPwmMicroseconds(float percent)
{
    if (_esc_type == escType::UNIDIRECTIONAL)
    {
        // Unidirectional: 0-100% maps to throttle_min to throttle_max
        // 0% = minimum throttle (stop), 100% = maximum throttle
        uint16_t range = _throttle_max - _throttle_min;
        return _throttle_min + (uint16_t)((percent / 100.0f) * range);
    }
    else // BIDIRECTIONAL
    {
        // Bidirectional: -100 to +100% maps across full range
        // -100% = minimum (full reverse)
        // 0% = center (stop)
        // +100% = maximum (full forward)
        uint16_t center = (_throttle_min + _throttle_max) / 2;
        uint16_t half_range = center - _throttle_min;
        
        if (percent < 0.0f)
        {
            // Negative (reverse): map -100 to 0% → throttle_min to center
            return center - (uint16_t)((-percent / 100.0f) * half_range);
        }
        else if (percent > 0.0f)
        {
            // Positive (forward): map 0 to 100% → center to throttle_max
            return center + (uint16_t)((percent / 100.0f) * half_range);
        }
        else
        {
            // Zero = center (stop)
            return center;
        }
    }
}

uint16_t ESC::percentToDshotValue(float percent)
{
    if (_esc_type == escType::UNIDIRECTIONAL)
    {
        // Unidirectional DSHOT: 0% = stop command (0), 0.1-100% maps to 48-2047
        // Values 0-47 are reserved for special commands
        // 0 = motor stop, 1-47 = other special commands
        constexpr uint16_t DSHOT_MIN_THROTTLE = 48;
        constexpr uint16_t DSHOT_MAX_THROTTLE = 2047;
        constexpr uint16_t DSHOT_THROTTLE_RANGE = DSHOT_MAX_THROTTLE - DSHOT_MIN_THROTTLE;
        
        if (percent <= 0.0f)
        {
            // 0% = stop command
            return 0;
        }
        else if (percent >= 100.0f)
        {
            return DSHOT_MAX_THROTTLE;
        }
        else
        {
            // Map 0.1-100% to 48-2047 (skip command range)
            return DSHOT_MIN_THROTTLE + (uint16_t)((percent / 100.0f) * DSHOT_THROTTLE_RANGE);
        }
    }
    else // BIDIRECTIONAL (3D Mode)
    {
        // Bidirectional DSHOT throttle ranges:
        // Direction 1 (reverse): 48-1047 (1000 steps)
        // Direction 2 (forward): 1049-2047 (999 steps)
        // Note: 1048 is INVALID and should never be sent!
        // 0 is the stop command
        
        constexpr uint16_t DSHOT_REVERSE_MIN = 48;
        constexpr uint16_t DSHOT_REVERSE_MAX = 1047;
        constexpr uint16_t DSHOT_FORWARD_MIN = 1049;
        constexpr uint16_t DSHOT_FORWARD_MAX = 2047;
        constexpr uint16_t DSHOT_REVERSE_RANGE = DSHOT_REVERSE_MAX - DSHOT_REVERSE_MIN;
        constexpr uint16_t DSHOT_FORWARD_RANGE = DSHOT_FORWARD_MAX - DSHOT_FORWARD_MIN;
        
        if (percent < -0.5f) // Reverse (with small deadband)
        {
            // Map -100 to -0.5% → 48 to 1047
            float abs_percent = -percent; // Make positive
            return DSHOT_REVERSE_MIN + (uint16_t)((abs_percent / 100.0f) * DSHOT_REVERSE_RANGE);
        }
        else if (percent > 0.5f) // Forward (with small deadband)
        {
            // Map 0.5 to 100% → 1049 to 2047
            return DSHOT_FORWARD_MIN + (uint16_t)((percent / 100.0f) * DSHOT_FORWARD_RANGE);
        }
        else
        {
            // Center deadband (-0.5 to +0.5%) = stop command
            return 0;
        }
    }
}

void ESC::sendThrottleInternal()
{
    if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        uint16_t dshot_value = percentToDshotValue(_actual_throttle);
        _dshot->sendThrottle(dshot_value);
        updateTelemetry();
    }
    else if (_mode == escMode::PWM)
    {
        uint16_t pwm_microseconds = percentToPwmMicroseconds(_actual_throttle);
        _pwm.writeMicroseconds(pwm_microseconds);
    }
}

void ESC::sendDshotCommand(dshotCommand command, uint8_t repeat_count)
{
    if (_mode != escMode::DSHOT || _dshot == nullptr)
    {
        Serial.println("ERROR: DSHOT commands only work in DSHOT mode");
        return;
    }
    
    // Send the command the specified number of times
    for (uint8_t i = 0; i < repeat_count; i++)
    {
        _dshot->sendThrottle(static_cast<uint16_t>(command));
        delay(1); // Small delay between command repetitions
    }
    
    Serial.print("Sent DSHOT command ");
    Serial.print(static_cast<uint16_t>(command));
    Serial.print(" (");
    Serial.print(repeat_count);
    Serial.println("x)");
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

void ESC::setEscType(escType type)
{
    _esc_type = type;
}

void ESC::setDshotSpeed(dshotSpeed speed)
{
    _dshot_speed = speed;
    
    // If already in DSHOT mode, reinitialize with new speed
    if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        delete _dshot;
        _dshot = new BidirDShotX1(_signal_pin, static_cast<int>(speed));
    }
}

void ESC::setThrottleRange(uint16_t min, uint16_t max)
{
    _throttle_min = min;
    _throttle_max = max;
}

void ESC::setRampRates(uint16_t up_rate, uint16_t down_rate, bool enabled)
{
    _ramp_up_rate = up_rate;
    _ramp_down_rate = down_rate;
    _ramp_enabled = enabled;
    
    // Initialize ramp timing
    _last_ramp_update = millis();
}

void ESC::updateRamp()
{
    if (!_ramp_enabled)
    {
        return;
    }
    
    unsigned long current_time = millis();
    unsigned long delta_time = current_time - _last_ramp_update;
    
    // Handle timer overflow (millis() wraps every ~49 days)
    if (delta_time > 1000)
    {
        delta_time = 0;
    }
    
    _last_ramp_update = current_time;
    
    // Calculate maximum percentage change allowed based on time elapsed
    // Rate is in μs/second, but we need to convert to %/second
    // For simplicity, assume 1000μs range = 100%, so rate_μs/s ≈ rate_%/s * 10
    // This gives us approximate percentage ramp rates
    float max_percent_change;
    
    if (_commanded_throttle > _actual_throttle)
    {
        // Ramping up
        max_percent_change = (_ramp_up_rate / 10.0f / 1000.0f) * delta_time;
    }
    else if (_commanded_throttle < _actual_throttle)
    {
        // Ramping down
        max_percent_change = (_ramp_down_rate / 10.0f / 1000.0f) * delta_time;
    }
    else
    {
        // Already at target
        return;
    }
    
    // Calculate new actual throttle percentage
    float difference = _commanded_throttle - _actual_throttle;
    
    if (abs(difference) <= max_percent_change)
    {
        // Within one step, just set to target
        _actual_throttle = _commanded_throttle;
    }
    else
    {
        // Apply maximum change
        if (difference > 0.0f)
        {
            _actual_throttle += max_percent_change;
        }
        else
        {
            _actual_throttle -= max_percent_change;
        }
    }
    
    // Send updated throttle to ESC
    sendThrottleInternal();
    _telemetry.throttle = _actual_throttle;
}

void ESC::stop()
{
    // Set throttle to 0% (stop)
    _commanded_throttle = 0.0f;
    _actual_throttle = 0.0f;
    
    if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        // DSHOT mode: Send special stop command (command 0)
        _dshot->sendThrottle(0);
    }
    else if (_mode == escMode::PWM)
    {
        // PWM mode: Behavior depends on ESC type
        if (_esc_type == escType::UNIDIRECTIONAL)
        {
            // Unidirectional: Send minimum throttle (typically 1000μs)
            _pwm.writeMicroseconds(_throttle_min);
        }
        else // BIDIRECTIONAL
        {
            // Bidirectional: Send center/zero throttle (typically 1500μs)
            uint16_t center = (_throttle_min + _throttle_max) / 2;
            _pwm.writeMicroseconds(center);
        }
    }

    _telemetry.throttle = 0;
}

ESC::telemData ESC::getTelemetry()
{
    return _telemetry;
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