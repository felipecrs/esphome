# Callback Optimization Analysis - Why It Failed

## Goal
Convert stateful lambdas in CallbackManager to stateless function pointers to reduce flash usage.

## Approach Tested

### Attempt 1: Discriminated Union in CallbackManager
**Changed:** `CallbackManager` to use union with discriminator (like `TemplatableValue`)
- Stateless lambdas → function pointer (8 bytes)
- Stateful lambdas → heap-allocated `std::function*` (8 bytes struct + 32 bytes heap)

**Result:**
- ❌ **+300 bytes heap usage** (37-38 callbacks × 8 bytes overhead)
- ✅ Flash savings potential: ~200-400 bytes per stateless callback
- **Verdict:** RAM is more precious than flash on ESP8266 - rejected

### Attempt 2: Convert Individual Callbacks to Stateless
**Changed:** API logger callback from `[this]` lambda to static member function
- Used existing `global_api_server` pointer
- Made callback stateless (convertible to function pointer)

**Result:**
```
Removed:
- Lambda _M_invoke: 103 bytes
- Lambda _M_manager: 20 bytes

Added:
- log_callback function: 104 bytes
- Function pointer _M_invoke: 20 bytes
- Function pointer _M_manager: 20 bytes
- Larger setup(): 7 bytes

Net: +32 bytes flash ❌
```

**Why it failed:**
Even though the callback became stateless, `CallbackManager` still uses `std::vector<std::function<void(Ts...)>>`. The function pointer STILL gets wrapped in `std::function`, generating the same template instantiation overhead. We just moved the code from a lambda to a static function.

## Root Cause

The optimization **requires BOTH**:
1. ✅ Stateless callback (function pointer)
2. ❌ Modified `CallbackManager` to store function pointers directly without `std::function` wrapper

Without modifying `CallbackManager`, converting individual callbacks to function pointers provides **no benefit** and actually **increases** code size slightly due to the extra function definition.

## Conclusion

This optimization path is a **dead end** for ESPHome because:
1. **Discriminated union approach**: Increases heap by 300 bytes (unacceptable for ESP8266)
2. **Individual callback conversion**: Increases flash by 32+ bytes (no benefit without CallbackManager changes)

The current `std::vector<std::function<...>>` approach is already optimal for the use case where most callbacks capture state.

## Alternative Approaches Considered

1. **Create separate `StatelessCallbackManager`**: Would require changing all call sites, not worth the complexity
2. **Template parameter to select storage type**: Same issue - requires modifying many components
3. **Hand-pick specific callbacks**: Provides no benefit as shown in Attempt 2

## Recommendation

**Do not pursue this optimization.** The RAM/flash trade-offs are unfavorable for embedded systems where RAM is typically more constrained than flash.

---

**Test Results:**
- Platform: ESP8266-Arduino
- Component: API
- Result: +32 bytes flash (0.01% increase)
- Status: Reverted

🤖 Analysis by Claude Code
