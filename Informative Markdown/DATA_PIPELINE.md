# Data Pipeline

This diagram shows the end-to-end flow from CAN bus to decoded outputs.

```mermaid
flowchart TD
  A[CAN Bus Frames] --> B[TJA1050 Transceiver]
  B --> C[ESP32 TWAI Receive]
  C --> D[Timestamp + IMU Sample]
  D --> E[CSV Row]
  E --> F[Encrypt Stream]
  F --> G[SD .NXT File]
  G --> H[Download via Web UI]
  H --> I[Decrypt to CSV]
  I --> J[DBC Decode]
  J --> K[Exports (CSV/XLSX/JSON/etc)]
```
