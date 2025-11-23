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
    // Note: DSHOT object will be recreated with new speed on next connect()
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
        // Always recreate DSHOT object to ensure clean PIO state
        // This is especially important after speed changes
        if (_dshot != nullptr)
        {
            delete _dshot;
        }
        Serial.print("ESC: Creating DSHOT object at speed ");
        Serial.println(static_cast<int>(_dshot_speed));
        _dshot = new BidirDShotX1(_signal_pin, static_cast<int>(_dshot_speed));
    }
    
    _connected = true;
    
    // Send stop command to ensure ESC starts in safe state
    stop();
    
    // DSHOT initialization sequence
    if (_mode == escMode::DSHOT && _dshot != nullptr)
    {
        // Step 1: DSHOT arming sequence - send throttle 0 for ~1 second
        // This is required by most ESC firmware before accepting throttle
        Serial.println("ESC: Starting DSHOT arming sequence (1000ms of throttle=0)...");
        unsigned long arm_start = millis();
        while (millis() - arm_start < 1000)
        {
            _dshot->sendThrottle(0);
            delay(1);
        }
        Serial.println("ESC: DSHOT arming complete");
        
        // Step 2: Enable extended telemetry (command 13) if bidirectional
        if (_esc_type == escType::BIDIRECTIONAL)
        {
            Serial.println("ESC: Enabling extended telemetry (command 13 for 500ms)...");
            unsigned long edt_start = millis();
            while (millis() - edt_start < 500)
            {
                _dshot->sendRaw11Bit(13);
                delayMicroseconds(200);
                updateTelemetry();
                delay(1);
            }
            Serial.println("ESC: Extended telemetry enabled");
        }
        Serial.println("ESC: Ready for throttle");
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
        // Send extended stop sequence to ensure ESC disarms properly
        // This is especially important at higher DSHOT speeds (1200/2400)
        Serial.println("ESC: Sending extended stop sequence...");
        for (int i = 0; i < 20; i++)
        {
            _dshot->sendThrottle(0);
            delay(1);  // 1ms between sends for proper frame timing
        }
        
        // Additional delay to let ESC process the disarm
        delay(100);
        
        Serial.println("ESC: Stopping PIO and deleting DSHOT object...");
        
        // CRITICAL: Must break PIO control of the pin before deleting object
        // Setting pinMode alone isn't enough - need to explicitly change GPIO function
        gpio_set_function(_signal_pin, GPIO_FUNC_SIO);  // Set to software IO
        gpio_set_dir(_signal_pin, GPIO_IN);              // Set as input
        delay(10);  // Brief delay to ensure pin change takes effect
        
        // Now delete DSHOT object to clean up PIO state machine
        delete _dshot;
        _dshot = nullptr;
        
        Serial.println("ESC: DSHOT cleanup complete");
    }
    
    // Ensure pin is INPUT (redundant for DSHOT, but necessary for PWM)
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
    
    // Debug: Print telemetry type and value every 100 successful reads
    static uint16_t telem_count = 0;
    static uint16_t telem_debug_interval = 100;
    bool should_debug = false;
    if (type != BidirDshotTelemetryType::NO_PACKET && type != BidirDshotTelemetryType::CHECKSUM_ERROR) {
        telem_count++;
        if (telem_count >= telem_debug_interval) {
            should_debug = true;
            telem_count = 0;
        }
    }
    
    switch (type)
    {
    case BidirDshotTelemetryType::ERPM:
        _telemetry.rpm = returnValue / (_motor_poles / 2);
        if (should_debug) {
            Serial.print("TELEM: ERPM=");
            Serial.print(returnValue);
            Serial.print(" (RPM=");
            Serial.print(_telemetry.rpm);
            Serial.println(")");
        }
        break;
    case BidirDshotTelemetryType::VOLTAGE:
        _telemetry.voltage = (float)returnValue / 4;
        if (should_debug) {
            Serial.print("TELEM: VOLTAGE raw=");
            Serial.print(returnValue);
            Serial.print(" (");
            Serial.print(_telemetry.voltage);
            Serial.println("V)");
        }
        break;
    case BidirDshotTelemetryType::CURRENT:
        _telemetry.current = returnValue;
        if (should_debug) {
            Serial.print("TELEM: CURRENT=");
            Serial.print(returnValue);
            Serial.println("A");
        }
        break;
    case BidirDshotTelemetryType::TEMPERATURE:
        _telemetry.temp = returnValue;
        if (should_debug) {
            Serial.print("TELEM: TEMP=");
            Serial.print(returnValue);
            Serial.println("°C");
        }
        break;
    case BidirDshotTelemetryType::STATUS:
        _telemetry.lastStatus = returnValue;
        if (should_debug) {
            Serial.print("TELEM: STATUS=0x");
            Serial.println(returnValue, HEX);
        }
        break;
    case BidirDshotTelemetryType::STRESS:
        _telemetry.stress = returnValue & ESC_STATUS_MAX_STRESS_MASK;
        if (should_debug) {
            Serial.print("TELEM: STRESS=");
            Serial.println(_telemetry.stress);
        }
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
    // CRITICAL: Must use sendRaw11Bit() for special commands, NOT sendThrottle()!
    for (uint8_t i = 0; i < repeat_count; i++)
    {
        _dshot->sendRaw11Bit(command);  // Send raw command without telemetry bit
        delayMicroseconds(200); // Wait for telemetry response timing
        
        // Read telemetry to maintain bidirectional communication
        updateTelemetry();
        
        delayMicroseconds(800); // Complete 1ms cycle
    }
}