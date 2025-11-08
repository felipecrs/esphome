# CallbackManager Optimization Plan

**Note:** ESPHome uses C++20 (gnu++20), so implementations leverage modern C++ features:
- **Concepts** for type constraints and better error messages
- **Designated initializers** for cleaner struct initialization
- **consteval** for compile-time validation
- **Requires clauses** for inline constraints

## Current State

### Memory Profile (ESP32 - 32-bit)

```cpp
sizeof(std::function<void(T)>):      32 bytes
sizeof(void*):                        4 bytes
sizeof(function pointer):             4 bytes
```

### Current Implementation

```cpp
template<typename... Ts> class CallbackManager<void(Ts...)> {
 public:
  void add(std::function<void(Ts...)> &&callback) {
    this->callbacks_.push_back(std::move(callback));
  }

  void call(Ts... args) {
    for (auto &cb : this->callbacks_)
      cb(args...);
  }

  size_t size() const { return this->callbacks_.size(); }

 protected:
  std::vector<std::function<void(Ts...)>> callbacks_;
};
```

### Memory Cost Per Instance

- **Per callback:** 32 bytes (std::function storage)
- **Vector reallocation code:** ~132 bytes (`_M_realloc_append` template instantiation)
- **Example (1 callback):** 32 + 132 = 164 bytes

### Codebase Usage

- **Total CallbackManager instances:** ~67 files
- **Estimated total callbacks:** 100-150 across all components
- **Examples:**
  - `sensor.h`: `CallbackManager<void(float)>` - multiple callbacks per sensor
  - `esp32_ble_tracker.h`: `CallbackManager<void(ScannerState)>` - 1 callback (bluetooth_proxy)
  - `esp32_improv.h`: `CallbackManager<void(State, Error)>` - up to 5 callbacks (automation triggers)
  - `climate.h`: `CallbackManager<void()>` - multiple callbacks for state/control

### Current Usage Pattern

All callbacks currently use lambda captures:

```cpp
// bluetooth_proxy.cpp
parent_->add_scanner_state_callback([this](ScannerState state) {
  if (this->api_connection_ != nullptr) {
    this->send_bluetooth_scanner_state_(state);
  }
});

// sensor.cpp (via automation)
sensor->add_on_state_callback([this](float state) {
  this->trigger(state);
});
```

---

## Optimization Options

### Option 1: Function Pointer + Context (Recommended)

**C++20 Implementation (Type-Safe with Concepts):**

```cpp
#include <concepts>
#include <type_traits>

// Concept to validate callback signature
template<typename F, typename Context, typename... Ts>
concept CallbackFunction = requires(F func, Context* ctx, Ts... args) {
  { func(ctx, args...) } -> std::same_as<void>;
};

template<typename... Ts>
class CallbackManager<void(Ts...)> {
 private:
  struct Callback {
    void (*invoker)(void*, Ts...);  // 4 bytes - type-erased invoker
    void* context;                   // 4 bytes - captured context
    // Total: 8 bytes
  };

  // Type-safe invoker template - knows real context type
  template<typename Context>
  static void invoke(void* ctx, Ts... args) {
    auto typed_func = reinterpret_cast<void(*)(Context*, Ts...)>(
      *static_cast<void**>(ctx)
    );
    auto typed_ctx = static_cast<Context*>(
      *reinterpret_cast<void**>(static_cast<char*>(ctx) + sizeof(void*))
    );
    typed_func(typed_ctx, args...);
  }

  std::vector<Callback> callbacks_;

 public:
  // Type-safe registration with concept constraint
  template<typename Context>
    requires CallbackFunction<void(*)(Context*, Ts...), Context, Ts...>
  void add(void (*func)(Context*, Ts...), Context* context) {
    // Use designated initializers (C++20)
    callbacks_.push_back({
      .invoker = [](void* storage, Ts... args) {
        // Extract function pointer and context from packed storage
        void* func_and_ctx[2];
        std::memcpy(func_and_ctx, storage, sizeof(func_and_ctx));

        auto typed_func = reinterpret_cast<void(*)(Context*, Ts...)>(func_and_ctx[0]);
        auto typed_ctx = static_cast<Context*>(func_and_ctx[1]);
        typed_func(typed_ctx, args...);
      },
      .context = nullptr  // Will store packed data
    });

    // Pack function pointer and context into the callback storage
    void* func_and_ctx[2] = { reinterpret_cast<void*>(func), context };
    std::memcpy(&callbacks_.back(), func_and_ctx, sizeof(func_and_ctx));
  }

  void call(Ts... args) {
    for (auto& cb : callbacks_) {
      cb.invoker(&cb, args...);
    }
  }

  constexpr size_t size() const { return callbacks_.size(); }
};
```

**Cleaner C++20 Implementation (12 bytes, simpler):**

```cpp
template<typename... Ts>
class CallbackManager<void(Ts...)> {
 private:
  struct Callback {
    void (*invoker)(void*, void*, Ts...);  // 4 bytes - generic invoker
    void* func_ptr;                         // 4 bytes - actual function
    void* context;                          // 4 bytes - context
    // Total: 12 bytes (still 20 bytes saved vs std::function!)
  };

  template<typename Context>
  static consteval auto make_invoker() {
    return +[](void* func, void* ctx, Ts... args) {
      auto typed_func = reinterpret_cast<void(*)(Context*, Ts...)>(func);
      typed_func(static_cast<Context*>(ctx), args...);
    };
  }

  std::vector<Callback> callbacks_;

 public:
  // C++20 concepts for type safety
  template<typename Context>
    requires std::invocable<void(*)(Context*, Ts...), Context*, Ts...>
  void add(void (*func)(Context*, Ts...), Context* context) {
    // C++20 designated initializers
    callbacks_.push_back({
      .invoker = make_invoker<Context>(),
      .func_ptr = reinterpret_cast<void*>(func),
      .context = context
    });
  }

  void call(Ts... args) {
    for (auto& cb : callbacks_) {
      cb.invoker(cb.func_ptr, cb.context, args...);
    }
  }

  constexpr size_t size() const { return callbacks_.size(); }
  constexpr bool empty() const { return callbacks_.empty(); }
};
```

**Most Efficient C++20 Implementation (8 bytes):**

```cpp
template<typename... Ts>
class CallbackManager<void(Ts...)> {
 private:
  struct Callback {
    void (*invoker)(void*, Ts...);  // 4 bytes
    void* context;                   // 4 bytes
    // Total: 8 bytes - maximum savings!
  };

  // C++20: consteval ensures compile-time evaluation
  template<typename Context>
  static consteval auto make_invoker() {
    // The + forces decay to function pointer
    return +[](void* ctx, Ts... args) {
      // Unpack the storage struct
      struct Storage {
        void (*func)(Context*, Ts...);
        Context* context;
      };
      auto* storage = static_cast<Storage*>(ctx);
      storage->func(storage->context, args...);
    };
  }

  std::vector<Callback> callbacks_;

 public:
  template<typename Context>
    requires std::invocable<void(*)(Context*, Ts...), Context*, Ts...>
  void add(void (*func)(Context*, Ts...), Context* context) {
    // Allocate storage for function + context
    struct Storage {
      void (*func)(Context*, Ts...);
      Context* context;
    };

    auto* storage = new Storage{func, context};

    callbacks_.push_back({
      .invoker = make_invoker<Context>(),
      .context = storage
    });
  }

  ~CallbackManager() {
    // Clean up storage
    for (auto& cb : callbacks_) {
      delete static_cast<void*>(cb.context);
    }
  }

  void call(Ts... args) {
    for (auto& cb : callbacks_) {
      cb.invoker(cb.context, args...);
    }
  }

  constexpr size_t size() const { return callbacks_.size(); }
};
```

**Simplest C++20 Implementation (Recommended):**

```cpp
template<typename... Ts>
class CallbackManager<void(Ts...)> {
 private:
  struct Callback {
    void (*invoker)(void*, void*, Ts...);  // 4 bytes
    void* func_ptr;                         // 4 bytes
    void* context;                          // 4 bytes
    // Total: 12 bytes
  };

  template<typename Context>
  static void invoke(void* func, void* ctx, Ts... args) {
    reinterpret_cast<void(*)(Context*, Ts...)>(func)(static_cast<Context*>(ctx), args...);
  }

  std::vector<Callback> callbacks_;

 public:
  template<typename Context>
    requires std::invocable<void(*)(Context*, Ts...), Context*, Ts...>
  void add(void (*func)(Context*, Ts...), Context* context) {
    callbacks_.push_back({
      .invoker = &invoke<Context>,
      .func_ptr = reinterpret_cast<void*>(func),
      .context = context
    });
  }

  void call(Ts... args) {
    for (auto& cb : callbacks_) {
      cb.invoker(cb.func_ptr, cb.context, args...);
    }
  }

  constexpr size_t size() const { return callbacks_.size(); }
};
```

**C++20 Benefits:**
- ✅ **Concepts** provide clear compile errors
- ✅ **Designated initializers** make code more readable
- ✅ **consteval** ensures compile-time evaluation
- ✅ **constexpr** improvements allow more compile-time validation
- ✅ **Requires clauses** document constraints inline

**Usage Changes:**

```cpp
// OLD (lambda):
parent_->add_scanner_state_callback([this](ScannerState state) {
  if (this->api_connection_ != nullptr) {
    this->send_bluetooth_scanner_state_(state);
  }
});

// NEW (static function + context):
static void scanner_state_callback(BluetoothProxy* proxy, ScannerState state) {
  if (proxy->api_connection_ != nullptr) {
    proxy->send_bluetooth_scanner_state_(state);
  }
}

// Registration
parent_->add_scanner_state_callback(scanner_state_callback, this);
```

**Savings:**
- **Per callback:** 24 bytes (32 → 8) or 20 bytes (32 → 12 for simpler version)
- **RAM saved (100-150 callbacks):** 2.4 - 3.6 KB
- **Flash saved:** ~5-10 KB (eliminates std::function template instantiations)

**Pros:**
- ✅ Maximum memory savings (75% reduction)
- ✅ Type-safe at registration time
- ✅ No virtual function overhead
- ✅ Works with all capture patterns
- ✅ Simple implementation

**Cons:**
- ❌ Requires converting lambdas to static functions
- ❌ Changes API for all 67 CallbackManager users
- ❌ More verbose at call site

---

### Option 2: Member Function Pointers

**Implementation:**

```cpp
template<typename... Ts>
class CallbackManager<void(Ts...)> {
 private:
  struct Callback {
    void (*invoker)(void*, Ts...);  // 4 bytes
    void* obj;                       // 4 bytes
    // Total: 8 bytes
  };

  template<typename T, void (T::*Method)(Ts...)>
  static void invoke_member(void* obj, Ts... args) {
    (static_cast<T*>(obj)->*Method)(args...);
  }

  std::vector<Callback> callbacks_;

 public:
  // Register a member function
  template<typename T, void (T::*Method)(Ts...)>
  void add(T* obj) {
    callbacks_.push_back({
      &invoke_member<T, Method>,
      obj
    });
  }

  void call(Ts... args) {
    for (auto& cb : callbacks_) {
      cb.invoker(cb.obj, args...);
    }
  }

  size_t size() const { return callbacks_.size(); }
};
```

**Usage Changes:**

```cpp
// Add a method to BluetoothProxy
void BluetoothProxy::on_scanner_state_changed(ScannerState state) {
  if (this->api_connection_ != nullptr) {
    this->send_bluetooth_scanner_state_(state);
  }
}

// Register it
parent_->add_scanner_state_callback<BluetoothProxy,
                                     &BluetoothProxy::on_scanner_state_changed>(this);
```

**Savings:**
- **Per callback:** 24 bytes (32 → 8)
- **RAM saved:** 2.4 - 3.6 KB
- **Flash saved:** ~5-10 KB

**Pros:**
- ✅ Same memory savings as Option 1
- ✅ Most type-safe (member function pointers)
- ✅ No static functions needed
- ✅ Clean separation of callback logic

**Cons:**
- ❌ Verbose syntax at registration: `add<Type, &Type::method>(this)`
- ❌ Requires adding methods to classes
- ❌ Can't capture additional state beyond `this`
- ❌ Template parameters at call site are ugly

---

### Option 3: Hybrid (Backward Compatible)

**Implementation:**

```cpp
template<typename... Ts>
class CallbackManager<void(Ts...)> {
 private:
  struct Callback {
    void (*invoker)(void*, Ts...);  // 4 bytes
    void* data;                      // 4 bytes
    bool is_std_function;            // 1 byte + 3 padding = 4 bytes
    // Total: 12 bytes
  };

  std::vector<Callback> callbacks_;

 public:
  // Optimized: function pointer + context
  template<typename Context>
  void add(void (*func)(Context*, Ts...), Context* context) {
    callbacks_.push_back({
      [](void* ctx, Ts... args) {
        auto cb = static_cast<Callback*>(ctx);
        auto typed_func = reinterpret_cast<void(*)(Context*, Ts...)>(cb->data);
        auto typed_ctx = static_cast<Context*>(*reinterpret_cast<void**>(
          static_cast<char*>(cb) + offsetof(Callback, data)
        ));
        typed_func(typed_ctx, args...);
      },
      reinterpret_cast<void*>(func),
      false
    });
  }

  // Legacy: std::function support (for gradual migration)
  void add(std::function<void(Ts...)>&& func) {
    auto* stored = new std::function<void(Ts...)>(std::move(func));
    callbacks_.push_back({
      [](void* ctx, Ts... args) {
        (*static_cast<std::function<void(Ts...)>*>(ctx))(args...);
      },
      stored,
      true
    });
  }

  ~CallbackManager() {
    for (auto& cb : callbacks_) {
      if (cb.is_std_function) {
        delete static_cast<std::function<void(Ts...)>*>(cb.data);
      }
    }
  }

  void call(Ts... args) {
    for (auto& cb : callbacks_) {
      cb.invoker(&cb, args...);
    }
  }

  size_t size() const { return callbacks_.size(); }
};
```

**Usage:**

```cpp
// NEW (optimized):
parent_->add_scanner_state_callback(scanner_state_callback, this);

// OLD (still works - gradual migration):
parent_->add_scanner_state_callback([this](ScannerState state) {
  // ... lambda still works
});
```

**Savings:**
- **Per optimized callback:** 20 bytes (32 → 12)
- **Per legacy callback:** 0 bytes (still uses std::function)
- **Allows gradual migration**

**Pros:**
- ✅ Backward compatible
- ✅ Gradual migration path
- ✅ Mix optimized and legacy in same codebase
- ✅ No breaking changes

**Cons:**
- ❌ More complex implementation
- ❌ Need to track which callbacks need cleanup
- ❌ Extra bool field (padding makes it 12 bytes instead of 8)
- ❌ std::function still compiled in

---

### Option 4: FixedVector (Keep std::function, Optimize Vector)

**Implementation:**

```cpp
template<typename... Ts>
class CallbackManager<void(Ts...)> {
 public:
  void add(std::function<void(Ts...)> &&callback) {
    if (this->callbacks_.empty()) {
      // Most CallbackManagers have 1-5 callbacks
      this->callbacks_.init(5);
    }
    this->callbacks_.push_back(std::move(callback));
  }

  void call(Ts... args) {
    for (auto &cb : this->callbacks_)
      cb(args...);
  }

  size_t size() const { return this->callbacks_.size(); }

 protected:
  FixedVector<std::function<void(Ts...)>> callbacks_;  // Changed from std::vector
};
```

**Savings:**
- **Per callback:** 0 bytes (still 32 bytes)
- **Per instance:** ~132 bytes (eliminates `_M_realloc_append`)
- **Flash saved:** ~5-10 KB (one less vector template instantiation per type)
- **Total:** ~132 bytes × ~20 unique callback types = ~2.6 KB

**Pros:**
- ✅ No API changes
- ✅ Drop-in replacement
- ✅ Eliminates vector reallocation machinery
- ✅ Zero migration cost

**Cons:**
- ❌ No per-callback savings
- ❌ std::function still 32 bytes each
- ❌ Must guess max size at runtime
- ❌ Can still overflow if guess is wrong

---

### Option 5: Template Parameter for Storage (Advanced)

**Implementation:**

```cpp
enum class CallbackStorage {
  FUNCTION,      // Use std::function (default, most flexible)
  FUNCTION_PTR   // Use function pointer + context (optimal)
};

template<typename... Ts, CallbackStorage Storage = CallbackStorage::FUNCTION>
class CallbackManager<void(Ts...)> {
 // Specialize implementation based on Storage parameter
};

// Default: std::function (backward compatible)
template<typename... Ts>
class CallbackManager<void(Ts...), CallbackStorage::FUNCTION> {
 protected:
  std::vector<std::function<void(Ts...)>> callbacks_;
  // ... current implementation
};

// Optimized: function pointer + context
template<typename... Ts>
class CallbackManager<void(Ts...), CallbackStorage::FUNCTION_PTR> {
 private:
  struct Callback {
    void (*func)(void*, Ts...);
    void* context;
  };
  std::vector<Callback> callbacks_;
  // ... Option 1 implementation
};
```

**Usage:**

```cpp
// Old components (no changes):
CallbackManager<void(float)> callback_;  // Uses std::function by default

// Optimized components:
CallbackManager<void(ScannerState), CallbackStorage::FUNCTION_PTR> scanner_state_callbacks_;
```

**Savings:**
- **Opt-in per component**
- **Same as Option 1 for optimized components**

**Pros:**
- ✅ Gradual migration
- ✅ No breaking changes
- ✅ Explicit opt-in per component
- ✅ Clear which components are optimized

**Cons:**
- ❌ Complex template metaprogramming
- ❌ Two implementations to maintain
- ❌ Template parameter pollution
- ❌ Harder to understand codebase

---

## Comparison Matrix

| Option | Per-Callback Savings | Flash Savings | API Changes | Complexity | Migration Cost |
|--------|---------------------|---------------|-------------|------------|----------------|
| **1. Function Ptr + Context** | **24 bytes** (75%) | **~10 KB** | Yes | Low | High (67 files) |
| **2. Member Function Ptrs** | **24 bytes** (75%) | **~10 KB** | Yes | Medium | High + class changes |
| **3. Hybrid** | **20 bytes** (opt-in) | **~8 KB** | No | High | Low (gradual) |
| **4. FixedVector** | **0 bytes** | **~3 KB** | No | Low | None |
| **5. Template Parameter** | **24 bytes** (opt-in) | **~10 KB** | Optional | High | Medium |

---

## Migration Effort Estimate

### Option 1 (Function Pointer + Context)

**Files to change:** ~67 files with CallbackManager usage

**Per-file changes:**
1. Convert lambda to static function (5 min)
2. Update registration call (1 min)
3. Test (5 min)

**Estimate:** ~11 min × 67 files = **~12 hours** (assuming some files have multiple callbacks)

**High-impact components to prioritize:**
- `sensor.h` / `sensor.cpp` - many sensor callbacks
- `esp32_ble_tracker.h` - BLE callbacks
- `climate.h` - climate callbacks
- `binary_sensor.h` - binary sensor callbacks

### Option 4 (FixedVector)

**Files to change:** 1 file (`esphome/core/helpers.h`)

**Changes:**
1. Change `std::vector` to `FixedVector` in CallbackManager
2. Initialize with reasonable default size (e.g., 5)
3. Test across codebase

**Estimate:** **~1 hour**

---

## Recommendations

### Immediate Action: Option 4 (FixedVector)

**Why:**
- Zero migration cost
- Immediate ~3 KB flash savings
- No API changes
- Low risk

**Implementation:**
```cpp
template<typename... Ts> class CallbackManager<void(Ts...)> {
 public:
  void add(std::function<void(Ts...)> &&callback) {
    if (this->callbacks_.empty()) {
      this->callbacks_.init(8);  // Most have < 8 callbacks
    }
    this->callbacks_.push_back(std::move(callback));
  }
  // ... rest unchanged
 protected:
  FixedVector<std::function<void(Ts...)>> callbacks_;
};
```

### Long-term: Option 1 (Function Pointer + Context)

**Why:**
- Maximum savings (2.4-3.6 KB RAM + 10 KB flash)
- Clean, simple implementation
- Type-safe
- Well-tested pattern

**Migration Strategy:**
1. Implement new `CallbackManager` in `helpers.h`
2. Migrate high-impact components first:
   - Core components (sensor, binary_sensor, climate)
   - BLE components (esp32_ble_tracker, bluetooth_proxy)
   - Network components (api, mqtt)
3. Create helper macros to reduce boilerplate
4. Migrate remaining components over 2-3 releases

**Helper Macro Example:**
```cpp
// Define a callback wrapper
#define CALLBACK_WRAPPER(Class, Method, ...) \
  static void Method##_callback(Class* self, ##__VA_ARGS__) { \
    self->Method(__VA_ARGS__); \
  }

// In class:
class BluetoothProxy {
  CALLBACK_WRAPPER(BluetoothProxy, on_scanner_state, ScannerState state)

  void on_scanner_state(ScannerState state) {
    // Implementation
  }

  void setup() {
    parent_->add_scanner_state_callback(on_scanner_state_callback, this);
  }
};
```

---

## Testing Plan

### Phase 1: Unit Tests
- Test CallbackManager with various signatures
- Test multiple callbacks (1, 5, 10, 50)
- Test callback removal/cancellation
- Test edge cases (empty, nullptr, etc.)

### Phase 2: Integration Tests
- Create test YAML with heavily-used callbacks
- Run on ESP32, ESP8266, RP2040
- Measure before/after memory usage
- Verify no functional regressions

### Phase 3: Component Tests
- Test high-impact components:
  - sensor with multiple state callbacks
  - esp32_improv with all automation triggers
  - climate with state/control callbacks
- Measure memory with `esphome analyze-memory`

---

## Risk Analysis

### Option 1 Risks

**Risk: Breaking change across 67 files**
- **Mitigation:** Gradual rollout over 2-3 releases
- **Mitigation:** Extensive testing on real hardware

**Risk: Static function verbosity**
- **Mitigation:** Helper macros (see above)
- **Mitigation:** Code generation from Python

**Risk: Missing captures**
- **Mitigation:** Static analysis to find lambda captures
- **Mitigation:** Compile-time errors for incorrect usage

### Option 4 Risks

**Risk: Buffer overflow if size guess is wrong**
- **Mitigation:** Choose conservative default (8)
- **Mitigation:** Add runtime warning on resize
- **Mitigation:** Monitor in CI/testing

**Risk: Still uses std::function (32 bytes each)**
- **Mitigation:** Follow up with Option 1 migration
- **Mitigation:** This is a stepping stone, not final solution

---

## Implementation Timeline

### Week 1: Option 4 (Quick Win)
- Implement FixedVector in CallbackManager
- Test across codebase
- Create PR with memory analysis
- **Expected savings:** ~3 KB flash

### Month 1-2: Option 1 (Core Components)
- Implement function pointer CallbackManager
- Migrate sensor, binary_sensor, climate
- Create helper macros
- **Expected savings:** ~1 KB RAM + 5 KB flash

### Month 3-4: Option 1 (Remaining Components)
- Migrate BLE components
- Migrate network components (api, mqtt)
- Migrate automation components
- **Expected savings:** ~2 KB RAM + 10 KB flash total

### Month 5: Cleanup
- Remove std::function CallbackManager
- Update documentation
- Blog post about optimization

---

## Conclusion

**Recommended Approach:**

1. **Immediate (Week 1):** Implement Option 4 (FixedVector)
   - Low risk, zero migration cost
   - ~3 KB flash savings
   - Sets foundation for Option 1

2. **Short-term (Month 1-2):** Begin Option 1 migration
   - Start with high-impact components
   - ~1-2 KB RAM + 5 KB flash savings
   - Validate approach

3. **Long-term (Month 3-6):** Complete Option 1 migration
   - Migrate all components
   - ~3-4 KB total RAM + 10 KB flash savings
   - Remove std::function variant

**Total Expected Savings:**
- **RAM:** 2.4 - 3.6 KB (75% reduction per callback)
- **Flash:** 8 - 13 KB (vector overhead + template instantiations)
- **Performance:** Slightly faster (no std::function indirection)

This is significant for ESP8266 (80 KB RAM, 1 MB flash) and beneficial for all platforms.
