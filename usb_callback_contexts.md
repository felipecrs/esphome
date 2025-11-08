# USB Host Component - Callback Execution Contexts

## Overview
After the refactoring to use a dedicated USB task, all USB callbacks now execute in the USB task context, NOT in the main loop. This prevents race conditions and data corruption.

## USB Task Architecture
```cpp
// USB Task (runs on Core 1, priority 5)
void USBClient::usb_task_loop() {
  while (usb_task_running_) {
    // This handles ALL USB events and triggers callbacks
    usb_host_client_handle_events(handle_, pdMS_TO_TICKS(10));
  }
}
```

## Callback Execution Contexts

### 1. **client_event_cb** (Device Connect/Disconnect)
- **Context**: USB task
- **Triggered by**: USB device connection/disconnection events
- **What it does**: Queues events to main loop via FreeRTOS queue
- **Data flow**: USB hardware → USB task → Queue → Main loop

### 2. **control_callback** (Control Transfers)
- **Context**: USB task
- **Triggered by**: Completion of USB control transfers
- **What it does**: Queues callback execution to main loop
- **Data flow**: USB hardware → USB task → Queue → Main loop

### 3. **transfer_callback** (Bulk Transfers)
- **Context**: USB task
- **Triggered by**: Completion of bulk IN/OUT transfers
- **What it does**: Queues callback execution to main loop
- **Data flow**: USB hardware → USB task → Queue → Main loop

### 4. **USBUartComponent Input Callback** (Data Reception)
- **Context**: USB task (lambda passed to transfer_in)
- **Called from**: transfer_callback in USB task
- **What it does**:
  - Copies received data to temporary buffer
  - Queues data processing to main loop via defer()
  - Main loop then safely pushes to ring buffer
- **Data flow**: USB hardware → USB task → Copy data → Queue → Main loop → Ring buffer

### 5. **USBUartComponent Output Callback** (Data Transmission)
- **Context**: USB task (lambda passed to transfer_out)
- **Called from**: transfer_callback in USB task
- **What it does**:
  - Marks output as not started
  - Queues next transfer start to main loop via defer()
- **Data flow**: Main loop reads ring buffer → Start transfer → USB task → Queue restart → Main loop

## Thread Safety Summary

### Safe Operations (Main Loop Only):
- Ring buffer push (incoming data)
- Ring buffer pop (outgoing data)
- Component state changes
- Transfer initiation

### USB Task Operations:
- USB event handling
- Transfer completion detection
- Event queueing to main loop

### Communication Mechanism:
- **FreeRTOS Queue**: For USB events and callbacks
- **defer()**: For component-specific operations

## Why This Architecture?

1. **Prevents data loss**: USB events are handled promptly even when main loop is busy
2. **Thread safety**: Ring buffer is only accessed from main loop
3. **No race conditions**: Data structures aren't shared between tasks
4. **Maintains responsiveness**: USB hardware FIFOs don't overflow

## Key Changes from Original Implementation

### Before (Problematic):
- `usb_host_client_handle_events()` called in main loop
- Callbacks executed in main loop context
- USB events dropped when main loop was busy
- Ring buffer corruption when data arrived during slow processing

### After (Fixed):
- `usb_host_client_handle_events()` runs in dedicated task
- Callbacks execute in USB task, queue work to main loop
- USB events always handled promptly
- Ring buffer only accessed from main loop (thread-safe)
