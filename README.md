# footie
a 4 button/4 bank portable, battery powered, MIDI footswitch with USB and MIDI DIN output

<img src=https://raw.githubusercontent.com/hunked/footie/master/images/001.jpg width=600><br>
<img src=https://raw.githubusercontent.com/hunked/footie/master/images/002.jpg width=300><img src=https://raw.githubusercontent.com/hunked/footie/master/images/004.jpg width=300>

I decided to make an updated version of my last project, an [8 button USB/MIDI footswitch](https://github.com/hunked/eightbuttonMIDIfootswitch), to address some issues I had with the last design. The new version features:
-An internal 18650 battery (removeable) that is charged by a USB-C charging board
-Only 4 footswitches vs 8, but with 4 soft buttons to switch between 4 banks of buttons (for a total of 16)
-Expression pedal input (WIP, waiting for a TRS jack as all I have on hand are TS)

Otherwise the functions are very similar to my previous project and the code is mostly shared between them.

There are 6 modes of operation selectable at boot. 
The selection screen can be returned to at any time by resetting the unit or pressing buttons 7 + 8 (top two buttons on the right side) together simultaneously.
- MIDI Note Momentary/Timed (on note sent on button press, off note sent after button is released (or preset time if using toggle switches))
- MIDI Note Toggle (press button once to turn on, press again to turn off)
- MIDI CC Momentary/Timed (same as mode 1 but with CC messages) 
- MIDI CC Toggle (same as mode 2 but with CC)
- Program Change (pressing each of the 8 buttons sends a different Program Change message)
- Settings Menu (change channels, MIDI messages, default options)

If you do not choose an option the footswitch will start the default (mode 1) after a set timeout (6 seconds). 

You can customize all of the messages sent by the footswitch. Currently the following options are configurable from the settings menu:
- CHAN - channel for NOTE/CC/PC messages (set separately)
- NOTE - MIDI note numbers and velocities
- CC - Control Change numbers and on/off values
- PC - Program Change numbers
- DEF - default runmode (RUNM), menu timeout (MENU), how long before OFF note/CC is sent (NOFF/COFF) (only in toggle version)
- LOAD - load values from EEPROM
- SAVE - save values to EEPROM (changes you make to settings will not be stored unless you press this)
