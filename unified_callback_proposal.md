# Unified Callback Storage - Proposal

## Current Implementation (ESP32 32-bit)

```cpp
class Sensor {
 protected:
  std::unique_ptr<CallbackManager<void(float)>> raw_callback_;  // 4 bytes
  CallbackManager<void(float)> callback_;                       // 12 bytes
};
```

**Memory costs:**
- No callbacks: **16 bytes**
- 1 filtered callback (MQTT): **32 bytes** (16 + 16 function)
- 1 raw + 1 filtered: **60 bytes** (16 + 12 + 16 + 16)

## Proposed: Single Vector with Type Flag

```cpp
class Sensor {
 protected:
  enum CallbackType : uint8_t { RAW, FILTERED };

  struct CallbackEntry {
    CallbackType type;              // 1 byte (+ 3 bytes padding)
    std::function<void(float)> fn;  // 16 bytes
    // Total: 20 bytes per callback
  };

  std::unique_ptr<std::vector<CallbackEntry>> callbacks_;  // 4 bytes, lazy allocated
};
```

**Memory costs:**
- No callbacks: **4 bytes** ✅ **SAVES 12 BYTES**
- 1 filtered callback (MQTT): **36 bytes** (4 + 12 vector + 20 entry) ⚠️ **COSTS 4 BYTES**
- 1 raw + 1 filtered: **56 bytes** (4 + 12 + 20 + 20) ✅ **SAVES 4 BYTES**

## Alternative: Two Vectors in One Struct

```cpp
struct SensorCallbacks {
  std::vector<std::function<void(float)>> raw_callbacks;      // 12 bytes
  std::vector<std::function<void(float)>> filtered_callbacks; // 12 bytes
  // Total: 24 bytes overhead
};

class Sensor {
 protected:
  std::unique_ptr<SensorCallbacks> callbacks_;  // 4 bytes, lazy allocated
};
```

**Memory costs:**
- No callbacks: **4 bytes** ✅ **SAVES 12 BYTES**
- 1 filtered callback: **44 bytes** (4 + 24 struct + 16 function) ❌ **COSTS 12 BYTES**
- 1 raw + 1 filtered: **60 bytes** (4 + 24 + 16 + 16) ✅ **SAME**

## Recommendation: Single Vector with Type

The single vector approach is better because:
1. ✅ Same 12-byte savings when no callbacks (most common after Controller Registry)
2. ✅ Only 4-byte cost when callbacks ARE used (vs 12-byte cost for two-vectors)
3. ✅ Simpler: one vector to manage instead of two
4. ✅ Better cache locality: callbacks stored together

## Implementation

### Header
```cpp
class Sensor : public EntityBase, /* ... */ {
 public:
  void add_on_state_callback(std::function<void(float)> &&callback);
  void add_on_raw_state_callback(std::function<void(float)> &&callback);
  void internal_send_state_to_frontend(float state);

 protected:
  enum CallbackType : uint8_t { RAW, FILTERED };

  struct CallbackEntry {
    CallbackType type;
    std::function<void(float)> fn;
  };

  std::unique_ptr<std::vector<CallbackEntry>> callbacks_;

  // Helper to allocate on first use
  void ensure_callbacks_() {
    if (!this->callbacks_) {
      this->callbacks_ = std::make_unique<std::vector<CallbackEntry>>();
    }
  }
};
```

### Implementation
```cpp
void Sensor::add_on_state_callback(std::function<void(float)> &&callback) {
  this->ensure_callbacks_();
  this->callbacks_->push_back({FILTERED, std::move(callback)});
}

void Sensor::add_on_raw_state_callback(std::function<void(float)> &&callback) {
  this->ensure_callbacks_();
  this->callbacks_->push_back({RAW, std::move(callback)});
}

void Sensor::internal_send_state_to_frontend(float state) {
  // Call filtered callbacks
  if (this->callbacks_) {
    for (auto &entry : *this->callbacks_) {
      if (entry.type == FILTERED) {
        entry.fn(state);
      }
    }
  }

#ifdef USE_CONTROLLER_REGISTRY
  ControllerRegistry::notify_sensor_update(this);
#endif
}

void Sensor::publish_state(float state) {
  // Call raw callbacks first
  if (this->callbacks_) {
    for (auto &entry : *this->callbacks_) {
      if (entry.type == RAW) {
        entry.fn(state);
      }
    }
  }

  // Then filters, which eventually call internal_send_state_to_frontend
  // ... existing filter logic ...
}
```

## Performance Impact

**Hot path (publish_state):**
- Before: Direct vector iteration
- After: Vector iteration with type check (`if (entry.type == FILTERED)`)
- Cost: ~1 CPU cycle per callback (trivial branch)
- Most entities have 0-2 callbacks, so negligible

**Cold path (add_on_state_callback):**
- Extra check + possible allocation
- Only happens during setup()
- Negligible impact

## Migration Path

1. **Start with Sensor** (most common entity type)
2. Measure real-world impact
3. Apply to other entities: BinarySensor, TextSensor, Climate, Fan, etc.

## Real-World Scenarios

### Scenario 1: API-only device (10 sensors, no MQTT)
**Before:** 10 × 16 = 160 bytes
**After:** 10 × 4 = 40 bytes
**Saves: 120 bytes** ✅

### Scenario 2: MQTT-enabled (10 sensors)
**Before:** 10 × 32 = 320 bytes
**After:** 10 × 36 = 360 bytes
**Costs: 40 bytes** ⚠️

### Scenario 3: Mixed (5 API-only + 5 MQTT)
**Before:** (5 × 16) + (5 × 32) = 240 bytes
**After:** (5 × 4) + (5 × 36) = 200 bytes
**Saves: 40 bytes** ✅

### Scenario 4: Heavy automation (10 sensors, MQTT + automation each)
**Before:** 10 × (16 + 16 + 16) = 480 bytes  (base + MQTT fn + automation fn)
**After:** 10 × (4 + 12 + 20 + 20) = 560 bytes  (ptr + vector + 2 entries)
**Costs: 80 bytes** ⚠️

Wait, let me recalculate this more carefully...

Actually with 2 callbacks:
**Before:** 16 (base) + 16 (MQTT fn) + 16 (automation fn) = 48 bytes per sensor
**After:** 4 (ptr) + 12 (vector) + 20 (MQTT entry) + 20 (automation entry) = 56 bytes per sensor
**Costs: 8 bytes per sensor with 2+ callbacks** ⚠️

### Revised Decision Matrix

**Wins:**
- Entities with 0 callbacks: Save 12 bytes (most common after Controller Registry)
- Entities with 2+ different types (raw + filtered): Save 4 bytes

**Loses:**
- Entities with 1 callback: Cost 4 bytes
- Entities with 2+ callbacks of same type: Cost 8 bytes

**Net benefit:** Positive if >75% of entities have 0 callbacks

## Conclusion

**RECOMMENDED** for implementation because:
1. ✅ After Controller Registry, most entities are API-only (0 callbacks)
2. ✅ 12-byte savings per entity adds up quickly (120 bytes for 10 sensors)
3. ✅ 4-8 byte cost for MQTT users is acceptable (MQTT already has ~60+ bytes overhead)
4. ✅ Simpler code: one container instead of two
5. ✅ Negligible performance impact (predictable branch on hot path)

**Start with Sensor, measure, then expand to other entity types.**
