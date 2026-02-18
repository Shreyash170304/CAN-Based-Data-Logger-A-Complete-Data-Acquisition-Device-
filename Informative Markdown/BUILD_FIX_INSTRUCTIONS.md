# Fix for PyArmor Runtime Error

## Problem
When building with `build_advanced.bat`, you get:
```
ModuleNotFoundError: No module named 'pyarmor_runtime_000000'
```

## Solution

The updated `build_advanced.bat` now automatically:
1. Detects the PyArmor runtime module
2. Updates the spec file to include it
3. Copies the runtime directory to the build

## Quick Fix

If you still get the error, run this manually:

```bash
# 1. Find the runtime module name
dir /b /s obfuscated\pyarmor_runtime*

# 2. Update the spec file (replace XXXXXX with actual name)
python fix_pyarmor_build.py build_converter.spec pyarmor_runtime_000000

# 3. Copy runtime to current directory
xcopy /E /I /Y obfuscated\pyarmor_runtime_000000 .

# 4. Build
pyinstaller --clean build_converter.spec
```

## Alternative: Use Simplified Build

Try the simpler build script:
```bash
build_advanced_simple.bat
```

This uses a different approach that's more reliable.

## Manual Build (If Scripts Fail)

1. **Obfuscate:**
   ```bash
   pyarmor gen --recursive --enable-rft --output obfuscated nxt_to_mdf_advanced.py nxt_to_mdf_gui.py
   ```

2. **Copy files:**
   ```bash
   copy obfuscated\nxt_to_mdf_advanced.py .
   copy obfuscated\nxt_to_mdf_gui.py .
   xcopy /E /I /Y obfuscated\pyarmor_runtime_000000 .
   ```

3. **Build with explicit runtime:**
   ```bash
   pyinstaller --clean ^
       --name Nxt_logger ^
       --onefile ^
       --windowed ^
       --icon Nxt_logger.ico ^
       --hidden-import pyarmor_runtime_000000 ^
       --hidden-import asammdf ^
       --hidden-import cantools ^
       --hidden-import pandas ^
       --hidden-import numpy ^
       --hidden-import nxt_to_mdf_advanced ^
       nxt_to_mdf_gui.py
   ```

4. **Clean up:**
   ```bash
   del nxt_to_mdf_advanced.py
   del nxt_to_mdf_gui.py
   rmdir /s /q pyarmor_runtime_000000
   ```

## Verify Runtime is Included

After building, check the executable includes the runtime:
```bash
# Extract and check (optional)
pyinstaller --clean --onedir --name test nxt_to_mdf_gui.py
# Check dist\test\_internal\ for pyarmor_runtime_000000
```

## Still Having Issues?

1. Make sure PyArmor is up to date: `pip install --upgrade pyarmor`
2. Try without `--enable-rft` flag: `pyarmor gen --recursive --output obfuscated ...`
3. Use basic protection instead: `build_converter.bat` (no obfuscation)

