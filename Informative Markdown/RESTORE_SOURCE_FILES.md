# Restoring Source Files for Advanced Build

## Problem

The build script is looking for:
- `nxt_to_mdf_advanced.py`
- `nxt_to_mdf_gui.py`

But these files are missing from your directory. The files in the `obfuscated` folder are already obfuscated and cannot be used as source.

## Solutions

### Option 1: Restore from Git (if using version control)

```bash
git checkout nxt_to_mdf_advanced.py nxt_to_mdf_gui.py
```

### Option 2: Restore from Backup

If you have backups, restore:
- `nxt_to_mdf_advanced.py`
- `nxt_to_mdf_gui.py`

### Option 3: Use Basic Protection Instead

If you can't restore the source files, use basic protection:

```bash
build_converter.bat
```

This doesn't require obfuscation and will work with the obfuscated files (though it's less secure).

### Option 4: Recreate Files

If you have the functionality documented, you can recreate the files. However, this is time-consuming.

## Quick Check

To see what files you have:

```bash
dir *.py
dir obfuscated\*.py
```

## Recommendation

**For now, use basic protection:**
```bash
build_converter.bat
```

This will create `dist\Nxt_logger.exe` without obfuscation. It still protects your code by bundling it into an executable.

## Future Prevention

1. **Keep source files separate** from obfuscated files
2. **Use version control** (Git) to track source files
3. **Create backups** before obfuscating
4. **Don't delete source files** after obfuscation

## File Structure Recommendation

```
project/
├── src/                    # Original source files (keep these!)
│   ├── nxt_to_mdf_advanced.py
│   └── nxt_to_mdf_gui.py
├── obfuscated/            # Obfuscated files (generated)
├── dist/                  # Built executables
└── build_advanced.bat     # Build script
```

