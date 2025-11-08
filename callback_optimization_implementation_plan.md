# Callback Optimization Implementation Plan

## Analysis Summary

After Controller Registry (PR #11772), callback infrastructure can be further optimized:

**Current overhead per entity (ESP32 32-bit):**
- No callbacks: 16 bytes (4-byte ptr + 12-byte empty vector)
- With callbacks: 32+ bytes (16 baseline + 16+ per callback)

**Opportunity:** After Controller Registry, most entities have **zero callbacks** (API/WebServer use registry instead). We can save 12 bytes per entity by lazy allocation.

## Entity Types by Callback Needs

### Entities with ONLY filtered callbacks (most)
- Climate, Fan, Light, Cover
- Switch, Lock, Valve
- Number, Select, Text, Button
- AlarmControlPanel, MediaPlayer
- BinarySensor, Event, Update, DateTime

**Optimization:** Simple lazy-allocated vector

### Entities with raw AND filtered callbacks
- **Sensor** - has raw callbacks for automation triggers
- **TextSensor** - has raw callbacks for automation triggers

**Optimization:** Partitioned vector (filtered | raw)

## Proposed Implementations

### Option 1: Simple Lazy Vector (for entities without raw callbacks)

```cpp
class Climate {
 protected:
  std::unique_ptr<std::vector<std::function<void(Climate&)>>> state_callback_;
};

void Climate::add_on_state_callback(std::function<void(Climate&)> &&callback) {
  if (!this->state_callback_) {
    this->state_callback_ = std::make_unique<std::vector<std::function<void(Climate&)>>>();
  }
  this->state_callback_->push_back(std::move(callback));
}

void Climate::publish_state() {
  if (this->state_callback_) {
    for (auto &cb : *this->state_callback_) {
      cb(*this);
    }
  }
}
```

**Memory (ESP32):**
- No callbacks: 4 bytes (saves 12 vs current)
- 1 callback: 36 bytes (costs 4 vs current)
- Net: Positive for API-only devices

### Option 2: Partitioned Vector (for Sensor & TextSensor)

```cpp
class Sensor {
 protected:
  struct Callbacks {
    std::vector<std::function<void(float)>> callbacks_;
    uint8_t filtered_count_{0};  // Partition point: [filtered | raw]

    void add_filtered(std::function<void(float)> &&fn) {
      callbacks_.push_back(std::move(fn));
      if (filtered_count_ < callbacks_.size() - 1) {
        std::swap(callbacks_[filtered_count_], callbacks_[callbacks_.size() - 1]);
      }
      filtered_count_++;
    }

    void add_raw(std::function<void(float)> &&fn) {
      callbacks_.push_back(std::move(fn));  // Append to raw section
    }

    void call_filtered(float value) {
      for (size_t i = 0; i < filtered_count_; i++) {
        callbacks_[i](value);
      }
    }

    void call_raw(float value) {
      for (size_t i = filtered_count_; i < callbacks_.size(); i++) {
        callbacks_[i](value);
      }
    }
  };

  std::unique_ptr<Callbacks> callbacks_;
};
```

**Why partitioned:**
- Maintains separation of raw (pre-filter) vs filtered (post-filter) callbacks
- O(1) insertion via swap (order doesn't matter)
- No branching in hot path
- Saves 12 bytes when no callbacks

## Memory Impact Analysis

### Scenario 1: API-only device (10 sensors, no MQTT, no automations)
**Current:** 10 × 16 = 160 bytes
**Optimized:** 10 × 4 = 40 bytes
**Saves: 120 bytes** ✅

### Scenario 2: MQTT-enabled device (10 sensors with MQTT)
**Current:** 10 × 32 = 320 bytes
**Optimized:** 10 × 36 = 360 bytes
**Costs: 40 bytes** ⚠️

### Scenario 3: Mixed device (5 API-only + 5 MQTT)
**Current:** (5 × 16) + (5 × 32) = 240 bytes
**Optimized:** (5 × 4) + (5 × 36) = 200 bytes
**Saves: 40 bytes** ✅

### Scenario 4: Sensor with automation (1 raw + 1 filtered)
**Current:** 16 + 12 + 16 + 16 = 60 bytes
**Optimized:** 4 + 16 + 32 = 52 bytes
**Saves: 8 bytes** ✅

## Implementation Strategy

### Phase 1: Simple Entities (high impact, low complexity)
1. **Climate** (common, no raw callbacks)
2. **Fan** (common, no raw callbacks)
3. **Cover** (common, no raw callbacks)
4. **Switch** (very common, no raw callbacks)
5. **Lock** (no raw callbacks)

**Change:** Replace `CallbackManager<void(...)> callback_` with `std::unique_ptr<std::vector<std::function<...>>>`

### Phase 2: Sensor & TextSensor (more complex)
1. **Sensor** (most common entity, has raw callbacks)
2. **TextSensor** (common, has raw callbacks)

**Change:** Implement partitioned vector approach

### Phase 3: Remaining Entities
- BinarySensor, Number, Select, Text
- Light, Valve, AlarmControlPanel
- MediaPlayer, Button, Event, Update, DateTime

**Change:** Simple lazy vector

## Code Template for Simple Entities

```cpp
// Header (.h)
class EntityType {
 public:
  void add_on_state_callback(std::function<void(Args...)> &&callback);

 protected:
  std::unique_ptr<std::vector<std::function<void(Args...)>>> state_callback_;
};

// Implementation (.cpp)
void EntityType::add_on_state_callback(std::function<void(Args...)> &&callback) {
  if (!this->state_callback_) {
    this->state_callback_ = std::make_unique<std::vector<std::function<void(Args...)>>>();
  }
  this->state_callback_->push_back(std::move(callback));
}

void EntityType::publish_state(...) {
  // ... state update logic ...

  if (this->state_callback_) {
    for (auto &cb : *this->state_callback_) {
      cb(...);
    }
  }

#ifdef USE_CONTROLLER_REGISTRY
  ControllerRegistry::notify_entity_update(this);
#endif
}
```

## Testing Strategy

1. **Unit tests:** Verify callback ordering/execution unchanged
2. **Integration tests:** Test with MQTT, automations, copy components
3. **Memory benchmarks:** Measure actual flash/RAM impact
4. **Compatibility:** Ensure no API breakage

## Expected Results

**For typical ESPHome devices after Controller Registry:**
- Most entities: API/WebServer only (no callbacks)
- Some entities: MQTT (1 callback)
- Few entities: Automations (1-2 callbacks)

**Memory savings:**
- Device with 20 entities, 5 with MQTT: ~180 bytes saved
- Device with 50 entities, 10 with MQTT: ~480 bytes saved

**Trade-off:**
- Entities without callbacks: Save 12 bytes ✅
- Entities with callbacks: Cost 4 bytes ⚠️
- Net benefit: Positive for most devices

## Risks & Mitigation

**Risk 1:** Increased complexity
- **Mitigation:** Start with simple entities first, template for reuse

**Risk 2:** Performance regression
- **Mitigation:** Minimal - just nullptr check (likely free with branch prediction)

**Risk 3:** Edge cases with callback order
- **Mitigation:** Order already undefined within same callback type

## Open Questions

1. Should we template the Callbacks struct for reuse across entity types?
2. Should Phase 1 include a memory benchmark before expanding?
3. Should we make this configurable (compile-time flag)?

## Files Modified

### Phase 1 (Simple Entities)
- `esphome/components/climate/climate.h`
- `esphome/components/climate/climate.cpp`
- `esphome/components/fan/fan.h`
- `esphome/components/fan/fan.cpp`
- `esphome/components/cover/cover.h`
- `esphome/components/cover/cover.cpp`
- (etc. for switch, lock)

### Phase 2 (Partitioned)
- `esphome/components/sensor/sensor.h`
- `esphome/components/sensor/sensor.cpp`
- `esphome/components/text_sensor/text_sensor.h`
- `esphome/components/text_sensor/text_sensor.cpp`

### Phase 3 (Remaining)
- All other entity types

## Conclusion

**Recommendation: Implement in phases**

1. Start with Climate (common entity, simple change)
2. Measure impact on real device
3. If positive, proceed with other simple entities
4. Implement partitioned approach for Sensor/TextSensor
5. Complete remaining entity types

Expected net savings: **50-500 bytes per typical device**, depending on entity count and MQTT usage.
