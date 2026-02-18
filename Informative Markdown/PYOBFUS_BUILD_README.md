# PyObfus Build Guide

## Overview

This build uses **PyObfus** (free obfuscation) combined with **manual encryption key encoding** for better protection than PyInstaller alone.

## Protection Features

### Code Obfuscation (PyObfus)
- ✅ Obfuscates code structure (function names, variables)
- ✅ Makes reverse engineering harder
- ✅ Free and open source
- ✅ No license limitations

### Encryption Key Protection (Manual Encoding)
- ✅ Encryption key XOR encoded and split into parts
- ✅ Key decoded at runtime (not visible in source)
- ✅ Better protection than plain obfuscation alone
- ✅ No additional cost

## How to Build

### Step 1: Run the Build Script

```bash
build_pyobfus.bat
```

### Step 2: What Happens

1. **Checks Dependencies**
   - Installs PyObfus if needed
   - Installs PyInstaller if needed
   - Creates icon if missing

2. **Obfuscates Source Code**
   - Obfuscates `nxt_to_mdf_advanced.py`
   - Obfuscates `nxt_to_mdf_gui.py`
   - Outputs to `obfuscated/` directory

3. **Builds Executable**
   - Uses PyInstaller to bundle obfuscated code
   - Includes all dependencies
   - Adds custom icon/logo
   - Creates `dist\Nxt_logger.exe`

### Step 3: Result

- **Executable:** `dist\Nxt_logger.exe`
- **Protection Level:** Medium-High (free solution)
- **File Size:** ~70-80 MB (includes all dependencies)

## Protection Comparison

| Feature | PyInstaller Only | PyObfus + Encoding |
|---------|-----------------|-------------------|
| Code Obfuscation | ❌ No | ✅ Yes |
| Key Protection | ⚠️ Weak | ✅ Medium |
| Reverse Engineering | ⚠️ Easy | ✅ Harder |
| Cost | Free | Free |
| Build Time | ~2-5 min | ~5-10 min |

## Technical Details

### Encryption Key Encoding

The encryption key is:
- **XOR encoded** with key `0x40`
- **Split into 4 parts** for better obfuscation
- **Decoded at runtime** using `_decode_encryption_key()`
- **Cached** to avoid repeated decoding

### Obfuscation Process

1. Source files are obfuscated with PyObfus
2. Obfuscated files replace originals temporarily
3. PyInstaller bundles obfuscated code
4. Original files are restored

## Troubleshooting

### "Module not found" errors
- Ensure all dependencies are installed: `pip install -r requirements.txt`
- Check that source files exist before building

### Obfuscation fails
- Verify PyObfus is installed: `pip install pyobfus`
- Check Python version (3.8+ required)

### Build fails
- Check PyInstaller is installed: `pip install pyinstaller`
- Verify icon file exists or can be created

## Advantages Over PyArmor

- ✅ **Free** - No license costs
- ✅ **No size limits** - Works with any file size
- ✅ **No trial expiration** - Works indefinitely
- ✅ **Open source** - Transparent process

## Limitations

- ⚠️ **Less protection than PyArmor paid** - But still good for most cases
- ⚠️ **Keys still somewhat visible** - But much harder to extract than plain code
- ⚠️ **Longer build time** - Obfuscation adds ~2-3 minutes

## For Maximum Protection

If you need even better protection:
- Consider **PyArmor Basic License** ($200-300)
- Provides string obfuscation (mix-str)
- Better encryption key protection
- Professional support

## Summary

This build provides **good free protection** suitable for:
- ✅ Personal projects
- ✅ Internal distribution
- ✅ Non-critical commercial use
- ✅ Protecting proprietary algorithms (moderate level)

For **critical commercial use** or **maximum security**, consider PyArmor paid license.

