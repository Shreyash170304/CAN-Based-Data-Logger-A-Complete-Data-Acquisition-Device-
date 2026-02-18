# Fix Arduino IDE Color Scheme (Brown Background Issue)

If your Arduino IDE editor shows a brown/beige background instead of a normal light theme, follow these steps.

## Arduino IDE 2.x
1. Open Arduino IDE 2.x
2. Go to File > Preferences
3. Change Editor Theme to Light or Default
4. Restart Arduino IDE

## Arduino IDE 1.x
1. Open Arduino IDE 1.x
2. Go to File > Preferences
3. Look for editor theme or color settings
4. Select Default/Light and restart

## Reset Preferences (Last Resort)
- Close Arduino IDE
- Rename or delete the preferences file
  - Windows: %APPDATA%/Arduino/preferences.txt
  - Mac: ~/Library/Arduino15/preferences.txt
  - Linux: ~/.arduino15/preferences.txt
- Restart Arduino IDE

## Notes
- Always back up preferences if you have custom settings.
- Arduino IDE 2.x has better theme support than 1.x.
