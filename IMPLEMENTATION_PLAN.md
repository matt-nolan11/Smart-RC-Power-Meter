# Smart RC Power Meter - Implementation Plan

**Status:** Planning Phase  
**Target Architecture:** BLE + GitHub Pages Web App  
**Last Updated:** November 2, 2025

---

## System Architecture

### Hardware (Pico W)
- **Communication:** Bluetooth Low Energy (BLE) GATT server
- **Sensors:** Current (ACS72981), Voltage Divider, ESC Telemetry (optional)
- **Output:** ESC control via GPIO18 (PWM or DSHOT)

### Software (Web App)
- **Hosting:** GitHub Pages (static site)
- **Connection:** Web Bluetooth API
- **Framework:** TBD (Vanilla JS / React / Vue)
- **Charting:** Chart.js or similar lightweight library

---

## Feature Specifications

### 1. ESC Control Interface

#### 1.1 Control Mode Selection
- **Default Mode:** PWM (broader ESC compatibility)
- **Alternative Mode:** DSHOT (requires compatible ESC)
- **UI Element:** Radio button or toggle switch
- **State:** Mode selection persists in localStorage

#### 1.2 DSHOT Mode Configuration (Optional)
- **Effective Diameter:** For tip speed calculation
  - Numeric input with unit selector
  - Units: inches (default), mm, cm
  - Stored in localStorage per device
- **Moment of Inertia (MOI):** For kinetic energy calculation
  - Numeric input with unit selector
  - Units: kg·mm² (default), kg·cm², kg·m², g·cm²
  - Stored in localStorage per device
- **Tip Speed Display Units:**
  - Dropdown selector: mph (default), m/s, km/h, ft/s
  - Stored in localStorage per device
- **UI Note:** "Optional: Configure for advanced DSHOT metrics"

#### 1.2 DSHOT Mode Configuration (Optional)
- **Effective Diameter:** For tip speed calculation
  - Numeric input with unit selector
  - Units: inches (default), mm, cm
  - Stored in localStorage per device
- **Moment of Inertia (MOI):** For kinetic energy calculation
  - Numeric input with unit selector
  - Units: kg·mm² (default), kg·cm², kg·m², g·cm²
  - Stored in localStorage per device
- **Tip Speed Display Units:**
  - Dropdown selector: mph (default), m/s, km/h, ft/s
  - Stored in localStorage per device
- **UI Note:** "Optional: Configure for advanced DSHOT metrics"

#### 1.3 ESC Type Configuration
- **Options:**
  - Unidirectional (default) - Throttle range: stop to full forward
  - Bidirectional - Throttle range: full reverse to stop to full forward
- **Impact:** Affects throttle mapping and zero-speed value
- **UI Element:** Radio button or toggle switch
- **Implementation Note:** Requires `ESC::stop()` function to be implemented

#### 1.3 ESC Type Configuration
- **Options:**
  - Unidirectional (default) - Throttle range: stop to full forward
  - Bidirectional - Throttle range: full reverse to stop to full forward
- **Impact:** Affects throttle mapping and zero-speed value
- **UI Element:** Radio button or toggle switch
- **Implementation Note:** Requires `ESC::stop()` function to be implemented

#### 1.4 Throttle Control
- **Primary Control:** Slider with live value display
- **Range Configuration:**
  - Minimum: 500μs (configurable)
  - Maximum: 2500μs (configurable)
  - Default: 1000μs - 2000μs
- **UI Elements:**
  - Main slider (large, touch-friendly)
  - Current value display (e.g., "1500μs")
  - Min/max input fields (numeric, μs units)

#### 1.4 Throttle Control
- **Primary Control:** Slider with live value display
- **Range Configuration:**
  - Minimum: 500μs (configurable)
  - Maximum: 2500μs (configurable)
  - Default: 1000μs - 2000μs
- **UI Elements:**
  - Main slider (large, touch-friendly)
  - Current value display (e.g., "1500μs")
  - Min/max input fields (numeric, μs units)

#### 1.5 Ramp Control (Slew Rate Limiting)
- **Purpose:** Gradual throttle changes to prevent sudden motor starts
- **Parameters:**
  - Ramp-up rate (μs/second)
  - Ramp-down rate (μs/second)
  - Enable/disable toggle
- **Default:** Enabled with moderate rate (TBD during testing)
- **Implementation:** Firmware-side interpolation between commanded and actual throttle
- **UI Elements:**
  - Enable/disable checkbox
  - Separate sliders or numeric inputs for up/down rates
  - Visual indicator of commanded vs actual throttle

#### 1.5 Ramp Control (Slew Rate Limiting)
- **Purpose:** Gradual throttle changes to prevent sudden motor starts
- **Parameters:**
  - Ramp-up rate (μs/second)
  - Ramp-down rate (μs/second)
  - Enable/disable toggle
- **Default:** Enabled with moderate rate (TBD during testing)
- **Implementation:** Firmware-side interpolation between commanded and actual throttle
- **UI Elements:**
  - Enable/disable checkbox
  - Separate sliders or numeric inputs for up/down rates
  - Visual indicator of commanded vs actual throttle

#### 1.6 Battery Protection
- **Purpose:** Prevent over-discharge by stopping ESC when voltage drops too low
- **Configuration:**
  - Cell count: Dropdown or numeric input (1S-12S, default: 4S)
  - Low-voltage cutoff per cell: Numeric input (default: 3.2V/cell)
  - UI note: "⚠️ Recommended: Keep above 3.0V to preserve battery health"
  - Warning delta: Numeric input (default: 0.2V per cell)
  - Total cutoff voltage = cells × cutoff_per_cell (e.g., 4S × 3.2V = 12.8V)
  - Total warning voltage = cells × (cutoff_per_cell + warning_delta) (e.g., 4S × 3.4V = 13.6V)
- **Behavior:**
  - **Warning State** (voltage < warning threshold):
    - Yellow banner: "⚠️ Battery voltage low (12.9V) - approaching cutoff (12.8V)"
    - Continue operation but alert user
  - **Cutoff State** (voltage < cutoff voltage):
    - Automatically call `ESC::stop()`
    - Red banner: "🛑 Battery cutoff reached - ESC stopped at 12.7V"
    - Disable START button until voltage recovers or settings changed
    - Optional: Audio alert (browser beep)
- **Implementation:** 
  - Firmware-side monitoring (more reliable)
  - Web app shows warning/cutoff state
  - User can disable protection (checkbox: "Disable battery protection") with confirmation dialog

#### 1.6 Battery Protection
- **Purpose:** Prevent over-discharge by stopping ESC when voltage drops too low
- **Configuration:**
  - Cell count: Dropdown or numeric input (1S-12S, default: 4S)
  - Low-voltage cutoff per cell: Numeric input (default: 3.2V/cell)
  - UI note: "⚠️ Recommended: Keep above 3.0V to preserve battery health"
  - Warning delta: Numeric input (default: 0.2V per cell)
  - Total cutoff voltage = cells × cutoff_per_cell (e.g., 4S × 3.2V = 12.8V)
  - Total warning voltage = cells × (cutoff_per_cell + warning_delta) (e.g., 4S × 3.4V = 13.6V)
- **Behavior:**
  - **Warning State** (voltage < warning threshold):
    - Yellow banner: "⚠️ Battery voltage low (13.5V) - approaching cutoff (12.8V)"
    - Continue operation but alert user
  - **Cutoff State** (voltage < cutoff voltage):
    - Automatically call `ESC::stop()`
    - Red banner: "🛑 Battery cutoff reached - ESC stopped at 12.7V"
    - Disable START button until voltage recovers or settings changed
    - Optional: Audio alert (browser beep)
- **Implementation:** 
  - Firmware-side monitoring (more reliable)
  - Web app shows warning/cutoff state
  - User can disable protection (checkbox: "Disable battery protection") with confirmation dialog

#### 1.7 START/STOP Button
- **Default State:** STOPPED (always on page load)
- **Visual:** Large, prominent button
  - STOPPED state: Green "START" button
  - RUNNING state: Red "STOP" button
- **Behavior:**
  - START: Begins sending throttle commands to ESC
  - STOP: Calls `ESC::stop()`, disables motor
- **Safety:** Must confirm before starting (optional confirmation dialog)

---

### 2. Data Streaming

#### 2.1 PWM Mode Data
**Transmitted from Pico W to Web App:**
- Voltage (V)
- Current (A)
- Commanded Throttle (μs)
- Power (W) - calculated: V × I

**Update Rate:** 10 Hz (100ms intervals)

#### 2.2 DSHOT Mode Data
**All PWM mode data PLUS ESC telemetry:**
- RPM (calculated from ERPM)
- ESC Voltage (V) - from telemetry
- ESC Current (A) - from telemetry
- ESC Temperature (°C)
- ESC Status (raw value)
- ESC Stress Level

**DSHOT Mode Calculated Metrics (requires user configuration):**
- **Tip Speed:** Calculated from RPM and effective diameter
  - User inputs: Diameter (configurable units: mm, cm, inches, default: inches)
  - Formula: `tip_speed = π × diameter × RPM / 60`
  - Display units: Selectable (m/s, km/h, mph, ft/s, default: mph)
  - Example: 10" diameter @ 10,000 RPM = 264 mph tip speed
- **Rotational Kinetic Energy:** Calculated from RPM and moment of inertia
  - User inputs: MOI (configurable units: kg·mm², kg·cm², kg·m², g·cm², default: kg·mm²)
  - Formula: `KE = 0.5 × MOI × (2π × RPM / 60)²`
  - Display units: Joules (J)
  - Example: 5000 kg·mm² MOI @ 10,000 RPM = 274 J

**Update Rate:** 10 Hz (limited by BLE throughput)

#### 2.3 BLE Characteristic Design
**Approach 1: Individual Characteristics (simpler)**
- One characteristic per data item
- Pro: Easy to subscribe to specific data
- Con: Higher overhead (more notifications)

**Approach 2: Packed Struct (efficient)**
- Single characteristic with binary-packed struct
- Pro: Lower BLE overhead, atomic updates
- Con: More complex parsing in web app

**Decision:** TBD based on BLE throughput testing

---

### 3. Flexible Plotting System

#### 3.1 Plot Management
- **Add Plot:** Button to create new chart area
- **Remove Plot:** X button on each plot
- **Reorder Plots:** Drag handles (optional, Phase 2)
- **Layout:** Vertical stack, each plot full-width

#### 3.2 Y-Axis Configuration (Per Plot)
- **Multi-axis Support:** 
  - Left Y-axis (primary)
  - Right Y-axis (optional secondary)
- **Data Assignment:**
  - Dropdown/checkbox list of all available data streams
  - User selects which data items to plot on each axis
  - Color-coded legends
- **Axis Scaling:**
  - Auto-scale (default)
  - Fixed min/max (user-defined)
  - Toggle for each axis

#### 3.3 X-Axis (Time)
- **Fixed Window:** Last N seconds (configurable, default 10s)
- **Auto-scroll:** Continuous real-time updates
- **Zoom/Pan:** Optional Phase 2 feature

#### 3.4 Example Use Cases
```
Plot 1: Power (W) - single axis
Plot 2: Voltage (V) + Current (A) - dual axis
Plot 3: RPM + Temperature - dual axis
Plot 4: Tip Speed (mph) + Kinetic Energy (J) - dual axis
```

**Available Metrics for Plotting:**
- **Always Available:** Voltage, Current, Power, Throttle
- **DSHOT Only:** RPM, ESC Voltage, ESC Current, ESC Temperature, ESC Status, ESC Stress
- **DSHOT + Config:** Tip Speed (requires diameter), Kinetic Energy (requires MOI)

#### 3.5 Plot Presets (Phase 2)
- Save/load plot configurations
- Pre-defined templates (e.g., "Power Analysis", "ESC Health")

---

### 4. CSV Export

#### 4.1 Data Selection
- **UI:** Checkbox list of all available data items
- **Defaults:** All items checked
- **Include:**
  - Timestamp (ms since session start OR absolute time)
  - Any subset of: voltage, current, power, throttle, RPM, temp, etc.

#### 4.2 Export Trigger
- **Button:** "Export to CSV" (prominent placement)
- **Data Source:** 
  - Option A: Export buffered data from current session
  - Option B: Real-time logging toggle (start/stop recording)
- **Decision:** Option B (explicit logging control)

#### 4.3 File Format
```csv
Timestamp (ms), Voltage (V), Current (A), Power (W), Throttle (μs), RPM, Temp (°C)
0, 12.34, 5.67, 69.99, 1500, 0, 25
100, 12.35, 5.68, 70.15, 1500, 0, 25
...
```

#### 4.4 Logging Control
- **UI Elements:**
  - "Start Recording" button (changes to "Stop Recording")
  - Recording indicator (red dot or similar)
  - Data point counter (e.g., "1,234 samples recorded")
- **Buffer:** Store in browser memory (IndexedDB for large datasets?)
- **Limits:** Max recording time or sample count (prevent memory overflow)

---

### 5. Live Data Displays

#### 5.1 Real-Time Data Cards
- **Purpose:** Show current values for selected metrics without plotting
- **UI:** Card-based layout with large, readable numbers
- **Management:**
  - "Add Data View" button
  - Each card has remove button (X)
  - Dropdown to select which metric to display
  - Configurable: units, precision, color-coding

#### 5.2 Example Data Cards
```
┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  Voltage    │  │   Power     │  │  RPM        │  │ Tip Speed   │
│   12.3 V    │  │   68.9 W    │  │  12,450     │  │  264 mph    │
│      [X]    │  │      [X]    │  │      [X]    │  │      [X]    │
└─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘

┌─────────────┐
│ Kinetic E.  │
│   274 J     │
│      [X]    │
└─────────────┘
```

**Available Metrics:**
- **Always Available:** Voltage, Current, Power, Throttle
- **DSHOT Only:** RPM, ESC Voltage, ESC Current, ESC Temperature, ESC Status, ESC Stress
- **DSHOT + Config:** Tip Speed (requires diameter), Kinetic Energy (requires MOI)

### 6. Device Management (Multi-Device Support)

#### 6.1 Device Naming
- **On Connection:** User prompted to name device (default: "Power Meter 1")
- **Persistence:** Name stored in localStorage, associated with BLE device MAC
- **UI:** 
  - Device name shown in header
  - Editable via settings icon
  - Used in plot titles, CSV filenames

#### 6.2 Multi-Device Connection
- **UI Flow:**
  - "Connect Device" button (can click multiple times)
  - Each connected device gets own tab or panel
  - Switch between devices via tabs
- **Data Isolation:**
  - Each device has independent plots and data views
  - Recording state per device
  - CSV export includes device name prefix
- **Session Persistence (Browser localStorage):**
  - Device name stored by BLE MAC address
  - Battery configuration stored per device (cells, cutoff, warning delta)
  - ESC settings (mode, type, ramp) stored per device
  - Throttle range settings stored per device
  - DSHOT config stored per device (diameter, MOI, tip speed units)
  - Settings persist across page reloads and reconnections
  - Clear settings option available in UI

#### 6.3 CSV Filename Format
```
[DeviceName]_[YYYY-MM-DD]_[HH-MM-SS].csv
Example: PowerMeter1_2025-11-02_14-30-45.csv
```

### 7. Web App UI Layout (Draft)

**Responsive Design:** Adapts between mobile (single column) and desktop (multi-column)

#### Desktop Layout:
```
┌────────────────────────────────────────────────────────────────┐
│  RC Power Meter                   [PowerMeter1 ▼]  [+ Connect] │
├────────────────────────────────────────────────────────────────┤
│  Status: Connected | Recording: ● 1,234 samples               │
├────────────────────────────────────────────────────────────────┤
│  ┌─── ESC Control ───────────────────────────────────────────┐│
│  │ Mode: ◉ PWM  ○ DSHOT    Type: ◉ Unidirectional  ○ Bidir │││
│  │                                                            ││
│  │ DSHOT Config (optional):                                  ││
│  │   Diameter: [10] [inches ▼]  MOI: [5000] [kg·mm² ▼]     │││
│  │   Tip speed units: [mph ▼]                                ││
│  │                                                            ││
│  │ Throttle: [=========○==] 1500μs    Range: 1000-2000μs    │││
│  │ Ramp: ☑ Enable  Up: 500μs/s  Down: 1000μs/s              │││
│  │                                                            ││
│  │ Battery: [4S ▼]  Cutoff: [3.2] V/cell  Warning: [0.2] V  │││
│  │          Cutoff: 12.8V  Warning: 13.6V                    │││
│  │          ☑ Battery protection   ⚠️ Keep above 3.0V        │││
│  │                                                            ││
│  │              [ ▶ START ] / [ ■ STOP ]                     │││
│  └────────────────────────────────────────────────────────────┘│
├────────────────────────────────────────────────────────────────┤
│  ⚠️ Battery voltage low (13.5V) - approaching cutoff (12.8V)  │
├────────────────────────────────────────────────────────────────┤
│  ┌─── Live Data ─────────────────────────────────┐ [+ Add]    │
│  │ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐      │
│  │ │ Voltage  │ │ Current  │ │  Power   │ │ Throttle │      │
│  │ │  12.3 V  │ │  5.6 A   │ │  68.9 W  │ │ 1500 μs  │      │
│  │ │    [X]   │ │    [X]   │ │    [X]   │ │    [X]   │      │
│  │ └──────────┘ └──────────┘ └──────────┘ └──────────┘      │
│  │ ┌──────────┐ ┌──────────┐                                │
│  │ │   RPM    │ │Tip Speed │  (DSHOT mode)                  │
│  │ │  12,450  │ │  264 mph │                                │
│  │ │    [X]   │ │    [X]   │                                │
│  │ └──────────┘ └──────────┘                                │
│  └──────────────────────────────────────────────────────────┘ │
├────────────────────────────────────────────────────────────────┤
│  ┌─── Plot 1: Power [PowerMeter1] ─────────────────── [X]  ┐ │
│  │ Y-Axis: ☑ Power (W)                                      │ │
│  │ [Chart.js Line Chart - 10 second window]                │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                │
│  ┌─── Plot 2: Voltage + Current [PowerMeter1] ─────── [X]  ┐ │
│  │ Left: ☑ Voltage (V)  Right: ☑ Current (A)               │ │
│  │ [Chart.js Line Chart with dual axes]                    │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                │
│  [+ Add Plot]                                                  │
├────────────────────────────────────────────────────────────────┤
│  ┌─── Data Logging ──────────────────────────────────────────┐│
│  │ [ Start Recording ] / [ Stop Recording ]                 ││
│  │                                                            ││
│  │ Export: ☑ Voltage ☑ Current ☑ Power ☑ Throttle          ││
│  │         ☑ RPM ☑ Temperature ☑ ESC Voltage ☑ ESC Current ││
│  │         ☑ Tip Speed ☑ Kinetic Energy                     ││
│  │                                                            ││
│  │ Filename: [PowerMeter1_2025-11-02_14-30-45.csv] [Export] ││
│  └────────────────────────────────────────────────────────────┘│
└────────────────────────────────────────────────────────────────┘
```

#### Mobile Layout (Collapsed):
```
┌──────────────────────────────────┐
│  RC Power Meter        [≡ Menu]  │
├──────────────────────────────────┤
│  Device: PowerMeter1  [+ Add]    │
│  Status: Connected  Recording: ● │
├──────────────────────────────────┤
│  ▼ ESC Control                   │
│    Mode: PWM  Type: Uni          │
│    Battery: 4S @ 3.2V (12.8V)    │
│    [=======○=] 1500μs            │
│    [ ▶ START ]                   │
├──────────────────────────────────┤
│  ⚠️ Battery low: 13.5V → 12.8V   │
├──────────────────────────────────┤
│  ▼ Live Data              [+ Add]│
│  ┌────────┐ ┌────────┐           │
│  │Voltage │ │Current │           │
│  │ 12.3 V │ │ 5.6 A  │           │
│  └────────┘ └────────┘           │
├──────────────────────────────────┤
│  ▼ Plot: Power            [X]    │
│    [Chart - compact view]        │
│                                  │
│  [+ Add Plot]                    │
├──────────────────────────────────┤
│  ▼ Recording                     │
│    [Start Recording]             │
│    Select data... [Export CSV]   │
└──────────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Core Infrastructure (MVP)
**Goal:** Basic BLE connection + live data display

- [ ] Pico W: BLE GATT server with basic characteristics
  - Voltage, current, power characteristics
  - Single notification characteristic (packed struct)
- [ ] Pico W: Existing sensor code integration
- [ ] Web App: Basic HTML/CSS/JS structure
- [ ] Web App: BLE connection flow
- [ ] Web App: Live data display (numeric values only)
- [ ] Test: Verify data streaming reliability

### Phase 2: ESC Control
**Goal:** User can control ESC from web interface with battery protection

- [ ] Pico W: Add ESC control BLE characteristics
  - Mode selection (PWM/DSHOT)
  - Throttle command (write characteristic)
  - Ramp settings
  - START/STOP command
  - Battery protection settings (cell count, cutoff per cell, warning delta)
  - Battery status (normal/warning/cutoff state)
- [ ] Pico W: Implement `ESC::stop()` function
- [ ] Pico W: Implement bidirectional throttle mapping
- [ ] Pico W: Implement ramp/slew rate limiting
- [ ] Pico W: Implement low-voltage monitoring with two thresholds
  - Warning threshold: cells × (cutoff + warning_delta)
  - Cutoff threshold: cells × cutoff (auto-stop ESC)
- [ ] Web App: ESC control UI panel
- [ ] Web App: Throttle slider with range config
- [ ] Web App: Battery protection settings (cell count, cutoff, warning delta)
- [ ] Web App: Real-time display of calculated cutoff/warning voltages
- [ ] Web App: UI note about 3.0V minimum recommendation
- [ ] Web App: Low-voltage warning banner (yellow, before cutoff)
- [ ] Web App: Cutoff banner (red, at cutoff with ESC stopped)
- [ ] Web App: START/STOP button with safety checks
- [ ] Web App: Disable protection confirmation dialog
- [ ] Test: ESC responds correctly to commands
- [ ] Test: Warning triggers at correct configurable threshold
- [ ] Test: Battery protection triggers at correct voltage

### Phase 3: DSHOT Telemetry
**Goal:** ESC telemetry streaming in DSHOT mode with calculated metrics

- [ ] Pico W: Add telemetry characteristics
  - RPM, temp, voltage, current, status, stress
- [ ] Pico W: Integrate existing `ESC::getTelemetry()`
- [ ] Web App: Display telemetry data (conditional on mode)
- [ ] Web App: DSHOT configuration UI
  - Propeller diameter input with unit selector
  - MOI input with unit selector
  - Tip speed unit selector
- [ ] Web App: Calculate tip speed from RPM + diameter
  - Support unit conversions (inches, mm, cm → mph, m/s, km/h, ft/s)
- [ ] Web App: Calculate kinetic energy from RPM + MOI
  - Support unit conversions (kg·mm², kg·cm², kg·m², g·cm² → J)
- [ ] Web App: Add tip speed and kinetic energy to available metrics
- [ ] Web App: Store DSHOT config in localStorage per device
- [ ] Web App: Update UI to show/hide DSHOT-only fields
- [ ] Test: Verify telemetry accuracy with known ESC
- [ ] Test: Verify tip speed calculations
- [ ] Test: Verify kinetic energy calculations

### Phase 4: Data Display System
**Goal:** Flexible visualization with plots and live data cards

- [ ] Web App: Live data card system (add/remove cards)
- [ ] Web App: Metric selection dropdown for each card
- [ ] Web App: Card layout (responsive grid)
- [ ] Web App: Implement plot management (add/remove)
- [ ] Web App: Chart.js integration with real-time config
- [ ] Web App: Y-axis data selection UI
- [ ] Web App: Dual-axis support
- [ ] Web App: Auto-scaling logic
- [ ] Web App: Device name in plot titles
- [ ] Test: Performance with 3+ plots + 8+ data cards

### Phase 5: Data Logging & Export
**Goal:** CSV export with user-selectable data and device naming

- [ ] Web App: Recording state management (per device)
- [ ] Web App: Data buffering (in-memory array)
- [ ] Web App: CSV generation with device name prefix
- [ ] Web App: Dynamic filename generation (device_date_time.csv)
- [ ] Web App: Selective data export UI
- [ ] Test: Large dataset exports (1000+ samples)
- [ ] Test: Multi-device CSV exports with correct naming

### Phase 6: Multi-Device Support
**Goal:** Connect to multiple power meters simultaneously with persistent settings

- [ ] Web App: Device naming UI (prompt on first connect)
- [ ] Web App: localStorage implementation (MAC → device settings)
  - Device name persistence
  - Battery config persistence
  - ESC settings persistence
  - Throttle range persistence
- [ ] Web App: Settings load on device reconnection
- [ ] Web App: Clear/reset device settings option
- [ ] Web App: Multi-device connection management
- [ ] Web App: Tab or panel system for device switching
- [ ] Web App: Per-device data isolation (plots, cards, recording)
- [ ] Web App: Device dropdown in header
- [ ] Pico W: BLE advertising with unique/identifiable name
- [ ] Test: 2+ devices connected simultaneously
- [ ] Test: Settings isolated between devices
- [ ] Test: Settings persist across page reload
- [ ] Test: Reconnecting device loads previous settings

### Phase 7: Responsive Design & Polish
**Goal:** Production-ready adaptive UI with settings persistence

- [ ] Web App: Mobile layout (collapsible sections)
- [ ] Web App: Desktop layout (multi-column)
- [ ] Web App: Breakpoint testing (phone, tablet, desktop)
- [ ] Web App: Touch-friendly controls (44px min tap targets)
- [ ] Web App: Connection error handling & recovery
- [ ] Web App: Settings management UI (view/clear stored devices)
- [ ] Pico W: Power optimization (sleep modes?)
- [ ] Documentation: User guide
- [ ] Test: Full integration testing across devices
- [ ] Test: localStorage quota handling (cleanup old devices)

---

## Technical Decisions to Make

### 1. BLE Data Structure
**Question:** Individual characteristics vs packed struct?  
**Considerations:** 
- BLE throughput limit: ~20 notifications/second
- 10 Hz × 8 data items = 80 notifications/second (won't work)
- **Likely Decision:** Packed binary struct in single characteristic

### 2. ESC Stop Implementation
**Question:** How to implement `ESC::stop()` for unidirectional vs bidirectional?  
**Unidirectional:** Send 1000μs (or configured minimum)  
**Bidirectional:** Send 1500μs (center/zero throttle)  
**Action:** Add enum to ESC class for type, implement conditional logic

### 3. Ramp Implementation Location
**Question:** Firmware or web app?  
**Firmware (Recommended):** 
- Pro: Works even if BLE connection drops
- Pro: More precise timing
- Con: More complex firmware
**Web App:**
- Pro: Easier to modify
- Con: Depends on reliable BLE connection

### 4. Battery Protection Implementation
**Question:** Where to implement low-voltage monitoring?  
**Firmware (Recommended):**
- Pro: Works even if BLE disconnects (safety-critical)
- Pro: Can immediately stop ESC
- Con: Requires sending cutoff settings to Pico W
**Web App:**
- Pro: Easier to implement
- Con: Unsafe if connection drops during test

### 5. Web Framework Choice
**Question:** Vanilla JS, React, or Vue?  
**Vanilla JS:**
- Pro: No build step, lightweight
- Con: More boilerplate for state management
**React/Vue:**
- Pro: Better state management, component reusability
- Con: Requires build step (Vite/Webpack)
**Recommendation:** Start vanilla, migrate if complexity grows

### 6. Chart Library
**Options:** Chart.js, Plotly.js, uPlot, Dygraphs  
**Recommendation:** Chart.js (good balance of features & simplicity)

---

## Open Questions

1. **Calibration UI:** Should users be able to adjust sensor calibration from web app (and store in localStorage)?
2. **Data Sync:** Should CSV exports include absolute timestamps (RTC) or relative (ms since start)?
3. **Plot Presets:** How many default templates, and what should they show? Should these persist in localStorage?
4. **Default Data Cards:** Should some data cards (e.g., Voltage, Current, Power) be shown by default on first connect?
5. **Device Discovery:** Should web app show signal strength or other BLE info when scanning?
6. **Max Devices:** Should we limit concurrent connections (e.g., max 4 devices)?
7. **Default Device Names:** What should be the default naming pattern? "Power Meter 1", "Power Meter 2", etc.?
8. **Settings Sync:** Should changing settings in web app immediately write to device via BLE, or batch on START?
9. **localStorage Cleanup:** Should we automatically clear device settings older than X days/weeks?

---

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] User can connect via BLE in <30 seconds
- [ ] Real-time voltage/current displayed at 10 Hz
- [ ] User can control ESC throttle via slider
- [ ] START/STOP button works reliably
- [ ] At least one live plot updates smoothly
- [ ] CSV export produces valid file

### Full Feature Set
- [ ] All features in specification implemented
- [ ] UI responsive on mobile and desktop
- [ ] No data loss during 5+ minute sessions
- [ ] ESC control feels smooth and predictable
- [ ] Plotting system handles 4+ simultaneous plots
- [ ] CSV export completes in <2 seconds for 10,000 samples

---

## Notes & Considerations

- **Browser Compatibility:** Web Bluetooth requires Chrome/Edge/Opera (no Safari/Firefox)
- **HTTPS Requirement:** GitHub Pages serves over HTTPS (required for Web Bluetooth API)
- **Range:** BLE typically ~10m, adequate for bench testing
- **Power:** Consider adding "deep sleep" mode to Pico W when no BLE connection
- **Future:** Consider WiFi fallback for longer range (hybrid approach)
