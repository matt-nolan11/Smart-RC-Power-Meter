# Firmware TODO: DSHOT Special Commands Support

## Summary
The web app now has complete support for DSHOT special commands. The firmware needs to be updated to:
1. Add two new BLE characteristics (command write + response notify)
2. Handle incoming DSHOT command bytes
3. Send commands to ESC via DSHOT protocol
4. Send responses back to web app

## Files to Modify

### 1. `include/BLEManager.h`

Add to class definition:
```cpp
private:
    BLECharacteristic *pDshotCommandCharacteristic;
    BLECharacteristic *pDshotResponseCharacteristic;

public:
    void sendDSHOTResponse(uint8_t type, uint8_t* data, size_t length);
```

### 2. `src/BLEManager.cpp`

#### In `setupCharacteristics()` function, add:
```cpp
// DSHOT Command characteristic (write only)
pDshotCommandCharacteristic = pService->createCharacteristic(
    BLEUUID("12345678-1234-5678-1234-56789abcdef5"),
    BLECharacteristic::PROPERTY_WRITE
);
pDshotCommandCharacteristic->setCallbacks(new DSHOTCommandCallbacks());

// DSHOT Response characteristic (notify only)
pDshotResponseCharacteristic = pService->createCharacteristic(
    BLEUUID("12345678-1234-5678-1234-56789abcdef6"),
    BLECharacteristic::PROPERTY_NOTIFY
);
pDshotResponseCharacteristic->addDescriptor(new BLE2902());
```

#### Add callback class:
```cpp
// Forward declaration for ESC access
extern ESC esc;

class DSHOTCommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() != 1) {
            Serial.println("Invalid DSHOT command length");
            return;
        }
        
        uint8_t command = (uint8_t)value[0];
        
        #if ENABLE_SERIAL_DEBUG
            Serial.print("DSHOT command received: ");
            Serial.println(command);
        #endif
        
        // Handle command based on type
        switch (command) {
            case 1: case 2: case 3: case 4: case 5:
                // Beep commands
                esc.sendSpecialCommand(command);
                sendAckResponse();
                break;
                
            case 6:
                // ESC Info request
                sendESCInfoResponse();
                break;
                
            case 7: case 8: case 20: case 21:
                // Direction control
                esc.sendSpecialCommand(command);
                sendAckResponse();
                break;
                
            case 9: case 10:
                // 3D Mode control
                esc.sendSpecialCommand(command);
                sendAckResponse();
                break;
                
            case 12:
                // Save settings
                esc.sendSpecialCommand(command);
                sendAckResponse();
                break;
                
            case 22: case 23: case 24: case 25:
            case 26: case 27: case 28: case 29:
                // LED control
                esc.sendSpecialCommand(command);
                sendAckResponse();
                break;
                
            default:
                #if ENABLE_SERIAL_DEBUG
                    Serial.println("Unknown DSHOT command");
                #endif
                break;
        }
    }
    
private:
    void sendAckResponse() {
        uint8_t response[1] = {0}; // Type 0 = ack
        BLEManager::getInstance()->sendDSHOTResponse(0, nullptr, 0);
    }
    
    void sendESCInfoResponse() {
        uint8_t data[3];
        data[0] = 0;  // Firmware version (TODO: read from ESC if available)
        data[1] = 0;  // Rotation direction (0=normal, 1=reversed) - TODO: track state
        data[2] = 0;  // 3D mode (0=off, 1=on) - TODO: track state
        
        BLEManager::getInstance()->sendDSHOTResponse(1, data, 3);
    }
};
```

#### Add helper method:
```cpp
void BLEManager::sendDSHOTResponse(uint8_t type, uint8_t* data, size_t dataLength) {
    if (!pDshotResponseCharacteristic) {
        return;
    }
    
    // Create response packet: [type, data...]
    uint8_t response[32]; // Max packet size
    response[0] = type;
    
    if (data && dataLength > 0) {
        memcpy(response + 1, data, dataLength);
    }
    
    pDshotResponseCharacteristic->setValue(response, 1 + dataLength);
    pDshotResponseCharacteristic->notify();
    
    #if ENABLE_SERIAL_DEBUG
        Serial.print("DSHOT response sent, type: ");
        Serial.println(type);
    #endif
}
```

### 3. `include/ESC.h`

Add to class definition:
```cpp
public:
    void sendSpecialCommand(uint8_t command);
```

### 4. `src/ESC.cpp`

Add method implementation:
```cpp
void ESC::sendSpecialCommand(uint8_t command) {
    // Only works in DSHOT mode
    if (_mode != ESCMode::DSHOT || !_dshot) {
        #if ENABLE_SERIAL_DEBUG
            Serial.println("Special commands require DSHOT mode");
        #endif
        return;
    }
    
    // Send special command multiple times for reliability
    // DSHOT protocol requires 6-10 repetitions for special commands
    for (int i = 0; i < 6; i++) {
        _dshot->send_dshot_value(command, DSHOT_TELEMETRIC_ON);
        delay(1); // Small delay between transmissions
    }
    
    #if ENABLE_SERIAL_DEBUG
        Serial.print("DSHOT special command sent: ");
        Serial.println(command);
    #endif
}
```

### 5. `src/main.cpp` (Optional - Singleton Access)

If BLEManager is not a singleton, make it accessible:
```cpp
BLEManager bleManager;

// Or convert to singleton pattern in BLEManager.h:
class BLEManager {
private:
    static BLEManager* instance;
    BLEManager() {}
    
public:
    static BLEManager* getInstance() {
        if (!instance) {
            instance = new BLEManager();
        }
        return instance;
    }
    // ... rest of class
};
```

## Testing Checklist

After implementing firmware changes:

### Basic Tests (No Propeller)
- [ ] Connect to device in DSHOT mode
- [ ] DSHOT Commands panel appears in web app
- [ ] Click "Beep 1" → ESC beeps
- [ ] Try all beep commands (1-5)
- [ ] Click "Read ESC Info" → Response received

### Advanced Tests (No Propeller)
- [ ] LED On/Off buttons (if ESC has LEDs)
- [ ] Direction reversal (test at low throttle with prop after confirming safe)
- [ ] 3D Mode enable/disable (test carefully)
- [ ] Save Settings (verify persistence across power cycle)

### Safety Validation
- [ ] Direction changes only when ESC stopped
- [ ] Settings properly saved and restored
- [ ] No unexpected motor starts
- [ ] BLE communication stable with commands

## DSHOT Protocol Notes

### Special Command Timing
- Commands 1-47 are special commands
- Must be sent 6-10 times for reliability
- Small delay (1ms) between repetitions recommended
- Should not be sent while motor is running at high throttle

### Command Categories
```cpp
// Beep commands (1-5): Audio feedback
// ESC Info (6): Read configuration
// Direction (7,8,20,21): Change rotation
// 3D Mode (9,10): Enable/disable bidirectional
// Settings (11,12): Read/save configuration
// LED (22-29): Control LED indicators
```

### Integration with Regular Throttle
- Special commands use same DSHOT frame format
- Value field contains command number instead of throttle
- Throttle values start at 48 (0-47 reserved for special commands)
- Current ESC class already handles this in `setThrottle()`

## Implementation Priority

### Phase 1 (Essential)
1. ✅ Add BLE characteristics
2. ✅ Add command callback handler
3. ✅ Implement `ESC::sendSpecialCommand()`
4. ✅ Test beeper commands

### Phase 2 (Important)
1. Test ESC info reading
2. Implement direction control testing procedure
3. Add state tracking for direction/3D mode
4. Test save settings functionality

### Phase 3 (Enhancement)
1. Add LED control support
2. Implement 3D mode control (requires compatible ESC)
3. Add firmware version reading from ESC telemetry
4. Enhance error handling and feedback

## Estimated Implementation Time
- Phase 1: 1-2 hours
- Phase 2: 2-3 hours
- Phase 3: 2-4 hours
- Total: 5-9 hours including testing

## Resources
- DSHOT Protocol: https://github.com/bitdump/BLHeli/blob/master/BLHeli_32%20ARM/BLHeli_32%20Firmware%20specs/Digital_Cmd_Spec.txt
- BLHeli_32 Commands: https://github.com/bitdump/BLHeli/tree/master/BLHeli_32%20ARM
