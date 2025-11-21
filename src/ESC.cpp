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
                           _ramp_up_enabled(true),
                           _ramp_down_enabled(true),
                           _commanded_throttle(0.0f),
                           _actual_throttle(0.0f),
                           _last_ramp_update(0),
                           _connected(false)
{
    // Don't initialize signal output - wait for explicit connect() call
}

void ESC::setThrottle(float percent)
{
    // Don't send throttle if not connected
    if (!_connected) return;
    
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
    
    // If both ramps are disabled, apply immediately
    if (!_ramp_up_enabled && !_ramp_down_enabled)
    {
        _actual_throttle = percent;
        sendThrottleInternal();
    }
    // If any ramp is enabled, updateRamp() will gradually change _actual_throttle

    _telemetry.throttle = _actual_throttle;
}

uint16_t ESC::percentToPwmMicroseconds(float percent)
{
    if (_esc_type == escType::UNIDIRECTIONAL)
    {
        // Unidirectional: 0-0.5% = minimum (stop/deadband), 0.5-100% maps to throttle_min to throttle_max
        if (percent <= 0.5f)
        {
            return _throttle_min; // Deadband - always send minimum
        }
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
        // Unidirectional DSHOT: 0-0.5% = stop command (0), 0.5-100% maps to 48-2047
        // Values 0-47 are reserved for special commands
        // 0 = motor stop, 1-47 = other special commands
        constexpr uint16_t DSHOT_MIN_THROTTLE = 48;
        constexpr uint16_t DSHOT_MAX_THROTTLE = 2047;
        constexpr uint16_t DSHOT_THROTTLE_RANGE = DSHOT_MAX_THROTTLE - DSHOT_MIN_THROTTLE;
        
        if (percent <= 0.5f)
        {
            // 0-0.5% = stop command (deadband)
            return 0;
        }
        else if (percent >= 100.0f)
        {
            return DSHOT_MAX_THROTTLE;
        }
        else
        {
            // Map 0.5-100% to 48-2047 (skip command range)
            // Subtract 0.5 and scale by 99.5% range to properly distribute values
            float normalized = (percent - 0.5f) / 99.5f;  // 0.0 to 1.0
            return DSHOT_MIN_THROTTLE + (uint16_t)(normalized * DSHOT_THROTTLE_RANGE);
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
            float normalized = (abs_percent - 0.5f) / 99.5f;  // 0.0 to 1.0
            return DSHOT_REVERSE_MIN + (uint16_t)(normalized * DSHOT_REVERSE_RANGE);
        }
        else if (percent > 0.5f) // Forward (with small deadband)
        {
            // Map 0.5 to 100% → 1049 to 2047
            float normalized = (percent - 0.5f) / 99.5f;  // 0.0 to 1.0
            return DSHOT_FORWARD_MIN + (uint16_t)(normalized * DSHOT_FORWARD_RANGE);
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
        
        // Debug: Print throttle changes (only when value changes significantly)
        static uint16_t last_printed_value = 0;
        static float last_printed_percent = 0.0f;
        if (abs(dshot_value - last_printed_value) > 10 || abs(_actual_throttle - last_printed_percent) > 1.0f)
        {
            Serial.print("DSHOT: ");
            Serial.print(_actual_throttle, 2);
            Serial.print("% -> ");
            Serial.println(dshot_value);
            last_printed_value = dshot_value;
            last_printed_percent = _actual_throttle;
        }
        
        // Note: Telemetry is read separately in updateTelemetry() which should be called regularly
    }
    else if (_mode == escMode::PWM)
    {
        uint16_t pwm_microseconds = percentToPwmMicroseconds(_actual_throttle);
        _pwm.writeMicroseconds(pwm_microseconds);
    }
}

void ESC::setMode(escMode mode)
{
    // Teardown old mode (only if connected)
    if (_connected && _mode != mode)
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

    // Setup new mode (only if connected)
    if (_connected)
    {
        if (_mode == escMode::PWM && !_pwm.attached())
        {
            _pwm.attach(_signal_pin);
        }
        else if (_mode == escMode::DSHOT && _dshot == nullptr)
        {
            _dshot = new BidirDShotX1(_signal_pin, static_cast<int>(_dshot_speed));
        }
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

void ESC::setRampRates(uint16_t up_rate, uint16_t down_rate, bool up_enabled, bool down_enabled)
{
    _ramp_up_rate = up_rate;
    _ramp_down_rate = down_rate;
    _ramp_up_enabled = up_enabled;
    _ramp_down_enabled = down_enabled;
    
    // Initialize ramp timing
    _last_ramp_update = millis();
}

void ESC::resetThrottle()
{
    // Reset actual throttle to 0% without changing commanded throttle
    // This ensures ramping starts from a stopped state
    _actual_throttle = 0.0f;
    _last_ramp_update = millis();
    
    // Send stop command to ESC
    if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        _dshot->sendThrottle(0);
    }
    else if (_mode == escMode::PWM)
    {
        if (_esc_type == escType::UNIDIRECTIONAL)
        {
            _pwm.writeMicroseconds(_throttle_min);
        }
        else // BIDIRECTIONAL
        {
            uint16_t center = (_throttle_min + _throttle_max) / 2;
            _pwm.writeMicroseconds(center);
        }
    }
}

void ESC::updateRamp()
{
    unsigned long current_time = millis();
    unsigned long delta_time = current_time - _last_ramp_update;
    
    // Handle timer overflow (millis() wraps every ~49 days)
    if (delta_time > 1000)
    {
        delta_time = 0;
    }
    
    _last_ramp_update = current_time;
    
    // Check if we're already at target
    if (_commanded_throttle == _actual_throttle)
    {
        return;
    }
    
    // Calculate maximum percentage change allowed based on time elapsed
    // Rate is now in %/second directly (changed from μs/s)
    float max_percent_change;
    
    if (_commanded_throttle > _actual_throttle)
    {
        // Ramping up
        if (_ramp_up_enabled)
        {
            max_percent_change = (_ramp_up_rate / 1000.0f) * delta_time;
        }
        else
        {
            // Instant jump if ramp up disabled
            _actual_throttle = _commanded_throttle;
            return;
        }
    }
    else // _commanded_throttle < _actual_throttle
    {
        // Ramping down
        if (_ramp_down_enabled)
        {
            max_percent_change = (_ramp_down_rate / 1000.0f) * delta_time;
        }
        else
        {
            // Instant jump if ramp down disabled
            _actual_throttle = _commanded_throttle;
            return;
        }
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
    
    // Don't send here - let main loop handle sending at controlled rate
    _telemetry.throttle = _actual_throttle;
}

void ESC::stop()
{
    // Don't send signals if not connected
    if (!_connected) return;
    
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

void ESC::connect()
{
    if (_connected) 
    {
        Serial.println("ESC: Already connected - ignoring connect request");
        return;
    }
    
    Serial.println("ESC: Connecting...");
    
    // Initialize signal output based on current mode
    if (_mode == escMode::PWM)
    {
        _pwm.attach(_signal_pin);
    }
    else if (_mode == escMode::DSHOT)
    {
        if (_dshot == nullptr)
        {
            Serial.println("ESC: Creating new DSHOT object");
            _dshot = new BidirDShotX1(_signal_pin, static_cast<int>(_dshot_speed));
        }
        else
        {
            Serial.println("ESC: Reusing existing DSHOT object");
        }
    }
    
    _connected = true;
    
    // Send stop command to ensure ESC starts in safe state
    stop();
    
    // DSHOT arming sequence: Send command 0 (stop) for ~300ms
    // This is required by most ESC firmware before accepting throttle
    if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        Serial.println("ESC: Starting DSHOT arming sequence (300ms)...");
        unsigned long arm_start = millis();
        while (millis() - arm_start < 300)
        {
            _dshot->sendThrottle(0); // Send stop command
            delayMicroseconds(200);  // ~5kHz rate as recommended
        }
        Serial.println("ESC: DSHOT arming complete - ready for throttle");
        
        // Enable extended telemetry (command 13, requires sending 10 times)
        // This enables voltage, current, temperature, and stress telemetry
        Serial.println("ESC: Enabling extended telemetry...");
        for (int i = 0; i < 10; i++)
        {
            _dshot->sendThrottle(13);
            delayMicroseconds(200); // Wait for telemetry response timing
            
            // Read telemetry to maintain bidirectional communication
            updateTelemetry();
            
            delayMicroseconds(800); // Complete 1ms cycle
        }
        Serial.println("ESC: Extended telemetry enabled");
    }
}

void ESC::disconnect()
{
    if (!_connected) return; // Already disconnected
    
    Serial.println("ESC: Disconnecting...");
    
    // Send stop command before disconnecting
    stop();
    
    // Reset throttle state completely
    _commanded_throttle = 0.0f;
    _actual_throttle = 0.0f;
    _last_ramp_update = millis();
    
    // Clear telemetry data
    _telemetry = telemData{};
    
    // Cleanup signal output
    if (_mode == escMode::PWM)
    {
        _pwm.detach();
    }
    else if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        // Send a few more stop commands before deleting
        for (int i = 0; i < 5; i++)
        {
            _dshot->sendThrottle(0);
            delayMicroseconds(200);
        }
        
        delete _dshot;
        _dshot = nullptr;
    }
    
    // CRITICAL: Set signal pin to INPUT to stop all output
    // This ensures no spurious signals are sent to ESC after disconnect
    pinMode(_signal_pin, INPUT);
    
    _connected = false;
    Serial.println("ESC: Disconnect complete - signal pin set to INPUT");
}

bool ESC::isConnected() const
{
    return _connected;
}

ESC::telemData ESC::getTelemetry()
{
    return _telemetry;
}

float ESC::getActualThrottle() const
{
    return _actual_throttle;
}

void ESC::sendThrottle()
{
    // Public wrapper - sends current throttle value to ESC
    // For DSHOT: This MUST be called continuously (>500Hz) or ESC will timeout
    sendThrottleInternal();
}

void ESC::readTelemetry()
{
    // Public wrapper - reads telemetry from ESC
    // For DSHOT bidirectional mode: Should be called every loop iteration
    updateTelemetry();
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

void ESC::sendDshotCommand(dshotCommand command, uint8_t repeat_count)
{
    if (_mode != escMode::DSHOT || !_connected || _dshot == nullptr)
    {
        return; // DSHOT commands only work in DSHOT mode when connected
    }

    // Send the special command multiple times as required by DSHOT protocol
    // Most commands need to be sent 6-10 times to be registered by ESC
    for (uint8_t i = 0; i < repeat_count; i++)
    {
        _dshot->sendThrottle(command);
        delayMicroseconds(200); // Wait for telemetry response timing
        
        // Read telemetry to maintain bidirectional communication
        updateTelemetry();
        
        delayMicroseconds(800); // Complete 1ms cycle
    }
}