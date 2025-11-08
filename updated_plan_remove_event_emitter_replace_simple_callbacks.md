# Plan: Remove EventEmitter and Replace with Simple Callbacks

## Overview

This plan describes the architectural change from using the EventEmitter component to direct callback storage in ESP32 BLE server classes. This simplifies the codebase by removing template metaprogramming and code generation complexity.

## Motivation

The previous EventEmitter-based approach had several issues:
1. **Memory overhead**: Each EventEmitter instance stored a `std::vector` of listeners, even when only 0-1 listeners were needed
2. **Complexity**: Required template metaprogramming with per-UUID specialized classes
3. **Code generation**: Required Python code generation to create specialized classes
4. **Allocation tracking**: Required complex API to pre-allocate listener slots

## New Approach

Replace EventEmitter with direct callback storage using `std::function`:

- Each BLE object (characteristic, descriptor, server) stores callbacks directly as member variables
- Only one callback per event type (sufficient for ESPHome's automation framework)
- No code generation needed
- No allocation tracking needed
- Simpler, more maintainable code

## Implementation Changes

### 1. BLECharacteristic (ble_characteristic.h/cpp)

**Removed:**
- `EventEmitter<BLECharacteristicEvt::VectorEvt, std::vector<uint8_t>, uint16_t>` inheritance
- `EventEmitter<BLECharacteristicEvt::EmptyEvt, uint16_t>` inheritance
- `BLECharacteristicEvt` namespace with event enums
- Virtual `emit_on_write_()` and `emit_on_read_()` methods

**Added:**
- Direct callback registration methods:
  ```cpp
  void on_write(std::function<void(std::span<const uint8_t>, uint16_t)> &&callback);
  void on_read(std::function<void(uint16_t)> &&callback);
  ```
- Callback member variables:
  ```cpp
  std::function<void(std::span<const uint8_t>, uint16_t)> on_write_callback_{nullptr};
  std::function<void(uint16_t)> on_read_callback_{nullptr};
  ```

**Implementation:**
- In `gatts_event_handler()`, replaced `emit_on_write_()` with direct callback invocation:
  ```cpp
  if (this->on_write_callback_) {
    this->on_write_callback_(this->value_, param->write.conn_id);
  }
  ```

### 2. BLEDescriptor (ble_descriptor.h/cpp)

**Removed:**
- `EventEmitter<BLEDescriptorEvt::VectorEvt, std::vector<uint8_t>, uint16_t>` inheritance
- `BLEDescriptorEvt` namespace with event enums
- Virtual `emit_on_write_()` method

**Added:**
- Direct callback registration:
  ```cpp
  void on_write(std::function<void(std::span<const uint8_t>, uint16_t)> &&callback);
  ```
- Callback member variable:
  ```cpp
  std::function<void(std::span<const uint8_t>, uint16_t)> on_write_callback_{nullptr};
  ```

### 3. BLEServer (ble_server.h/cpp)

**Removed:**
- `EventEmitter<BLEServerEvt::EmptyEvt, uint16_t>` inheritance
- `BLEServerEvt` namespace with event enums
- Virtual `emit_on_connect_()` and `emit_on_disconnect_()` methods

**Added:**
- Direct callback registration:
  ```cpp
  void on_connect(std::function<void(uint16_t)> &&callback);
  void on_disconnect(std::function<void(uint16_t)> &&callback);
  ```
- Callback member variables:
  ```cpp
  std::function<void(uint16_t)> on_connect_callback_{nullptr};
  std::function<void(uint16_t)> on_disconnect_callback_{nullptr};
  ```

### 4. BLE Server Automations (ble_server_automations.h/cpp)

**Removed:**
- EventEmitter listener ID tracking (`EventEmitterListenerID`)
- `INVALID_LISTENER_ID` constant
- Complex listener ID management in `BLECharacteristicSetValueActionManager`

**Simplified:**
- `BLECharacteristicSetValueActionManager` now just tracks presence of listener (not ID)
- Uses simple `has_listener()` check instead of ID comparison
- Callback registration uses direct `on_write()` and `on_read()` methods

### 5. Python Code Generation (__init__.py)

**Removed:**
- Entire allocation API (functions to pre-allocate listener slots)
- All code generation for specialized per-UUID classes
- Allocation tracking data structures
- EventEmitter from `AUTO_LOAD`

**Result:**
- ~700 lines of complex code generation removed
- Simpler `to_code()` functions
- No allocation calls needed in component configurations

### 6. EventEmitter Component

**Removed:**
- Entire `esphome/components/event_emitter/` directory deleted
- No longer needed since callbacks are stored directly

### 7. Components Using BLE Server (esp32_improv)

**Changes:**
- Removed EventEmitter allocation calls
- Uses direct callback registration via existing interface
- No other changes needed (uses virtual methods that call callbacks)

## Benefits

1. **Reduced memory usage**: No `std::vector` overhead for each event type
2. **Simpler codebase**: ~700 lines of complex code removed
3. **Easier to maintain**: No template metaprogramming or code generation
4. **Faster compilation**: No specialized classes to generate
5. **Clearer intent**: Direct callbacks are easier to understand than EventEmitter indirection

## Technical Details

### Callback Signature Changes

Using `std::span<const uint8_t>` for zero-copy data passing:
- **Before**: `std::vector<uint8_t>` (requires copy)
- **After**: `std::span<const uint8_t>` (zero-copy view)

### Callback Storage

Using `std::function` with nullptr initialization:
```cpp
std::function<void(...)> callback_{nullptr};
```

Checked before invocation:
```cpp
if (this->callback_) {
  this->callback_(...);
}
```

### Callback Registration

Callbacks moved into storage to avoid copies:
```cpp
void on_write(std::function<void(std::span<const uint8_t>, uint16_t)> &&callback) {
  this->on_write_callback_ = std::move(callback);
}
```

### Lambda Captures

Automation framework registers lambdas with captures:
```cpp
characteristic->on_write([on_write_trigger](std::span<const uint8_t> data, uint16_t id) {
  on_write_trigger->trigger(std::vector<uint8_t>(data.begin(), data.end()), id);
});
```

## Testing

After implementation:
1. Test ESP32 BLE server compilation with various configurations
2. Test esp32_improv component (uses BLE server)
3. Verify memory usage improvements
4. Verify functionality with BLE automations

## Files Modified

- `esphome/components/esp32_ble_server/ble_characteristic.h`
- `esphome/components/esp32_ble_server/ble_characteristic.cpp`
- `esphome/components/esp32_ble_server/ble_descriptor.h`
- `esphome/components/esp32_ble_server/ble_descriptor.cpp`
- `esphome/components/esp32_ble_server/ble_server.h`
- `esphome/components/esp32_ble_server/ble_server.cpp`
- `esphome/components/esp32_ble_server/ble_server_automations.h`
- `esphome/components/esp32_ble_server/ble_server_automations.cpp`
- `esphome/components/esp32_ble_server/__init__.py`
- `esphome/components/esp32_improv/__init__.py`

## Files Deleted

- `esphome/components/event_emitter/` (entire directory)

## Summary

This change dramatically simplifies the ESP32 BLE server implementation while reducing memory usage and improving maintainability. The direct callback approach is more idiomatic C++ and easier to understand than the EventEmitter abstraction.
