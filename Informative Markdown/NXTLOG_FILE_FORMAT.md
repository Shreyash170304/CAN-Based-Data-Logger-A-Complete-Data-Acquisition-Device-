# NXTLOG File Format (Deep Dive)

## Header (16 bytes)
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 6 | Signature | "NXTLOG" |
| 0x06 | 1 | Version | 0x01 |
| 0x07 | 1 | Header Size | 0x10 |
| 0x08 | 4 | Nonce | uint32 LE |
| 0x0C | 4 | Reserved | 0 |

## Cipher Initialization
```text
state = nonce XOR 0xA5A5A5A5
for each key_byte in ENCRYPTION_KEY[0..15]:
    state = (state * 1664525) + 1013904223 + key_byte
```

## Per-Byte Encrypt/Decrypt
```text
state = (state * 1664525) + 1013904223
key_byte = ENCRYPTION_KEY[state & 0x0F]
stream_byte = ((state >> 24) & 0xFF) XOR key_byte
output_byte = input_byte XOR stream_byte
```

Encryption and decryption are identical (XOR with keystream).

## Payload
- Entire CSV payload is encrypted as a continuous stream.
- No per-frame IVs or length fields.
- After decryption, the payload is valid CSV.
- CSV includes IMU linear acceleration, Gravity, and GPS fields.

## Security Note
This is a lightweight stream cipher for obfuscation, not a modern cryptographic standard. Do not rely on it for high-security use cases without review.
