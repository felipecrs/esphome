# Lazy Callback Allocation - Tradeoff Analysis

## Current Implementation
```cpp
class Sensor {
  std::unique_ptr<CallbackManager<void(float)>> raw_callback_;  // lazy
  CallbackManager<void(float)> callback_;                       // always allocated
};
```

## Proposed Implementation
```cpp
class Sensor {
  std::unique_ptr<CallbackManager<void(float)>> raw_callback_;  // lazy
  std::unique_ptr<CallbackManager<void(float)>> callback_;      // ALSO lazy
};
```

## Memory Impact (ESP32 32-bit)

### No Callbacks Registered
**Current:**
- `raw_callback_` unique_ptr: 4 bytes (nullptr)
- `callback_` vector struct: 12 bytes (empty, no heap allocation)
- **Total: 16 bytes**

**Lazy:**
- `raw_callback_` unique_ptr: 4 bytes (nullptr)
- `callback_` unique_ptr: 4 bytes (nullptr)
- **Total: 8 bytes**

**Savings: 8 bytes per entity without callbacks** ✅

### One Callback Registered (e.g., MQTT)
**Current:**
- In object: 4 bytes (raw ptr) + 12 bytes (vector struct) = 16 bytes
- On heap: vector allocates storage for std::function ≈ 16 bytes
- **Total: 16 + 16 = 32 bytes**

**Lazy:**
- In object: 4 bytes (raw ptr) + 4 bytes (callback ptr) = 8 bytes
- Heap #1: CallbackManager object (vector struct) = 12 bytes
- Heap #2: vector allocates storage for std::function ≈ 16 bytes
- **Total: 8 + 12 + 16 = 36 bytes**

**Cost: 4 bytes MORE when callbacks are used** ❌

## Code Changes Required

### 1. Update publish_state() - Hot path!
```cpp
// Current
void Sensor::internal_send_state_to_frontend(float state) {
  this->callback_.call(state);  // Always valid
#ifdef USE_CONTROLLER_REGISTRY
  ControllerRegistry::notify_sensor_update(this);
#endif
}

// Lazy - adds nullptr check in hot path
void Sensor::internal_send_state_to_frontend(float state) {
  if (this->callback_) {  // ← NEW: nullptr check
    this->callback_->call(state);
  }
#ifdef USE_CONTROLLER_REGISTRY
  ControllerRegistry::notify_sensor_update(this);
#endif
}
```

### 2. Update add_on_state_callback()
```cpp
// Current
void Sensor::add_on_state_callback(std::function<void(float)> &&callback) {
  this->callback_.add(std::move(callback));
}

// Lazy - lazy allocate on first use
void Sensor::add_on_state_callback(std::function<void(float)> &&callback) {
  if (!this->callback_) {
    this->callback_ = std::make_unique<CallbackManager<void(float)>>();
  }
  this->callback_->add(std::move(callback));
}
```

### 3. Apply to ALL entity types
Need to update:
- Sensor, BinarySensor, TextSensor
- Climate, Fan, Light, Cover
- Switch, Lock, Valve
- Number, Select, Text, Button
- AlarmControlPanel, MediaPlayer
- etc.

## Performance Impact

**Hot path (publish_state):** Adds one nullptr check per state update
- Branch predictor should handle this well (mostly predictable per entity)
- Cost: 1-2 CPU cycles (likely free with branch prediction)

**Cold path (add callback):** Extra allocation + initialization
- Only happens during setup(), not during loop()
- Negligible impact

## Who Benefits?

### Entities WITHOUT callbacks (saves 8 bytes each):
✅ Sensors with **only** API/WebServer (no MQTT, no automations, no copy, no derivatives)
✅ Switches with **only** API/WebServer
✅ Binary sensors with **only** API/WebServer
✅ Covers, Fans, Lights, etc. with **only** API/WebServer

### Entities WITH callbacks (costs 4 bytes each):
❌ Any entity with MQTT enabled
❌ Any entity with automations (`on_value:`, `on_state:`)
❌ Copy components
❌ Derivative sensors (total_daily_energy, integration, etc.)
❌ Climate/covers with feedback sensors

## Realistic Scenario Analysis

### Scenario 1: Simple API-only device (10 sensors, no MQTT)
**Current:** 10 × 16 = 160 bytes
**Lazy:** 10 × 8 = 80 bytes
**Savings: 80 bytes** ✅

### Scenario 2: MQTT-enabled device (10 sensors with MQTT)
**Current:** 10 × 32 = 320 bytes
**Lazy:** 10 × 36 = 360 bytes
**Cost: 40 bytes** ❌

### Scenario 3: Mixed device (5 API-only, 5 with MQTT)
**Current:** (5 × 16) + (5 × 32) = 80 + 160 = 240 bytes
**Lazy:** (5 × 8) + (5 × 36) = 40 + 180 = 220 bytes
**Savings: 20 bytes** ✅

### Scenario 4: Heavy automation device (10 sensors, MQTT + automation on each)
**Current:** 10 × (32 + 16) = 480 bytes (2 callbacks each)
**Lazy:** 10 × (36 + 16) = 520 bytes
**Cost: 40 bytes** ❌

## Recommendation

### Pros:
✅ Saves 8 bytes per entity without callbacks
✅ Common case: many devices use only API/WebServer after Controller Registry
✅ Minimal code complexity (just nullptr checks)
✅ Negligible performance impact (predictable branch)
✅ Follows existing pattern (raw_callback_ is already lazy)

### Cons:
❌ Costs 4 extra bytes when callbacks ARE used (extra heap allocation)
❌ Adds nullptr check to hot path (publish_state called frequently)
❌ Requires changes to ALL entity base classes (~15+ files)
❌ Users with MQTT enabled pay the 4-byte cost

### Decision Matrix:
**Adopt if:** Most users have API-only devices (no MQTT)
**Skip if:** Most users enable MQTT or use many automations

### Data Needed:
- What % of ESPHome devices use MQTT?
- What % of entities have automations?
- Average entity count per device?

### My Recommendation: **WORTH CONSIDERING**

The savings for API-only devices are real (8 bytes per entity), and with Controller Registry, more devices are API-only. The 4-byte cost for MQTT users is small compared to MQTT's overall overhead (~60+ bytes of config per entity).

**Suggested approach:**
1. Start with Sensor (most common entity type)
2. Measure real-world impact
3. Expand to other entity types if beneficial

**Code pattern:**
```cpp
// Helper macro to reduce boilerplate
#define LAZY_CALLBACK_CALL(callback_ptr, ...) \
  do { if (callback_ptr) { callback_ptr->call(__VA_ARGS__); } } while(0)

void Sensor::internal_send_state_to_frontend(float state) {
  LAZY_CALLBACK_CALL(this->callback_, state);
  #ifdef USE_CONTROLLER_REGISTRY
  ControllerRegistry::notify_sensor_update(this);
  #endif
}
```
