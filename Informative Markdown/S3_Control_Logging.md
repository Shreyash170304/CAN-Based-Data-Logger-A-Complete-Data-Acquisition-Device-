# Real-Time Torque Control + Logging on ESP32-S3

This document explains how to run a torque-feedback PI loop *and* log CAN data at the same time, why it works, and why an ESP32-S3 is preferable to an ESP32-C6. It also outlines a dual-core task architecture, queueing strategy, and SD logging practices.

## Why It Is Possible
- The CAN logger already receives every frame in real time. The torque signal is just one decoded field, so the same receive path can feed a PI controller.
- Control math (PI) is lightweight; even at tens of Hz (or hundreds) it fits easily on the ESP32 CPU budget.
- By separating time-critical control from slower I/O (SD/Wi-Fi) you prevent logging from disturbing the control loop.

## Why Choose ESP32-S3 Over ESP32-C6
| Feature | ESP32-C6 | ESP32-S3 | Why S3 Helps |
| --- | --- | --- | --- |
| CPU cores | 1 | 2 (Xtensa) | Pin control loop to one core, logging/comms to the other ? lower jitter |
| Max clock | ~160?MHz | up to 240?MHz | More headroom for decode + logging bursts |
| RAM | Smaller | Larger | Deeper ring buffers, bigger SD write batches |
| Native USB | No | Yes (CDC/MSC) | Faster log offload / alt comms without Wi-Fi load |
| Peripheral slots | Fewer | More | Easier to keep CAN + SD + UART without pin conflicts |
| Radio | BLE/2.4?GHz | BLE/2.4?GHz | Similar; not decisive |

## High-Level Architecture (S3)
- **Core 0 (High Priority):** CAN RX task + PI control
  - Reads CAN frames (TWAI), decodes torque
  - Runs PI (no dynamic allocation, no delays)
  - Sends actuator command (CAN or UART/RS232) immediately
- **Core 1 (Medium Priority):** Logging + Comms
  - Drains ring buffer of frames/decoded samples
  - Writes to SD in 4–8?KB batches
  - Serves optional Wi-Fi/API/UI if needed
- **Shared Structures:** lock-free ring buffer or FreeRTOS queue for frames/records; minimal copy (struct with timestamp, ID, data[8], decoded torque)

## Detailed Steps
1) **CAN Receive (Core 0):**
   - Use TWAI driver; optional acceptance filter to reduce load.
   - On each frame, if ID == torque-frame, extract raw bits ? torque_Nm = raw * scale + offset.
   - Push a compact record to a ring buffer: `{timestamp_us, id, dlc, data[8], torque}`.

2) **PI Control (Core 0):**
   - Trigger on torque frame arrival or fixed-rate timer.
   - `error = setpoint - torque; integrator += Ki*error*dt; output = Kp*error + integrator; clamp output & integrator;`.
   - Send command frame (CAN) or call PSU/UART setter. Keep it non-blocking.

3) **Logging (Core 1):**
   - Periodically (e.g., every 20–50?ms) drain up to N records from ring buffer.
   - Serialize to CSV line(s) and write in chunks (4–8?KB) to minimize SD card latency.
   - Flush only on batch boundaries or file rotate.

4) **Avoiding Interference:**
   - No SD writes on Core 0.
   - No `delay()` in control path; use timestamp arithmetic or esp_timer.
   - Keep Wi-Fi and UI on Core 1 with lower priority than logging.

5) **Timing Targets:**
   - CAN torque rate: ~20?Hz (from your sensor).
   - Control loop latency: dominated by frame arrival; processing is microseconds.
   - Logging batch interval: 20–50?ms to keep SD overhead amortized.

## Example FreeRTOS Task Partitioning
```cpp
// Pseudocode
void can_control_task(void* arg) { // core 0, high priority
  for (;;) {
    if (twai_receive(&frame, portMAX_DELAY) == ESP_OK) {
      if (frame.identifier == TORQUE_ID) {
        float torque = decode_torque(frame);
        float output = pi_update(torque);
        send_command(output);
      }
      push_to_ring(frame); // minimal copy
    }
  }
}

void logging_task(void* arg) { // core 1, medium priority
  static uint8_t buf[8192];
  for (;;) {
    size_t n = drain_ring_to_csv(buf, sizeof(buf));
    if (n) sd_write(buf, n); // batch write
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
```
Pin `can_control_task` to core 0, `logging_task` to core 1 with `xTaskCreatePinnedToCore`.

## SD Card Practices
- SPI at 20–40?MHz; dedicated CS pin.
- Preallocate files if possible; rotate at size/time boundaries.
- Buffer lines in RAM; flush per batch, not per line.

## What if You Stay on C6?
- It can work, but SD writes and Wi-Fi share the single core with control ? more jitter.
- Mitigate by: disabling Wi-Fi, reducing log rate, or offloading logging to PC via serial.

## Conclusion
- Yes, you can run real-time torque PI control **and** log data simultaneously.
- ESP32-S3 is the better choice: dual cores, higher clock, more RAM, and USB give you room to keep the control loop deterministic while logging at full rate.
