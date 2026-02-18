# Issue 03 - Compilation Errors (Strings/Braces)

## Symptoms
- `missing terminating " character`
- `expected unqualified-id` near `if`
- Unbalanced braces in `loop()`

## Root Cause
- Broken string literals from edits.
- Duplicate blocks pasted outside function scope.

## Fix
- Repaired malformed strings.
- Removed duplicate code blocks.
- Restored brace balance.

## Files Referenced
- `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
