# Partitioned Callback Vector - Final Proposal

## Design

Use a **single vector** partitioned into filtered and raw sections, managed with **swap** to maintain O(1) insertion:

```cpp
// Layout: [filtered_0, ..., filtered_n-1, raw_0, ..., raw_m-1]
//          ^                               ^
//          0                               filtered_count_
```

## Implementation

### Header (sensor.h)
```cpp
class Sensor : public EntityBase, /* ... */ {
 public:
  void add_on_state_callback(std::function<void(float)> &&callback);
  void add_on_raw_state_callback(std::function<void(float)> &&callback);
  void internal_send_state_to_frontend(float state);
  void publish_state(float state);

 protected:
  struct Callbacks {
    std::vector<std::function<void(float)>> callbacks_;  // 12 bytes
    uint8_t filtered_count_{0};                          // 1 byte (+ 3 padding)
    // Total: 16 bytes on ESP32

    void add_filtered(std::function<void(float)> &&fn) {
      callbacks_.push_back(std::move(fn));
      if (filtered_count_ < callbacks_.size() - 1) {
        // Swap new callback into filtered section
        std::swap(callbacks_[filtered_count_], callbacks_[callbacks_.size() - 1]);
      }
      filtered_count_++;
    }

    void add_raw(std::function<void(float)> &&fn) {
      callbacks_.push_back(std::move(fn));
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

  std::unique_ptr<Callbacks> callbacks_;  // 4 bytes, lazy allocated
};
```

### Implementation (sensor.cpp)
```cpp
void Sensor::add_on_state_callback(std::function<void(float)> &&callback) {
  if (!this->callbacks_) {
    this->callbacks_ = std::make_unique<Callbacks>();
  }
  this->callbacks_->add_filtered(std::move(callback));
}

void Sensor::add_on_raw_state_callback(std::function<void(float)> &&callback) {
  if (!this->callbacks_) {
    this->callbacks_ = std::make_unique<Callbacks>();
  }
  this->callbacks_->add_raw(std::move(callback));
}

void Sensor::publish_state(float state) {
  this->raw_state = state;

  // Call raw callbacks (before filters)
  if (this->callbacks_) {
    this->callbacks_->call_raw(state);
  }

  ESP_LOGV(TAG, "'%s': Received new state %f", this->name_.c_str(), state);

  // ... filter logic ...
}

void Sensor::internal_send_state_to_frontend(float state) {
  this->set_has_state(true);
  this->state = state;

  ESP_LOGD(TAG, "'%s': Sending state %.5f %s with %d decimals of accuracy",
           this->get_name().c_str(), state, this->get_unit_of_measurement_ref().c_str(),
           this->get_accuracy_decimals());

  // Call filtered callbacks (after filters)
  if (this->callbacks_) {
    this->callbacks_->call_filtered(state);
  }

#if defined(USE_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_sensor_update(this);
#endif
}
```

## Memory Comparison (ESP32 32-bit)

### Current Implementation
```cpp
std::unique_ptr<CallbackManager<void(float)>> raw_callback_;  // 4 bytes
CallbackManager<void(float)> callback_;                       // 12 bytes
```

| Scenario | Current | Partitioned | Savings |
|----------|---------|-------------|---------|
| No callbacks | 16 bytes | 4 bytes | **12 bytes ✅** |
| 1 filtered (MQTT) | 32 bytes | 33 bytes | -1 byte ⚠️ |
| 2 filtered | 48 bytes | 49 bytes | -1 byte ⚠️ |
| 1 raw + 1 filtered | 60 bytes | 49 bytes | **11 bytes ✅** |
| 2 raw + 2 filtered | 92 bytes | 65 bytes | **27 bytes ✅** |

Wait, let me recalculate this more carefully...

### Corrected Memory Analysis

**Current:**
- No callbacks: 16 bytes (4 ptr + 12 vec)
- 1 filtered: 16 + 16 (fn) = 32 bytes
- 2 filtered: 16 + 32 (2 fns) = 48 bytes
- 1 raw + 1 filtered: 16 + 12 (raw vec) + 16 (raw fn) + 16 (filtered fn) = 60 bytes

**Partitioned:**
- No callbacks: 4 bytes (nullptr)
- 1 filtered: 4 (ptr) + 16 (Callbacks struct) + 16 (fn) = 36 bytes
- 2 filtered: 4 + 16 + 32 (2 fns) = 52 bytes
- 1 raw + 1 filtered: 4 + 16 + 32 (2 fns) = 52 bytes

Hmm, the struct is 16 bytes (12 vec + 1 count + 3 padding), so:

Actually on ESP32:
- std::vector = 12 bytes (3 pointers)
- uint8_t = 1 byte
- padding = 3 bytes (to align to 4)
- Total struct: 16 bytes

But when heap allocated, the struct size is what matters for memory consumption. Let me revise:

**Partitioned (heap-allocated Callbacks struct):**
- Callbacks struct on heap: 12 (vector struct) + 1 (count) + 3 (padding) = 16 bytes
- Vector data on heap: N × 16 bytes for N callbacks

So:
- No callbacks: 4 bytes (nullptr) ✅ SAVES 12
- 1 filtered: 4 (ptr) + 16 (struct) + 16 (fn) = 36 bytes ⚠️ COSTS 4
- 2 filtered: 4 + 16 + 32 = 52 bytes ⚠️ COSTS 4
- 1 raw + 1 filtered: 4 + 16 + 32 = 52 bytes ✅ SAVES 8

Actually wait - in the current implementation, when we have raw + filtered, we have:
- 16 bytes base
- 12 bytes for raw CallbackManager (heap allocated)
- 16 bytes for raw std::function
- 16 bytes for filtered std::function
= 60 bytes total

With partitioned:
- 4 bytes (ptr)
- 16 bytes (Callbacks struct on heap)
- 16 bytes (raw fn)
- 16 bytes (filtered fn)
= 52 bytes total

SAVES 8 bytes ✅

Let me make a cleaner table:

| Scenario | Current | Partitioned | Savings |
|----------|---------|-------------|---------|
| No callbacks | 16 | 4 | **+12 ✅** |
| 1 filtered only | 32 | 36 | **-4 ⚠️** |
| 1 raw only | 44 | 36 | **+8 ✅** |
| 1 raw + 1 filtered | 60 | 52 | **+8 ✅** |
| 2 filtered only | 48 | 52 | **-4 ⚠️** |
| 10 API-only sensors | 160 | 40 | **+120 ✅** |
| 10 MQTT sensors | 320 | 360 | **-40 ⚠️** |

## Performance Characteristics

### Time Complexity
- `add_filtered()`: **O(1)** - append + swap
- `add_raw()`: **O(1)** - append
- `call_filtered()`: **O(n)** - iterate filtered section
- `call_raw()`: **O(m)** - iterate raw section

### Hot Path (publish_state)
**Before:**
```cpp
if (this->callback_) {
  this->callback_.call(state);  // Direct call
}
```

**After:**
```cpp
if (this->callbacks_) {
  for (size_t i = 0; i < callbacks_->filtered_count_; i++) {
    callbacks_->callbacks_[i](state);
  }
}
```

**Performance impact:**
- Adds nullptr check (already present for raw_callback_)
- Loop is tight, no branching inside
- Better cache locality than separate vectors
- Negligible impact for 0-2 callbacks (typical case)

## Advantages

1. ✅ **Best memory savings**: 12 bytes per entity without callbacks
2. ✅ **O(1) insertion**: Both filtered and raw use append (+ optional swap)
3. ✅ **No branching**: Hot path has no `if (type == FILTERED)` checks
4. ✅ **Cache friendly**: Callbacks stored contiguously
5. ✅ **Simple**: One container instead of two
6. ✅ **Minimal overhead**: Only 1 byte (+ padding) for partition count

## Disadvantages

1. ⚠️ **Costs 4 bytes** for entities with callbacks (vs current)
   - But saves 12 bytes for entities WITHOUT callbacks (more common after Controller Registry)

2. ⚠️ **Swap on filtered insertion**
   - Only during setup(), not runtime
   - O(1) operation
   - Negligible impact

3. ⚠️ **Order not preserved** within each section
   - Not a problem - callback order doesn't matter
   - MQTT and automation callbacks are independent

## Recommendation

**IMPLEMENT THIS!**

The partitioned vector with swap is the optimal design because:
- After Controller Registry, most entities have 0 callbacks (12-byte savings)
- Entities with callbacks pay only 4 extra bytes
- O(1) operations, no performance degradation
- Cleaner, simpler code

**Migration strategy:**
1. Implement for Sensor first
2. Measure real-world impact on flash/RAM
3. Apply to BinarySensor, TextSensor
4. Expand to other entity types (Climate, Fan, etc.)

## Code Reusability

The `Callbacks` struct can be templated for reuse across entity types:

```cpp
template<typename... Args>
struct PartitionedCallbacks {
  std::vector<std::function<void(Args...)>> callbacks_;
  uint8_t filtered_count_{0};

  void add_filtered(std::function<void(Args...)> &&fn) { /* ... */ }
  void add_raw(std::function<void(Args...)> &&fn) { /* ... */ }
  void call_filtered(Args... args) { /* ... */ }
  void call_raw(Args... args) { /* ... */ }
};

// Usage in different entity types:
class Sensor {
  std::unique_ptr<PartitionedCallbacks<float>> callbacks_;
};

class TextSensor {
  std::unique_ptr<PartitionedCallbacks<std::string>> callbacks_;
};

class Climate {
  std::unique_ptr<PartitionedCallbacks<Climate&>> callbacks_;
};
```
