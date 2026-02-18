# CAN Bus Connection Guide

## Summary
- CANH/CANL are required for receiving data.
- The transceiver can initialize without being connected to the bus, but no frames will be received.

## Wiring
![CAN Logger Wiring](docs/images/can_logger_wiring.svg)

| ESP32-C6 | TJA1050 |
|---------|---------|
| GPIO17 (CAN TX) | TXD |
| GPIO16 (CAN RX) | RXD |
| GND | GND |
| 5V/3.3V | VCC (per module) |

| TJA1050 | CAN Bus |
|---------|---------|
| CANH | CANH |
| CANL | CANL |

## Termination
- Use 120 ohm termination at each end of the CAN bus.
- If the logger sits at the end of the bus, ensure a termination resistor is present.

## Common Issues
- No frames: CANH/CANL not connected or bus speed mismatch.
- Erratic data: missing termination or poor ground reference.
