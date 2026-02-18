# Build Script Fix Summary

## Problems Fixed

### 1. Source Files Disappearing During Build
**Problem:** Original source files were being replaced with obfuscated versions and not always restored.

**Solution:**
- Build script now uses a `build_temp` directory
- Original files are NEVER modified
- Obfuscated files are copied to temp directory only
- Build happens from temp directory
- Original files remain safe throughout the process

### 2. Import Errors in Executable
**Problem:** PyObfus obfuscates all function names, so when GUI imports `decrypt_nxt_to_csv`, it doesn't exist (it's now `I22`).

**Solution:**
- Created `preserve_api.py` script
- Adds function aliases after obfuscation:
  - `decrypt_nxt_to_csv = I22`
  - `parse_csv_to_dataframe = I56`
  - `create_mdf_from_dataframe = I15`
- GUI can now import functions by their original names

### 3. Auto-Restore from Backup
**Problem:** If source files are missing, build fails.

**Solution:**
- Build script now automatically checks for source files
- If missing, attempts to restore from `source_backup_*` folders
- Shows clear error if restoration fails

## Updated Build Process

1. **Check/Restore Source Files**
   - Verifies `nxt_to_mdf_advanced.py` and `nxt_to_mdf_gui.py` exist
   - Auto-restores from backup if missing

2. **Obfuscate Files**
   - Obfuscates to `obfuscated/` directory
   - Original files remain untouched

3. **Prepare Build**
   - Creates `build_temp/` directory
   - Copies obfuscated files to temp
   - Adds function aliases to preserve public API
   - Creates modified spec file pointing to temp directory

4. **Build Executable**
   - Builds from temp directory
   - Original files never touched

5. **Cleanup**
   - Always cleans up temp files (even on failure)
   - Original files remain safe

## Files Created

- `preserve_api.py` - Adds function aliases to obfuscated files
- `create_temp_spec.py` - Creates modified spec file for temp build
- `source_backup_20251208_202947/` - Backup of original source files

## How to Use

### Build with PyObfus:
```bash
build_pyobfus.bat
```

### Restore Source Files (if missing):
```bash
copy source_backup_20251208_202947\nxt_to_mdf_advanced.py .
copy source_backup_20251208_202947\nxt_to_mdf_gui.py .
```

## Protection Features

- ✅ Original source files never modified
- ✅ Public API preserved (imports work)
- ✅ Encryption key encoded
- ✅ Code obfuscated
- ✅ Auto-restore from backup

## Troubleshooting

### "Source files not found"
- Check if files exist in root directory
- Run build script - it will auto-restore from backup
- Or manually restore from `source_backup_*` folder

### "Import Error" in executable
- Make sure `preserve_api.py` ran successfully
- Check that aliases were added to obfuscated file
- Rebuild if needed

### Build fails
- Original files are safe - they were never modified
- Check error messages
- Try restoring from backup and rebuilding

