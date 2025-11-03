# Smart RC Power Meter - AI Coding Instructions

## Project Overview
Embedded power analyzer for RC electronics running on Raspberry Pi Pico W. Measures voltage (up to 8S/33.6V), current (up to 150A), and reads ESC telemetry via bidirectional DSHOT. Uses PlatformIO with Arduino framework.

## Architecture

### Core Components
- **AnalogSensor** (base class): Provides ADC reading infrastructure with moving average support using `std::deque`
- **CurrentSensor**: Hall-effect sensor (ACS72981KLRATR-150U3) with specific 187.5A scale factor and 0.1V zero offset
- **VoltageDividerSensor**: High-voltage measurement via resistor divider (R1/R2 configurable)
- **ESC**: Dual-mode (DSHOT/PWM) motor controller interface with telemetry reading

### Key Design Patterns
1. **Inheritance hierarchy**: `CurrentSensor` and `VoltageDividerSensor` inherit from `AnalogSensor`
2. **Calibration chain**: Apply `offset` first, then multiply by `scale_factor` (order matters!)
3. **Moving average**: Optional feature enabled via `useAverage(true)`, uses `std::deque` with configurable window
4. **Dynamic allocation**: ESC uses `new`/`delete` for DSHOT object to handle mode switching

### ADC Configuration
- **Resolution**: Always 12-bit (4095 max value), set in `AnalogSensor` constructor
- **Reference voltage**: 3.3V on RP2040
- Pin assignments in `main.cpp`: A0 (current), A1 (voltage), GPIO18 (ESC signal)

## Development Workflow

### Build & Upload
```bash
# Build project
pio run

# Upload to Pico W
pio run --target upload

# Monitor serial output (115200 baud)
pio device monitor
```

### Platform Configuration
- **Platform**: Custom Raspberry Pi Pico fork (`maxgerhardt/platform-raspberrypi`)
- **Core**: earlephilhower (required for specific Arduino APIs)
- **Key dependency**: `pico-bidir-dshot` (bidirectional DSHOT protocol)
- **Build flag**: `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` (enables BTC/BLE)

## Code Conventions

### File Organization
- Headers in `include/`, implementations in `src/`
- Each class has separate `.h` and `.cpp` files (no header-only classes)
- Use `#pragma once` for header guards

### Sensor Calibration Pattern
Always configure sensors in `main.cpp` with explicit calibration values:
```cpp
constexpr unsigned int PIN = A0;
constexpr float SCALE = 1.048;      // Post-calibration multiplier
constexpr float OFFSET = -0.665;    // Zero-point correction
constexpr unsigned int AVG_WINDOW = 200;  // Moving average samples
```

### ESC Telemetry
- DSHOT mode provides: RPM, voltage, current, temperature, stress, status
- RPM calculation: `raw_ERPM / (motor_poles / 2)`
- Voltage scale: `raw_value / 4` (DSHOT spec)
- Telemetry updates automatically on each `setThrottle()` call
- PWM mode does not support telemetry

### Memory Management
- ESC class uses dynamic allocation for `BidirDShotX1*` pointer
- Must properly cleanup in `setMode()` when switching modes
- Servo object (`_pwm`) uses stack allocation

## Hardware-Specific Notes

### Current Sensor Formula
From [Pololu datasheet](https://www.pololu.com/product/5279):
```cpp
current = 187.5f * (adc / 4095.0f - 0.1f)
```
Zero current = 10% of Vcc (0.33V at 3.3V supply)

### Voltage Divider Calculation
```cpp
midpoint_voltage = (adc / 4095.0f) * 3.3f
input_voltage = midpoint_voltage * ((R1 + R2) / R2)
```
Default: R1=100kΩ, R2=5.6kΩ (19:1 ratio) for 8S compatibility

## Common Tasks

### Adding New Sensor Types
1. Inherit from `AnalogSensor`
2. Implement `read()` method with sensor-specific conversion formula
3. Call `analogRead(_pin)` to get raw ADC value1. 
4. Apply calibration: `value += _offset; value *= _scale_factor;`
5. Implement moving average using `_rolling_window` deque if `_use_avg` is true

### Modifying ESC Modes
- Check `_mode` before accessing `_dshot` or `_pwm` objects
- Always null-check `_dshot` pointer (can be nullptr)
- Use `setMode()` for safe transitions (handles cleanup)

### Debugging Serial Output
Monitor filter already configured in `platformio.ini` with timestamps. Sensor values print in format:
```
Current (A): 12.3    Voltage (V): 25.2
```
