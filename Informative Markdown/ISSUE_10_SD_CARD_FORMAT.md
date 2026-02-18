# Issue 10: SD Card Not Working (Wrong File System Format)

## Summary
The CAN data logger failed to initialize or write to the SD card. The root cause was an incompatible file system format. The SD card was not formatted as FAT32, which is required by the ESP32 SD library stack used by the firmware. Reformatting the SD card to FAT32 resolved the issue. Only SD cards up to 32 GB are supported; larger cards are typically exFAT by default and are not supported.

## Impact
- Logging did not start (no .NXT files created).
- SD initialization reported failure or the system repeatedly reported the SD card as not ready.
- Data was not persisted even though CAN frames were being received.

## Affected Configuration
- ESP32-C6 logger firmware using the Arduino SD library (SPI mode).
- SD cards formatted as exFAT, NTFS, or other non-FAT32 formats.
- SD cards larger than 32 GB (commonly default to exFAT).

## Symptoms
- SD card initialization fails during startup.
- Serial console indicates SD card not ready or card type not detected.
- No log file is created even when CAN traffic is present.
- Repeated retries to initialize the SD card without success.

## Root Cause
The firmware expects a FAT32 volume. SD cards that are:
- formatted as exFAT (common for 64 GB and above),
- formatted as NTFS (Windows default for external storage), or
- formatted with a corrupted or unsupported partition table
cannot be mounted by the SD stack used in this project. As a result, SD initialization fails and logging is disabled.

## Resolution
Reformat the SD card as FAT32 and ensure the capacity is 32 GB or smaller.

## Required SD Card Specification
- Capacity: 32 GB or less
- File System: FAT32
- Partition Scheme: MBR recommended for maximum compatibility

## Steps to Reproduce
1. Insert an SD card formatted as exFAT or NTFS.
2. Power the logger and observe SD initialization.
3. SD init fails; log file is not created.

## Fix: Format SD Card to FAT32

### Windows (GUI)
1. Insert the SD card.
2. Open File Explorer -> This PC.
3. Right click the SD card -> Format.
4. File system: FAT32.
5. Allocation size: Default (or 32 KB for better compatibility).
6. Uncheck Quick Format if the card previously had errors.
7. Click Start.

### Windows (diskpart)
1. Open Command Prompt as Administrator.
2. Run:
   - `diskpart`
   - `list disk`
   - `select disk X` (X = SD card)
   - `clean`
   - `create partition primary`
   - `format fs=fat32 quick`
   - `assign`
   - `exit`

### macOS
1. Open Disk Utility.
2. Select the SD card.
3. Click Erase.
4. Format: MS-DOS (FAT).
5. Scheme: Master Boot Record.
6. Click Erase.

### Linux
1. Identify the device (e.g., `/dev/sdb`).
2. Run:
   - `sudo umount /dev/sdb1`
   - `sudo mkfs.vfat -F 32 /dev/sdb1`

## Verification Steps
1. Reinsert the SD card.
2. Boot the logger.
3. Confirm SD initialization messages indicate success.
4. Trigger CAN traffic and verify log file creation on the SD card.
5. Confirm the file size increases as frames are logged.

## Preventive Actions
- Standardize on 32 GB or smaller SD cards for deployments.
- Label cards as FAT32 preformatted for this logger.
- Add a pre-flight checklist item: verify FAT32 before field use.
- If using a new SD card, always format it explicitly to FAT32.

## Notes
- SD cards larger than 32 GB are not supported by the current firmware configuration. These cards are typically exFAT by default and will not mount.
- If a 32 GB card ships as exFAT, it must still be reformatted to FAT32.
- If SD initialization still fails after formatting, test the card with a PC and replace the card if errors are found.

## Status
Resolved by reformatting to FAT32 and using SD cards <= 32 GB.
