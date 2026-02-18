# Why Only Bus_current Was Wrong (Simple Explanation)

Even though many CAN IDs and signals are decoded, **Bus_current** was the only one that looked clearly wrong. This happened because that signal is *very sensitive* to decoding mistakes.

## 1) Big offset makes errors obvious
Bus_current uses this formula from the DBC:

```
value = raw * 0.1 - 1000
```

So if the raw value is slightly wrong, the final result becomes **hundreds of amps off**. That’s why you saw **-300 A**.

## 2) Big‑endian (Motorola) layout
Bus_current is defined as:

```
23|16@0+
```

`@0` means **Motorola / big‑endian**. If the bytes are read in the wrong order, the raw number becomes wrong immediately.

Many other signals are **8‑bit** or simple byte‑aligned values, so they don’t get messed up as much.

## 3) Other signals still looked “normal”
Even if another signal was slightly off, it might still look believable (like a temperature or speed). But Bus_current becomes very large and negative, so the error is **obvious only in that signal**.

## Summary (simple)
- Bus_current has a **large offset** and **big‑endian format**.
- That combination makes it **easy to decode wrong**.
- Other signals are simpler or less sensitive, so they still looked correct.

