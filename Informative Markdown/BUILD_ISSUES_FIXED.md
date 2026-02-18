# Build Issues Fixed

## Problems Identified

### 1. Source Files Disappearing
**Symptom:** When running `build_pyobfus.bat`, source files (`nxt_to_mdf_advanced.py` and `nxt_to_mdf_gui.py`) vanish during obfuscation.

**Root Cause:** 
- Old build script temporarily replaced original files with obfuscated versions
- If build failed, files might not be restored
- Backup/restore logic had timing issues

**Solution:**
- ✅ Build script now uses `build_temp/` directory
- ✅ Original files are NEVER modified or moved
- ✅ Obfuscated files go to `obfuscated/` directory
- ✅ Build happens from `build_temp/` (copy of obfuscated files)
- ✅ Original files remain safe throughout entire process

### 2. Import Error in Executable
**Symptom:** When running the built `.exe`, shows "Import Error: Could not import converter functions."

**Root Cause:**
- PyObfus obfuscates ALL function names
- `decrypt_nxt_to_csv` becomes `I22`
- `parse_csv_to_dataframe` becomes `I56`
- `create_mdf_from_dataframe` becomes `I15`
- GUI tries to import by original names → ImportError

**Solution:**
- ✅ Created `preserve_api.py` script
- ✅ Automatically detects obfuscated function names
- ✅ Adds aliases to preserve public API:
  ```python
  decrypt_nxt_to_csv = I22
  parse_csv_to_dataframe = I56
  create_mdf_from_dataframe = I15
  ```
- ✅ GUI can now import functions by original names
- ✅ Works in both obfuscated and non-obfuscated builds

## Updated Build Process

### Step-by-Step Flow:

1. **Check Source Files**
   - Verifies `nxt_to_mdf_advanced.py` and `nxt_to_mdf_gui.py` exist
   - Auto-restores from `source_backup_*` if missing
   - Shows clear error if restoration fails

2. **Obfuscate Files**
   - Obfuscates to `obfuscated/` directory
   - Original files remain untouched in root directory
   - Both files obfuscated separately

3. **Prepare Build**
   - Creates `build_temp/` directory
   - Copies obfuscated files to temp
   - Runs `preserve_api.py` to add function aliases
   - Creates modified spec file pointing to temp directory

4. **Build Executable**
   - PyInstaller builds from `build_temp/` directory
   - Original files never touched
   - Obfuscated files with aliases are used

5. **Cleanup**
   - Always removes `build_temp/` directory
   - Always removes temporary spec file
   - Original files remain safe

## Files Created/Modified

### New Files:
- `preserve_api.py` - Adds function aliases to preserve public API
- `create_temp_spec.py` - Creates modified spec file for temp build
- `source_backup_20251208_202947/` - Backup of source files
- `BUILD_FIX_SUMMARY.md` - This document

### Modified Files:
- `build_pyobfus.bat` - Fixed to never modify original files

## How to Use

### Build Executable:
```bash
build_pyobfus.bat
```

### If Source Files Are Missing:
The build script will automatically restore them from backup. If that fails:
```bash
copy source_backup_20251208_202947\nxt_to_mdf_advanced.py .
copy source_backup_20251208_202947\nxt_to_mdf_gui.py .
```

## Verification

### Test Source Files Are Safe:
```bash
dir nxt_to_mdf*.py
```
Files should always be present in root directory.

### Test Imports Work:
```bash
python -c "from nxt_to_mdf_advanced import decrypt_nxt_to_csv; print('OK')"
```

### Test Executable:
```bash
dist\Nxt_logger.exe
```
Should open GUI without import errors.

## Key Improvements

1. **Original Files Never Modified**
   - Build process uses temporary directories
   - Original files stay in root directory
   - Safe even if build fails

2. **Public API Preserved**
   - Function aliases added after obfuscation
   - Imports work correctly
   - Executable runs without errors

3. **Auto-Restore from Backup**
   - Build script checks for source files
   - Automatically restores from backup if missing
   - Clear error messages if restoration fails

4. **Better Error Handling**
   - Always cleans up temp files
   - Clear error messages
   - Original files always safe

## Troubleshooting

### "Source files not found"
- Build script will auto-restore from backup
- Or manually restore: `copy source_backup_*\nxt_to_mdf_*.py .`

### "Import Error" in executable
- Check that `preserve_api.py` ran successfully
- Verify aliases exist in obfuscated file
- Rebuild if needed

### Build fails
- Original files are safe - they were never modified
- Check error messages
- Try restoring from backup and rebuilding

## Summary

✅ **Source files are now safe** - Never modified during build
✅ **Imports work correctly** - Function aliases preserve public API  
✅ **Auto-restore from backup** - Build script handles missing files
✅ **Better error handling** - Clear messages and safe cleanup

The build process is now robust and safe!

