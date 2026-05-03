# RMT Library for ESP32

A high-level RMT (Remote Control Transceiver) library for **BlueScript** on ESP32.
This package provides a powerful interface to generate precise digital pulse trains. It is highly useful for driving smart LEDs (e.g., WS2812/NeoPixel), transmitting infrared (IR) remote signals, or creating custom 1-wire protocols without blocking the CPU.

## Installation

Install this package in your BlueScript project:

```bash
bscript project install https://github.com/bluescript-lang/pkg-rmt-esp32.git
```

## Usage

### Example 1: Transmitting Bytes (e.g., Smart LEDs / WS2812)
Using `RmtByteEncoder`, you can define how logical `0` and `1` bits are translated into physical pulses. This is perfect for WS2812 LEDs.

```typescript
import { RmtTxChannel, RmtByteEncoder, RmtSymbol } from "rmt";

// 1. Initialize RMT TX Channel on GPIO 12 with 10MHz resolution (1 tick = 0.1 us)
const channel = new RmtTxChannel(12, 10000000);

// 2. Define the symbols for Bit 0 and Bit 1 (WS2812-like protocol)
// Bit 0: HIGH for 4 ticks (0.4us), LOW for 8 ticks (0.8us)
const bit0 = new RmtSymbol(1, 4, 0, 8);
// Bit 1: HIGH for 8 ticks (0.8us), LOW for 4 ticks (0.4us)
const bit1 = new RmtSymbol(1, 8, 0, 4);

// 3. Create a Byte Encoder (MSB first)
const encoder = new RmtByteEncoder(channel, bit0, bit1, true);

// 4. Create color data for 2 LEDs (GRB format: Green, Red, Blue)
const colorData = new Uint8Array(6, 0);
// LED 1: Red
colorData[0] = 0;   // G
colorData[1] = 255; // R
colorData[2] = 0;   // B
// LED 2: Green
colorData[3] = 255; // G
colorData[4] = 0;   // R
colorData[5] = 0;   // B

// 5. Transmit data (loopCount = 0, timeout = 1000ms)
channel.transmit(encoder, colorData, 0, 1000);

// 6. Cleanup
encoder.close();
channel.close();
```

### Example 2: Transmitting Custom Symbols (e.g., IR Remote)
Using `RmtCopyEncoder`, you can define exact timings for each individual high/low pulse.

```typescript
import { RmtTxChannel, RmtCopyEncoder, RmtSymbol } from "rmt";

// 1MHz resolution (1 tick = 1 us)
const channel = new RmtTxChannel(14, 1000000);
const encoder = new RmtCopyEncoder(channel);

// Define raw symbols
const symbols = [
    new RmtSymbol(1, 9000, 0, 4500), // Header mark & space
    new RmtSymbol(1, 560, 0, 1690),  // Logical 1
    new RmtSymbol(1, 560, 0, 560),   // Logical 0
    new RmtSymbol(1, 560, 0, 0)      // Stop bit
];

// Convert symbols to raw byte array
const rawData = encoder.symbolsToUint8Array(symbols);

// Transmit asynchronously and do other tasks
channel.transmitAsync(encoder, rawData, 0);

console.log("Transmitting IR signal in the background...");

// Wait for transmission to finish
channel.waitTransmitCompleted(1000);

encoder.close();
channel.close();
```

### Example 3: Creating a Custom Encoder
You can create a highly customized encoding process by extending the base `RmtEncoder` class. This allows you to hook into the ESP-IDF RMT driver's encoding callbacks directly from BlueScript.

> ⚠️ **Important Limitations for Custom Encoders**
> 
> The `encode` method is executed directly inside an **Hardware Interrupt Service Routine (ISR)**. Because of this, you must strictly follow these rules:
> 1. **Do not block:** Never use `time.delay()` or wait for locks. Execution must be as fast as possible.
> 2. **No heavy processing:** Avoid complex math or heavy loops.
> 3. **Do not allocate memory:** Do not create new objects or arrays inside `encode()` (e.g., `new Uint8Array()`). Allocating memory can trigger the Garbage Collector (GC) inside the interrupt, which will crash the system. Always pre-allocate necessary buffers in the constructor.

```typescript
import { RmtEncoder, RmtTxChannel } from "rmt";

export class MyFastEncoder extends RmtEncoder {
    done: boolean;

    constructor(channel: RmtTxChannel) {
        super(channel); // Automatically registers C-level callbacks
        this.done = false;
    }

    // WARNING: This runs in an ISR context!
    encode(data: Uint8Array): integer {
        // Fast, non-blocking logic only.
        // DO NOT allocate objects here.
        
        this.done = true;
        return 0; // Return the number of encoded symbols.
    }

    isDone(): boolean {
        return this.done;
    }

    reset(): void {
        this.done = false;
    }

    deinit(): void {
        // Cleanup resources
    }
}

// Usage
const channel = new RmtTxChannel(15, 1000000);
const customEncoder = new MyFastEncoder(channel);
channel.transmit(customEncoder, new Uint8Array(4, 0), 0, 1000);
```

## API Reference

### Class: `RmtTxChannel`
Represents an RMT Transmit hardware channel.

#### `constructor(pin: integer, resolutionHz: integer)`
Initializes the RMT TX channel.
- **pin**: The GPIO pin number to output the signal.
- **resolutionHz**: The tick resolution in Hz (e.g., `1000000` for 1µs ticks).

#### `transmit(encoder: RmtEncoder, data: Uint8Array, loopCount: integer, timeoutMs: integer): void`
Encodes and transmits data synchronously. Blocks until transmission is complete or times out.
- **loopCount**: Usually `0` for single transmission. `>0` to repeat the transmission.

#### `transmitAsync(encoder: RmtEncoder, data: Uint8Array, loopCount: integer): void`
Starts transmission in the background (non-blocking). 

#### `waitTransmitCompleted(timeoutMs: integer): void`
Blocks the script until an ongoing asynchronous transmission finishes.

#### `close(): void`
Disables and releases the hardware channel.


### Class: `RmtSymbol`
Represents a single RMT symbol composed of two pulse segments (Duration 0 and Duration 1).

#### `constructor(level0: integer, ticks0: integer, level1: integer, ticks1: integer)`
- **level0**: The logical level of the first pulse (`1` for HIGH, `0` for LOW).
- **ticks0**: Duration of the first pulse in channel ticks.
- **level1**: The logical level of the second pulse.
- **ticks1**: Duration of the second pulse in channel ticks.


### Class: `RmtEncoder` (Base Class)
The base class for all encoders. It internally binds BlueScript methods (`encode`, `isDone`, `reset`, `deinit`) to ESP-IDF hardware callbacks. You can extend this class to implement custom encoding behaviors.


### Class: `RmtByteEncoder` (Extends `RmtEncoder`)
Translates standard byte arrays into predefined `RmtSymbol` representations bit-by-bit.

#### `constructor(channel: RmtTxChannel, bit0: RmtSymbol, bit1: RmtSymbol, msbFirst: boolean)`
- **channel**: The target `RmtTxChannel`.
- **bit0**: The `RmtSymbol` used to represent a binary `0`.
- **bit1**: The `RmtSymbol` used to represent a binary `1`.
- **msbFirst**: `true` to transmit the Most Significant Bit first, `false` for LSB first.


### Class: `RmtCopyEncoder` (Extends `RmtEncoder`)
An encoder that directly copies pre-formatted raw symbols to the RMT channel. Useful for custom or irregular waveforms.

#### `constructor(channel: RmtTxChannel)`
Creates a copy encoder attached to a channel.

#### `symbolsToUint8Array(symbols: RmtSymbol[]): Uint8Array`
Converts an array of `RmtSymbol` objects into the native memory layout (`Uint8Array`) required for transmission. You must pass the result of this function to `channel.transmit()`.


## Enums

### `RmtResult`
Return values representing the status of the last RMT operation.

| Name | Value | Description |
| :--- | :--- | :--- |
| `OK` | 0 | Operation succeeded |
| `Fail` | 1 | Generic failure |
| `Timeout` | 2 | Operation timed out |
| `InvalidArg` | 3 | Invalid argument provided |
| `NoMemory` | 4 | Memory allocation failed |
| `NotFound` | 5 | Resource not found |
| `NotSupported` | 6 | Feature not supported |
| `InvalidState` | 7 | Hardware in invalid state for operation |


## Error Handling (`lastOperationResult`)

Similarly to other standard BlueScript libraries, methods in RMT classes update the `lastOperationResult` property of the object instead of throwing hard errors.

You can verify success by checking `lastOperationResult` immediately after initializing a channel or calling a method.

```typescript
const channel = new RmtTxChannel(12, 1000000);

if (channel.lastOperationResult !== RmtResult.OK) {
    console.log("Failed to initialize RMT channel. Error code:", channel.lastOperationResult);
}
```