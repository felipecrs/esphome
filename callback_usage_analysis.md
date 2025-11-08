# add_on_state_callback Usage Analysis

## Summary

After the Controller Registry migration (PR #11772), `add_on_state_callback` is still widely used in the codebase, but for **legitimate reasons** - components that genuinely need per-entity state tracking.

## Usage Breakdown

### 1. **MQTT Components** (~17 uses)
**Purpose:** Per-entity MQTT configuration requires callbacks
- Each MQTT component instance needs to publish to custom topics with custom QoS/retain settings
- Cannot use Controller pattern due to per-entity configuration overhead
- Examples: `mqtt_sensor.cpp`, `mqtt_climate.cpp`, `mqtt_number.cpp`, etc.

```cpp
this->sensor_->add_on_state_callback([this](float state) {
  this->publish_state(state);
});
```

### 2. **Copy Components** (~10 uses)
**Purpose:** Mirror state from one entity to another
- Each copy instance tracks a different source entity
- Legitimate use of callbacks for entity-to-entity synchronization
- Examples: `copy_sensor.cpp`, `copy_fan.cpp`, `copy_select.cpp`, etc.

```cpp
source_->add_on_state_callback([this](const std::string &value) {
  this->publish_state(value);
});
```

### 3. **Derivative Sensors** (~5-7 uses)
**Purpose:** Compute derived values from source sensors
- **integration_sensor:** Integrates sensor values over time
- **total_daily_energy:** Tracks cumulative energy
- **combination:** Combines multiple sensor values
- **graph:** Samples sensor data for display
- **duty_time:** Tracks on-time duration
- **ntc/absolute_humidity/resistance:** Mathematical transformations

```cpp
this->sensor_->add_on_state_callback([this](float state) {
  this->process_sensor_value_(state);
});
```

### 4. **Climate/Cover with Sensors** (~10-15 uses)
**Purpose:** External sensors providing feedback to control loops
- **feedback_cover:** Binary sensors for open/close/obstacle detection
- **bang_bang/pid/thermostat:** External temperature sensors for climate control
- **climate_ir (toshiba/yashima/heatpumpir):** Temperature sensors for IR climate

```cpp
this->sensor_->add_on_state_callback([this](float state) {
  this->current_temperature = state;
  // Trigger control loop update
});
```

### 5. **Entity Base Classes** (~10-15 definitions)
**Purpose:** Provide the callback interface for all entities
- Not actual usage, just the method definitions
- Examples: `sensor.cpp::add_on_state_callback()`, `climate.cpp::add_on_state_callback()`, etc.

### 6. **Automation Trigger Classes** (~15-20 definitions)
**Purpose:** User-defined YAML automations need callbacks
- Files like `sensor/automation.h`, `climate/automation.h`
- Implement triggers like `on_value:`, `on_state:`
- Cannot be migrated - this is user-facing automation functionality

### 7. **Miscellaneous** (~5-10 uses)
- **voice_assistant/micro_wake_word:** State coordination
- **esp32_improv:** Provisioning state tracking
- **http_request/update:** Update status monitoring
- **switch/binary_sensor:** Cross-component dependencies
- **OTA callbacks:** OTA state monitoring

## Key Insights

### What's NOT Using Callbacks Anymore ✅
**API Server and WebServer** - migrated to Controller Registry
- **Before:** Each entity had 2 callbacks (API + WebServer) = ~32 bytes overhead
- **After:** Zero per-entity overhead = saves ~32 bytes per entity

### What SHOULD Keep Using Callbacks ✅
All the above categories have legitimate reasons:

1. **Per-entity configuration:** MQTT needs custom topics/QoS per entity
2. **Entity-to-entity relationships:** Copy components, derivative sensors
3. **Control loop feedback:** Climate/cover with external sensors
4. **User-defined automations:** YAML triggers configured by users
5. **Component dependencies:** Components that genuinely depend on other entities

## Memory Impact

**Per Sensor (ESP32):**
- Empty callback infrastructure: **~16 bytes** (unique_ptr + empty vector)
- With one callback (e.g., MQTT): **~32 bytes** (16 + std::function)
- With multiple callbacks: **~32 + 16n bytes** (where n = additional callbacks)

**Typical scenarios:**
- Sensor with **only API/WebServer:** ~16 bytes (no callbacks registered)
- Sensor with **MQTT:** ~32 bytes (one callback)
- Sensor with **MQTT + automation:** ~48 bytes (two callbacks)
- Sensor with **copy + total_daily_energy + graph:** ~64 bytes (three callbacks)

## Conclusion

The callback system is still heavily used (~103 occurrences) but for **appropriate reasons**:
- Components with per-entity state/configuration (MQTT, Copy)
- Sensor processing chains (derivatives, transformations)
- Control loops with external feedback (climate, covers)
- User-defined automations (cannot be removed)

The Controller Registry successfully eliminated wasteful callbacks for **stateless global handlers** (API/WebServer), saving ~32 bytes per entity for those use cases.

**No further callback elimination opportunities** exist without fundamentally changing ESPHome's architecture or breaking user-facing features.
