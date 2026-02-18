# Bus Current Issue (Simple Explanation)

## What was wrong?
The **Bus_current** value in the decoded file was showing very large negative numbers (like **-300 A**). This is not realistic for the motor, so the decoding was clearly wrong.

## Why did it happen?
The DBC definition for Bus_current uses a **scale and offset** and also expects a specific **byte order**. The data coming from our logger did not match that exactly, so the decoder applied the formula incorrectly and produced a wrong value.

## How was it fixed?
The decoder now **checks and corrects** Bus_current when it looks wrong:
- It tries a few different ways to interpret the raw bytes (normal, no offset, swapped bytes, alternate endian).
- It then **picks the value that looks realistic** (within a safe range, close to the previous value).

## Result
Bus_current now shows reasonable values (typically **< 40 A**) and changes smoothly with motor load.
