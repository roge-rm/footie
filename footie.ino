/*
   https://github.com/hunked/footie
*/

#include <SPI.h>
#include <Wire.h>
#include <Bounce.h>
#include <MIDI.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 //OLED display width, in pixels
#define SCREEN_HEIGHT 64 //OLED display height, in pixels

//Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 //Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//Create MIDI instance for 5 pin MIDI output
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

//Declare pins for footswitches
const byte switchPin[8] = {10, 11, 12, 13, 14, 15, 16, 17};

//Declare pins for LEDs;
const byte ledPin[4] = {28, 29, 30, 31};
const int ledDelay = 50; //How long to light LEDs up for visual confirmation

//Default channel for MIDI notes, CC, program change, control change
int MIDI_NOTE_CHAN;
int MIDI_CC_CHAN;
int MIDI_PC_CHAN;

//Can select from 4 BANKs of notes/velocities/etc
int BANK = 0;

//Arrays are declared here but loaded from EEPROM/defaults in eepromREAD()
int switchNotes[4][4];
int switchVels[4][4];
int switchCCs[4][4];
int switchCCValOns[4][4];
int switchCCValOffs[4][4];
int switchPCs[4][4];

bool ledStatus[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};

//Switch states for runmodes that require toggling
bool switchStates[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};

//Create Bounce objects for each button and switch.
//Default debounce time is 5ms
const int DEBOUNCE_TIME = 5;

Bounce button1 = Bounce(switchPin[0], DEBOUNCE_TIME);
Bounce button2 = Bounce(switchPin[1], DEBOUNCE_TIME);
Bounce button3 = Bounce(switchPin[2], DEBOUNCE_TIME);
Bounce button4 = Bounce(switchPin[3], DEBOUNCE_TIME);
Bounce button5 = Bounce(switchPin[4], DEBOUNCE_TIME);
Bounce button6 = Bounce(switchPin[5], DEBOUNCE_TIME);
Bounce button7 = Bounce(switchPin[6], DEBOUNCE_TIME);
Bounce button8 = Bounce(switchPin[7], DEBOUNCE_TIME);

//Track runmode (function of footswitch) as well as pick default option if no change is made before timeout. Default timeout is 6000ms.
//0 = Startup screen (runmode select)
//1 = MIDI Note ON+OFF (timed), 2 = MIDI Note Toggle (must hit switch again to turn off)
//3 = MIDI CC ON+OFF (timed), 4 = MIDI CC Toggle (same as MIDI Note, but with CC)
//5 = Program Change, 8 = Settings
int RUNMODE = 0;

int RUNMODE_DEFAULT; //set by EEPROM
unsigned int RUNMODE_TIMEOUT = 6000;
const int RUNMODE_LONGTIMEOUT = 31000;

//Counter setup for menus requiring timeouts (due to lack of a back button)
unsigned long previousMillis = 0;

//For settings menu navigation
int menuCat[3] = {0, 0, 0};
unsigned long MENU_TIMEOUT;
unsigned long currentMillis = 0;

bool WIPE = 1;
int wipeCOUNT = 0;
int wipeLIMIT = 10;

void setup() {
  eepromREAD(); //Set variable values from EEPROM (or defaults if EEPROM has not been written)

  //Serial.begin(9600); //Debugging

  MIDI.begin();

  //Configure switch pins as for input mode with pullup resistors
  pinMode (switchPin[0], INPUT_PULLUP);
  pinMode (switchPin[1], INPUT_PULLUP);
  pinMode (switchPin[2], INPUT_PULLUP);
  pinMode (switchPin[3], INPUT_PULLUP);
  pinMode (switchPin[4], INPUT_PULLUP);
  pinMode (switchPin[5], INPUT_PULLUP);
  pinMode (switchPin[6], INPUT_PULLUP);
  pinMode (switchPin[7], INPUT_PULLUP);

  //Configure LED pins for output
  pinMode (ledPin[0], OUTPUT);
  pinMode (ledPin[1], OUTPUT);
  pinMode (ledPin[2], OUTPUT);
  pinMode (ledPin[3], OUTPUT);

  //SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { //Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); //Don't proceed, loop forever
  }

  updatebuttons(); //Pull initial button status to prevent phantom MIDI messages from being sent once the loop starts

  display.display();
  delay(500);

  display.clearDisplay();
}

void loop() {

  ledUPDATE();
  currentMillis = millis();
  if (WIPE) {
    display.clearDisplay();
    WIPE = 0;
    wipeCOUNT = 0;
  }
  display.setTextSize(2);
  display.setTextColor(WHITE);

  updatebuttons(); //Poll for button presses

  if (RUNMODE == 0) {
    runmodeSELECTMODE();
  } else if (RUNMODE == 1) {
    runmodeMIDINOTETIMED();
  } else if (RUNMODE == 2) {
    runmodeMIDINOTETOGGLE();
  } else if (RUNMODE == 3) {
    runmodeCCTIMED();
  } else if (RUNMODE == 4) {
    runmodeCCTOGGLE();
  } else if (RUNMODE == 5) {
    runmodePROGRAMCHANGE();
  } else if (RUNMODE == 8) {
    runmodeSETTINGS();
  }

  //Select between the 4 BANKs of buttons
  if (RUNMODE > 0 && RUNMODE < 8) {
    if (button5.fallingEdge() || button6.fallingEdge() || button7.fallingEdge() || button8.fallingEdge()) {
      display.clearDisplay();
      if (button5.fallingEdge()) {
        BANK = 0;
      }
      if (button6.fallingEdge()) {
        BANK = 1;
      }
      if (button7.fallingEdge()) {
        BANK = 2;
      }
      if (button8.fallingEdge()) {
        BANK = 3;
      }
      blinkLED(BANK + 1);
      wipeCOUNT = 0;
      displayTEXT(17, "Bank");
      displayTEXT(18, BANK + 1);
    }
  }

  //Break back to mode select by pressing buttons 7 & 8 simultaneously. Set timeout to 31 seconds to give user more time for a selection
  if (button7.fallingEdge() && button8.fallingEdge()) {
    RUNMODE = 0;
    RUNMODE_TIMEOUT = RUNMODE_LONGTIMEOUT;
    resetSWITCHES();
    resetMENU();
    previousMillis = currentMillis;
  }

  while (usbMIDI.read()) //Ignore incoming MIDI
  {
  }

  if (wipeCOUNT < wipeLIMIT) {
    wipeCOUNT++;
  } else {
    WIPE = 1;
  }
}

void updatebuttons() {
  button1.update();
  button2.update();
  button3.update();
  button4.update();
  button5.update();
  button6.update();
  button7.update();
  button8.update();
}

void runmodeSELECTMODE() { //Give choice between running modes, choose default mode after timeout if no option selected
  //unsigned long currentMillis = millis();
  long timeOut = (RUNMODE_TIMEOUT / 1000 - ((currentMillis - previousMillis) / 1000));

  display.setTextColor(WHITE, BLACK);
  display.setTextSize(1);
  display.setCursor(10, 34);
  display.println(F("CHOOSE RUNMODE"));
  display.setCursor(100, 34);
  display.println(timeOut);

  display.setTextSize(2);
  display.setCursor(10, 50);
  if (RUNMODE_DEFAULT == 1) {
    display.setTextColor(BLACK, WHITE);
  } else display.setTextColor(WHITE, BLACK);
  display.println(F("1"));
  display.setCursor(40, 50);
  if (RUNMODE_DEFAULT == 2) {
    display.setTextColor(BLACK, WHITE);
  } else display.setTextColor(WHITE, BLACK);
  display.println(F("2"));
  display.setCursor(70, 50);
  if (RUNMODE_DEFAULT == 3) {
    display.setTextColor(BLACK, WHITE);
  } else display.setTextColor(WHITE, BLACK);
  display.println(F("3"));
  display.setCursor(100, 50);
  if (RUNMODE_DEFAULT == 4) {
    display.setTextColor(BLACK, WHITE);
  } else display.setTextColor(WHITE, BLACK);
  display.println(F("4"));
  display.setCursor(10, 10);
  if (RUNMODE_DEFAULT == 5) {
    display.setTextColor(BLACK, WHITE);
  } else display.setTextColor(WHITE, BLACK);
  display.println(F("5"));
  display.setCursor(100, 10);
  display.setTextColor(WHITE, BLACK);
  display.println(F("8"));

  if (button1.fallingEdge()) {
    RUNMODE = 1;
    blinkLED(1);
    previousMillis = currentMillis;
  }
  if (button2.fallingEdge()) {
    RUNMODE = 2;
    blinkLED(2);
    previousMillis = currentMillis;
  }
  if (button3.fallingEdge()) {
    RUNMODE = 3;
    blinkLED(3);
    previousMillis = currentMillis;
  }
  if (button4.fallingEdge()) {
    RUNMODE = 4;
    blinkLED(4);
    previousMillis = currentMillis;
  }
  if (button5.fallingEdge()) {
    RUNMODE = 5;
    blinkLED(1);
    previousMillis = currentMillis;
  }
  if (button8.fallingEdge()) {
    RUNMODE = 8;
    blinkLED(4);
    previousMillis = currentMillis;
  }

  if (currentMillis - previousMillis >= RUNMODE_TIMEOUT) {
    RUNMODE = RUNMODE_DEFAULT;
    blinkLED(RUNMODE_DEFAULT);
    previousMillis = currentMillis;
  }
  WIPE = 1;
  display.setTextColor(WHITE, BLACK);
  display.display();
}


void runmodeMIDINOTETIMED() {
  if (button1.fallingEdge() || button2.fallingEdge() || button3.fallingEdge() || button4.fallingEdge()) {
    display.clearDisplay();
    wipeCOUNT = 0;
  }
  if (button1.fallingEdge()) {
    display.setCursor(10, 50);
    display.println(switchNotes[BANK][0]);
    ledStatus[BANK][0] = 1;
    usbMIDI.sendNoteOn(switchNotes[BANK][0], switchVels[BANK][0], MIDI_NOTE_CHAN);
    MIDI.sendNoteOn(switchNotes[BANK][0], switchVels[BANK][0], MIDI_NOTE_CHAN);
  }
  else if (button1.risingEdge()) {
    display.setCursor(10, 50);
    display.println(switchNotes[BANK][0]);
    usbMIDI.sendNoteOff(switchNotes[BANK][0], 0, MIDI_NOTE_CHAN);
    MIDI.sendNoteOff(switchNotes[BANK][0], 0, MIDI_NOTE_CHAN);
    ledStatus[BANK][0] = 0;
  }

  if (button2.fallingEdge()) {
    display.setCursor(40, 50);
    display.println(switchNotes[BANK][1]);
    ledStatus[BANK][1] = 1;
    usbMIDI.sendNoteOn(switchNotes[BANK][1], switchVels[BANK][1], MIDI_NOTE_CHAN);
    MIDI.sendNoteOn(switchNotes[BANK][1], switchVels[BANK][1], MIDI_NOTE_CHAN);
  }
  else if (button2.risingEdge()) {
    display.setCursor(40, 50);
    display.println(switchNotes[BANK][1]);
    usbMIDI.sendNoteOff(switchNotes[BANK][1], 0, MIDI_NOTE_CHAN);
    MIDI.sendNoteOff(switchNotes[BANK][1], 0, MIDI_NOTE_CHAN);
    ledStatus[BANK][1] = 0;
  }

  if (button3.fallingEdge()) {
    display.setCursor(70, 50);
    display.println(switchNotes[BANK][2]);
    ledStatus[BANK][2] = 1;
    usbMIDI.sendNoteOn(switchNotes[BANK][2], switchVels[BANK][2], MIDI_NOTE_CHAN);
    MIDI.sendNoteOn(switchNotes[BANK][2], switchVels[BANK][2], MIDI_NOTE_CHAN);
  }
  else if (button3.risingEdge()) {
    display.setCursor(70, 50);
    display.println(switchNotes[BANK][2]);
    usbMIDI.sendNoteOff(switchNotes[BANK][2], 0, MIDI_NOTE_CHAN);
    MIDI.sendNoteOff(switchNotes[BANK][2], 0, MIDI_NOTE_CHAN);
    ledStatus[BANK][2] = 0;
  }

  if (button4.fallingEdge()) {
    display.setCursor(100, 50);
    display.println(switchNotes[BANK][3]);
    ledStatus[BANK][3] = 1;
    usbMIDI.sendNoteOn(switchNotes[BANK][3], switchVels[BANK][3], MIDI_NOTE_CHAN);
    MIDI.sendNoteOn(switchNotes[BANK][3], switchVels[BANK][3], MIDI_NOTE_CHAN);
  }
  else if (button4.risingEdge()) {
    display.setCursor(100, 50);
    display.println(switchNotes[BANK][3]);
    usbMIDI.sendNoteOff(switchNotes[BANK][3], 0, MIDI_NOTE_CHAN);
    MIDI.sendNoteOff(switchNotes[BANK][3], 0, MIDI_NOTE_CHAN);
    ledStatus[BANK][3] = 0;
  }

  display.display();
}

void runmodeMIDINOTETOGGLE() {
  if (button1.fallingEdge() || button2.fallingEdge() || button3.fallingEdge() || button4.fallingEdge()) {
    display.clearDisplay();
    wipeCOUNT = 0;
    if (button1.fallingEdge()) {
      display.setCursor(10, 50);
      display.println(switchNotes[BANK][0]);
      if (!switchStates[BANK][0]) {
        ledStatus[BANK][0] = 1;
        usbMIDI.sendNoteOn(switchNotes[BANK][0], switchVels[BANK][0], MIDI_NOTE_CHAN);
        MIDI.sendNoteOn(switchNotes[BANK][0], switchVels[BANK][0], MIDI_NOTE_CHAN);
        switchStates[BANK][0] = 1;
      } else {
        usbMIDI.sendNoteOff(switchNotes[BANK][0], 0, MIDI_NOTE_CHAN);
        MIDI.sendNoteOff(switchNotes[BANK][0], 0, MIDI_NOTE_CHAN);
        ledStatus[BANK][0] = 0;
        switchStates[BANK][0] = 0;
      }
    }

    if (button2.fallingEdge()) {
      display.setCursor(40, 50);
      display.println(switchNotes[BANK][1]);
      if (!switchStates[BANK][1]) {
        ledStatus[BANK][1] = 1;
        usbMIDI.sendNoteOn(switchNotes[BANK][1], switchVels[BANK][1], MIDI_NOTE_CHAN);
        MIDI.sendNoteOn(switchNotes[BANK][1], switchVels[BANK][1], MIDI_NOTE_CHAN);
        switchStates[BANK][1] = 1;
      } else {
        usbMIDI.sendNoteOff(switchNotes[BANK][1], 0, MIDI_NOTE_CHAN);
        MIDI.sendNoteOff(switchNotes[BANK][1], 0, MIDI_NOTE_CHAN);
        ledStatus[BANK][1] = 0;
        switchStates[BANK][1] = 0;
      }
    }

    if (button3.fallingEdge()) {
      display.setCursor(70, 50);
      display.println(switchNotes[BANK][2]);
      if (!switchStates[BANK][2]) {
        ledStatus[BANK][2] = 1;
        usbMIDI.sendNoteOn(switchNotes[BANK][2], switchVels[BANK][2], MIDI_NOTE_CHAN);
        MIDI.sendNoteOn(switchNotes[BANK][2], switchVels[BANK][2], MIDI_NOTE_CHAN);
        switchStates[BANK][2] = 1;
      } else {
        usbMIDI.sendNoteOff(switchNotes[BANK][2], 0, MIDI_NOTE_CHAN);
        MIDI.sendNoteOff(switchNotes[BANK][2], 0, MIDI_NOTE_CHAN);
        ledStatus[BANK][2] = 0;
        switchStates[BANK][2] = 0;
      }
    }

    if (button4.fallingEdge()) {
      display.setCursor(100, 50);
      display.println(switchNotes[BANK][3]);
      if (!switchStates[BANK][3]) {
        ledStatus[BANK][3] = 1;
        usbMIDI.sendNoteOn(switchNotes[BANK][3], switchVels[BANK][3], MIDI_NOTE_CHAN);
        MIDI.sendNoteOn(switchNotes[BANK][3], switchVels[BANK][3], MIDI_NOTE_CHAN);
        switchStates[BANK][3] = 1;
      } else {
        usbMIDI.sendNoteOff(switchNotes[BANK][3], 0, MIDI_NOTE_CHAN);
        MIDI.sendNoteOff(switchNotes[BANK][3], 0, MIDI_NOTE_CHAN);
        ledStatus[BANK][3] = 0;
        switchStates[BANK][3] = 0;
      }
    }
  }

  display.display();
}

void runmodeCCTIMED() {
  if (button1.fallingEdge() || button2.fallingEdge() || button3.fallingEdge() || button4.fallingEdge()) {
    display.clearDisplay();
    wipeCOUNT = 0;
  }
  if (button1.fallingEdge()) {
    display.setCursor(10, 50);
    display.println(switchCCs[BANK][0]);
    ledStatus[BANK][0] = 1;
    usbMIDI.sendControlChange(switchCCs[BANK][0], switchCCValOns[BANK][0], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][0], switchCCValOns[BANK][0], MIDI_CC_CHAN);
  }
  else if (button1.risingEdge()) {
    display.setCursor(10, 50);
    display.println(switchCCs[BANK][0]);
    usbMIDI.sendControlChange(switchCCs[BANK][0], switchCCValOffs[BANK][0], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][0], switchCCValOffs[BANK][0], MIDI_CC_CHAN);
    ledStatus[BANK][0] = 0;
  }

  if (button2.fallingEdge()) {
    display.setCursor(40, 50);
    display.println(switchCCs[BANK][1]);
    ledStatus[BANK][1] = 1;
    usbMIDI.sendControlChange(switchCCs[BANK][1], switchCCValOns[BANK][1], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][1], switchCCValOns[BANK][1], MIDI_CC_CHAN);
  }
  else if (button2.risingEdge()) {
    display.setCursor(40, 50);
    display.println(switchCCs[BANK][1]);
    usbMIDI.sendControlChange(switchCCs[BANK][1], switchCCValOffs[BANK][1], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][1], switchCCValOffs[BANK][1], MIDI_CC_CHAN);
    ledStatus[BANK][1] = 0;
  }

  if (button3.fallingEdge()) {
    display.setCursor(70, 50);
    display.println(switchCCs[BANK][2]);
    ledStatus[BANK][2] = 1;
    usbMIDI.sendControlChange(switchCCs[BANK][2], switchCCValOns[BANK][2], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][2], switchCCValOns[BANK][2], MIDI_CC_CHAN);
  }
  else if (button3.risingEdge()) {
    display.setCursor(70, 50);
    display.println(switchCCs[BANK][2]);
    usbMIDI.sendControlChange(switchCCs[BANK][2], switchCCValOffs[BANK][2], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][2], switchCCValOffs[BANK][2], MIDI_CC_CHAN);
    ledStatus[BANK][2] = 0;
  }

  if (button4.fallingEdge()) {
    display.setCursor(100, 50);
    display.println(switchCCs[BANK][3]);
    ledStatus[BANK][3] = 1;
    usbMIDI.sendControlChange(switchCCs[BANK][3], switchCCValOns[BANK][3], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][3], switchCCValOns[BANK][3], MIDI_CC_CHAN);
  }
  else if (button4.risingEdge()) {
    display.setCursor(100, 50);
    display.println(switchCCs[BANK][3]);
    usbMIDI.sendControlChange(switchCCs[BANK][3], switchCCValOffs[BANK][3], MIDI_CC_CHAN);
    MIDI.sendControlChange(switchCCs[BANK][3], switchCCValOffs[BANK][3], MIDI_CC_CHAN);
    ledStatus[BANK][3] = 0;
  }

  display.display();
}


void runmodeCCTOGGLE() {
  if (button1.fallingEdge() || button2.fallingEdge() || button3.fallingEdge() || button4.fallingEdge()) {
    display.clearDisplay();
    wipeCOUNT = 0;
    if (button1.fallingEdge()) {
      display.setCursor(10, 50);
      display.println(switchCCs[BANK][0]);
      if (!switchStates[BANK][0]) {
        ledStatus[BANK][0] = 1;
        usbMIDI.sendControlChange(switchCCs[BANK][0], switchCCValOns[BANK][0], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][0], switchCCValOns[BANK][0], MIDI_CC_CHAN);
        switchStates[BANK][0] = 1;
      } else {
        usbMIDI.sendControlChange(switchCCs[BANK][0], switchCCValOffs[BANK][0], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][0], switchCCValOffs[BANK][0], MIDI_CC_CHAN);
        ledStatus[BANK][0] = 0;
        switchStates[BANK][0] = 0;
      }
    }

    if (button2.fallingEdge()) {
      display.setCursor(40, 50);
      display.println(switchCCs[BANK][1]);
      if (!switchStates[BANK][1]) {
        ledStatus[BANK][1] = 1;
        usbMIDI.sendControlChange(switchCCs[BANK][1], switchCCValOns[BANK][1], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][1], switchCCValOns[BANK][1], MIDI_CC_CHAN);
        switchStates[BANK][1] = 1;
      } else {
        usbMIDI.sendControlChange(switchCCs[BANK][1], switchCCValOffs[BANK][1], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][1], switchCCValOffs[BANK][1], MIDI_CC_CHAN);
        ledStatus[BANK][1] = 0;
        switchStates[BANK][1] = 0;
      }
    }

    if (button3.fallingEdge()) {
      display.setCursor(70, 50);
      display.println(switchCCs[BANK][2]);
      if (!switchStates[BANK][2]) {
        ledStatus[BANK][2] = 1;
        usbMIDI.sendControlChange(switchCCs[BANK][2], switchCCValOns[BANK][2], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][2], switchCCValOns[BANK][2], MIDI_CC_CHAN);
        switchStates[BANK][2] = 1;
      } else {
        usbMIDI.sendControlChange(switchCCs[BANK][2], switchCCValOffs[BANK][2], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][2], switchCCValOffs[BANK][2], MIDI_CC_CHAN);
        ledStatus[BANK][2] = 0;
        switchStates[BANK][2] = 0;
      }
    }

    if (button4.fallingEdge()) {
      display.setCursor(100, 50);
      display.println(switchCCs[BANK][3]);
      if (!switchStates[BANK][3]) {
        ledStatus[BANK][3] = 1;
        usbMIDI.sendControlChange(switchCCs[BANK][3], switchCCValOns[BANK][3], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][3], switchCCValOns[BANK][3], MIDI_CC_CHAN);
        switchStates[BANK][3] = 1;
      } else {
        usbMIDI.sendControlChange(switchCCs[BANK][3], switchCCValOffs[BANK][3], MIDI_CC_CHAN);
        MIDI.sendControlChange(switchCCs[BANK][3], switchCCValOffs[BANK][3], MIDI_CC_CHAN);
        ledStatus[BANK][3] = 0;
        switchStates[BANK][3] = 0;
      }
    }
  }

  display.display();
}


void runmodePROGRAMCHANGE() {
  if (button1.fallingEdge() || button2.fallingEdge() || button3.fallingEdge() || button4.fallingEdge()) {
    display.clearDisplay();
    wipeCOUNT = 0;
    if (button1.fallingEdge()) {
      display.setCursor(10, 50);
      display.println(switchPCs[BANK][0]);
      if (switchStates[BANK][0] != 1) {
        resetSWITCHES();
        switchStates[BANK][0] = 1;
        ledStatus[BANK][0] = 1;
      }
      usbMIDI.sendProgramChange(switchPCs[BANK][0], MIDI_PC_CHAN);
      MIDI.sendProgramChange(switchPCs[BANK][0], MIDI_PC_CHAN);
    }

    if (button2.fallingEdge()) {
      display.setCursor(40, 50);
      display.println(switchPCs[BANK][1]);
      if (switchStates[BANK][1] != 1) {
        resetSWITCHES();
        switchStates[BANK][1] = 1;
        ledStatus[BANK][1] = 1;
      }
      usbMIDI.sendProgramChange(switchPCs[BANK][1], MIDI_PC_CHAN);
      MIDI.sendProgramChange(switchPCs[BANK][1], MIDI_PC_CHAN);
    }

    if (button3.fallingEdge()) {
      display.setCursor(70, 50);
      display.println(switchPCs[BANK][2]);
      if (switchStates[BANK][2] != 1) {
        resetSWITCHES();
        switchStates[BANK][2] = 1;
        ledStatus[BANK][2] = 1;
      }
      usbMIDI.sendProgramChange(switchPCs[BANK][2], MIDI_PC_CHAN);
      MIDI.sendProgramChange(switchPCs[BANK][2], MIDI_PC_CHAN);
    }

    if (button4.fallingEdge()) {
      display.setCursor(100, 50);
      display.println(switchPCs[BANK][3]);
      if (switchStates[BANK][3] != 1) {
        resetSWITCHES();
        switchStates[BANK][3] = 1;
        ledStatus[BANK][3] = 1;
      }
      usbMIDI.sendProgramChange(switchPCs[BANK][3], MIDI_PC_CHAN);
      MIDI.sendProgramChange(switchPCs[BANK][3], MIDI_PC_CHAN);
    }
  }

  display.display();
}

void resetSWITCHES() {
  for (int i = 0; i < 4; i++) { //Set all switch states to off
    for (int j = 0; j < 4; j++) {
      switchStates[i][j] = 0;
    }
  }
  for (int i = 0; i < 4; i++) { //Set all LED states to off
    for (int j = 0; j < 4; j++) {
      ledStatus[i][j] = 0;
    }
  }
}

void runmodeSETTINGS() {
  display.setTextColor(WHITE, BLACK);
  display.setTextSize(1);

  if (menuCat[0] == 0) menuLEVEL0(); //Display top level menu
  else if (menuCat[0] == 1) { //Default channels submenu
    menuLEVEL1();
  } else if (menuCat[0] == 2) { //Notes submenu
    menuLEVEL2();
  } else if (menuCat[0] == 3) { //CC submenu
    menuLEVEL3();
  } else if (menuCat[0] == 4) { //PC submenu
    menuLEVEL4();
  } else if (menuCat[0] == 5) { //Default runmode,timeouts
    menuLEVEL5();
  }

  //serialUPDATE(); //Debugging
  display.display();
}

void menuLEVEL0() { //Top level
  displayTEXT(0, "CHANGE SETTINGS");
  displayTEXT(1, "CHAN");
  displayTEXT(2, "NOTE");
  displayTEXT(3, " CC");
  displayTEXT(4, " PC");
  displayTEXT(5, "DEF");
  displayTEXT(6, "LOAD");
  displayTEXT(7, "SAVE");
  displayTEXT(8, "EXIT");

  if (button1.fallingEdge()) {
    blinkLED(1);
    menuCat[0] = 1;
  }
  if (button2.fallingEdge()) {
    blinkLED(2);
    menuCat[0] = 2;
  }
  if (button3.fallingEdge()) {
    blinkLED(3);
    menuCat[0] = 3;
  }
  if (button4.fallingEdge()) {
    blinkLED(4);
    menuCat[0] = 4;
  }
  if (button5.fallingEdge()) {
    blinkLED(1);
    menuCat[0] = 5;
  }
  if (button6.fallingEdge()) {
    blinkLED(2);
    eepromREAD();
    display.clearDisplay();
    displayTEXT(9, "LOADED FROM EEPROM");
    display.display();
    delay(2000);
    //serialUPDATE();
  }
  if (button7.fallingEdge()) {
    blinkLED(3);
    eepromUPDATE();
    display.clearDisplay();
    displayTEXT(0, "SAVED TO EEPROM");
    display.display();
    delay(2000);
    //serialUPDATE();
  }
  if (button8.fallingEdge()) {
    blinkLED(4);
    if (menuCat[0] == 0) {
      RUNMODE = 0;
      RUNMODE_TIMEOUT = RUNMODE_LONGTIMEOUT;
      resetSWITCHES();
      resetMENU();
      previousMillis = currentMillis;
    }
  }
}

void menuLEVEL1() { //Channel edit submenu
  valueCHECK();
  if (menuCat[1] > 0) {
    displayTEXT(1, "+1");
    displayTEXT(2, "+10");
    displayTEXT(5, "-1");
    displayTEXT(6, "-10");

    if (button1.fallingEdge()) {
      if (menuCat[1] == 1) MIDI_NOTE_CHAN++;
      if (menuCat[1] == 2) MIDI_CC_CHAN++;
      if (menuCat[1] == 3) MIDI_PC_CHAN++;
    }
    else if (button2.fallingEdge()) {
      if (menuCat[1] == 1) MIDI_NOTE_CHAN += 10;
      if (menuCat[1] == 2) MIDI_CC_CHAN += 10;
      if (menuCat[1] == 3) MIDI_PC_CHAN += 10;
    }
    else if (button5.fallingEdge()) {
      if (menuCat[1] == 1) MIDI_NOTE_CHAN--;
      if (menuCat[1] == 2) MIDI_CC_CHAN--;
      if (menuCat[1] == 3) MIDI_PC_CHAN--;
    }
    else if (button6.fallingEdge()) {
      if (menuCat[1] == 1) MIDI_NOTE_CHAN -= 10;
      if (menuCat[1] == 2) MIDI_CC_CHAN -= 10;
      if (menuCat[1] == 3) MIDI_PC_CHAN -= 10;
    }
  }

  if (menuCat[1] == 1) {
    displayTEXT(0, "NOTE CHAN:");
    displayVALUE(0, MIDI_NOTE_CHAN);
  }
  else if (menuCat[1] == 2) {
    displayTEXT(0, "CC CHAN:");
    displayVALUE(0, MIDI_CC_CHAN);
  }
  else if (menuCat[1] == 3) {
    displayTEXT(0, "PC CHAN:");
    displayVALUE(0, MIDI_PC_CHAN);
  }
  else if (menuCat[1] < 1) {
    displayTEXT(0, "CHANGE DEF CHAN");
    displayTEXT(1, "NOTE");
    displayTEXT(2, " CC");
    displayTEXT(3, " PC");

    if (button1.fallingEdge()) {
      blinkLED(1);
      menuCat[1] = 1;
    }
    if (button2.fallingEdge()) {
      blinkLED(2);
      menuCat[1] = 2;
    }
    if (button3.fallingEdge()) {
      blinkLED(3);
      menuCat[1] = 3;
    }

  }

  displayTEXT(8, "BACK");
  if (button8.fallingEdge()) {
    blinkLED(4);
    if (menuCat[1] < 1) resetMENU();
    else menuCat[1] = 0;
  }
}

void menuLEVEL2() { //Note edit submenu
  valueCHECK();
  if (menuCat[1] > 0) {
    if (menuCat[2] > 0) {
      displayTEXT(1, "+1");
      displayTEXT(2, "+10");
      displayTEXT(5, "-1");
      displayTEXT(6, "-10");

      if (button1.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchNotes[BANK][0]++;
          if (menuCat[1] == 2) switchNotes[BANK][1]++;
          if (menuCat[1] == 3) switchNotes[BANK][2]++;
          if (menuCat[1] == 4) switchNotes[BANK][3]++;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchVels[BANK][0]++;
          if (menuCat[1] == 2) switchVels[BANK][1]++;
          if (menuCat[1] == 3) switchVels[BANK][2]++;
          if (menuCat[1] == 4) switchVels[BANK][3]++;
        }
      }
      else if (button2.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchNotes[BANK][0] += 10;
          if (menuCat[1] == 2) switchNotes[BANK][1] += 10;
          if (menuCat[1] == 3) switchNotes[BANK][2] += 10;
          if (menuCat[1] == 4) switchNotes[BANK][3] += 10;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchVels[BANK][0] += 10;
          if (menuCat[1] == 2) switchVels[BANK][1] += 10;
          if (menuCat[1] == 3) switchVels[BANK][2] += 10;
          if (menuCat[1] == 4) switchVels[BANK][3] += 10;
        }
      }
      else if (button5.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchNotes[BANK][0]--;
          if (menuCat[1] == 2) switchNotes[BANK][1]--;
          if (menuCat[1] == 3) switchNotes[BANK][2]--;
          if (menuCat[1] == 4) switchNotes[BANK][3]--;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchVels[BANK][0]--;
          if (menuCat[1] == 2) switchVels[BANK][1]--;
          if (menuCat[1] == 3) switchVels[BANK][2]--;
          if (menuCat[1] == 4) switchVels[BANK][3]--;
        }
      }
      else if (button6.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchNotes[BANK][0] -= 10;
          if (menuCat[1] == 2) switchNotes[BANK][1] -= 10;
          if (menuCat[1] == 3) switchNotes[BANK][2] -= 10;
          if (menuCat[1] == 4) switchNotes[BANK][3] -= 10;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchVels[BANK][0] -= 10;
          if (menuCat[1] == 2) switchVels[BANK][1] -= 10;
          if (menuCat[1] == 3) switchVels[BANK][2] -= 10;
          if (menuCat[1] == 4) switchVels[BANK][3] -= 10;
        }
      }

    }
  }

  if (menuCat[1] < 1) {
    if (menuCat[2] < 1) {
      displayTEXT(16, "CHANGE BTN NOTE");
      displayTEXT(17, "BANK:");
      displayTEXT(18, BANK + 1);
      displayTEXT(1, "BTN1");
      displayTEXT(2, "BTN2");
      displayTEXT(3, "BTN3");
      displayTEXT(4, "BTN4");
      displayTEXT(5, " B-");
      displayTEXT(6, " B+");
      displayTEXT(8, "BACK");

      if (button1.fallingEdge()) {
        blinkLED(1);
        menuCat[1] = 1;
      }
      else if (button2.fallingEdge()) {
        blinkLED(2);
        menuCat[1] = 2;
      }
      else if (button3.fallingEdge()) {
        blinkLED(3);
        menuCat[1] = 3;
      }
      else if (button4.fallingEdge()) {
        blinkLED(4);
        menuCat[1] = 4;
      }
      else if (button5.fallingEdge()) {
        blinkLED(1);
        BANK--;
      }
      else if (button6.fallingEdge()) {
        blinkLED(2);
        BANK++;
      }
      else if (button8.fallingEdge()) {
        blinkLED(4);
        menuCat[0] = 0;
      }
    }
  }
  else if (menuCat[1] > 0) {
    if (menuCat[2] == 1) {
      if (menuCat[1] == 1) {
        displayTEXT(0, "BTN 1 NOTE:");
        displayVALUE(1, switchNotes[BANK][0]);
      }
      else if (menuCat[1] == 2) {
        displayTEXT(0, "BTN 2 NOTE:");
        displayVALUE(1, switchNotes[BANK][1]);
      }
      else if (menuCat[1] == 3) {
        displayTEXT(0, "BTN 3 NOTE:");
        displayVALUE(1, switchNotes[BANK][2]);
      }
      else if (menuCat[1] == 4) {
        displayTEXT(0, "BTN 4 NOTE:");
        displayVALUE(1, switchNotes[BANK][3]);
      }
    }
    else if (menuCat[2] == 2) {
      if (menuCat[1] == 1) {
        displayTEXT(0, "BTN 1 VEL:");
        displayVALUE(1, switchVels[BANK][0]);
      }
      else if (menuCat[1] == 2) {
        displayTEXT(0, "BTN 2 VEL:");
        displayVALUE(1, switchVels[BANK][1]);
      }
      else if (menuCat[1] == 3) {
        displayTEXT(0, "BTN 3 VEL:");
        displayVALUE(1, switchVels[BANK][2]);
      }
      else if (menuCat[1] == 4) {
        displayTEXT(0, "BTN 4 VEL:");
        displayVALUE(1, switchVels[BANK][3]);
      }
    }
    else if (menuCat[2] < 1) {
      if (menuCat[1] > 0) {
        displayTEXT(15, "BUTTON");
        displayVALUE(3, menuCat[1]);
        displayTEXT(14, "CHANGE WHICH ATTR");
        displayTEXT(1, "NOTE");
        displayTEXT(2, "VEL");

        if (button1.fallingEdge()) {
          blinkLED(1);
          menuCat[2] = 1;
        }
        else if (button2.fallingEdge()) {
          blinkLED(2);
          menuCat[2] = 2;
        }
      }
    }

    displayTEXT(8, "BACK");
    if (button8.fallingEdge()) {
      blinkLED(4);
      if (menuCat[2] < 1) {
        menuCat[1] = 0;
      } else menuCat[2] = 0;
    }
  }
}

void menuLEVEL3() { //CC edit submenu
  valueCHECK();
  if (menuCat[1] > 0) {
    if (menuCat[2] > 0) {
      displayTEXT(1, "+1");
      displayTEXT(2, "+10");
      displayTEXT(5, "-1");
      displayTEXT(6, "-10");

      if (button1.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchCCs[BANK][0]++;
          if (menuCat[1] == 2) switchCCs[BANK][1]++;
          if (menuCat[1] == 3) switchCCs[BANK][2]++;
          if (menuCat[1] == 4) switchCCs[BANK][3]++;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchCCValOns[BANK][0]++;
          if (menuCat[1] == 2) switchCCValOns[BANK][1]++;
          if (menuCat[1] == 3) switchCCValOns[BANK][2]++;
          if (menuCat[1] == 4) switchCCValOns[BANK][3]++;
        }
        else if (menuCat[2] == 3) {
          if (menuCat[1] == 1) switchCCValOffs[BANK][0]++;
          if (menuCat[1] == 2) switchCCValOffs[BANK][1]++;
          if (menuCat[1] == 3) switchCCValOffs[BANK][2]++;
          if (menuCat[1] == 4) switchCCValOffs[BANK][3]++;
        }
      }
      else if (button2.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchCCs[BANK][0] += 10;
          if (menuCat[1] == 2) switchCCs[BANK][1] += 10;
          if (menuCat[1] == 3) switchCCs[BANK][2] += 10;
          if (menuCat[1] == 4) switchCCs[BANK][3] += 10;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchCCValOns[BANK][0] += 10;
          if (menuCat[1] == 2) switchCCValOns[BANK][1] += 10;
          if (menuCat[1] == 3) switchCCValOns[BANK][2] += 10;
          if (menuCat[1] == 4) switchCCValOns[BANK][3] += 10;
        }
        else if (menuCat[2] == 3) {
          if (menuCat[1] == 1) switchCCValOffs[BANK][0] += 10;
          if (menuCat[1] == 2) switchCCValOffs[BANK][1] += 10;
          if (menuCat[1] == 3) switchCCValOffs[BANK][2] += 10;
          if (menuCat[1] == 4) switchCCValOffs[BANK][3] += 10;
        }
      }

      else if (button5.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchCCs[BANK][0]--;
          if (menuCat[1] == 2) switchCCs[BANK][1]--;
          if (menuCat[1] == 3) switchCCs[BANK][2]--;
          if (menuCat[1] == 4) switchCCs[BANK][3]--;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchCCValOns[BANK][0]--;
          if (menuCat[1] == 2) switchCCValOns[BANK][1]--;
          if (menuCat[1] == 3) switchCCValOns[BANK][2]--;
          if (menuCat[1] == 4) switchCCValOns[BANK][3]--;
        }
        else if (menuCat[2] == 3) {
          if (menuCat[1] == 1) switchCCValOffs[BANK][0]--;
          if (menuCat[1] == 2) switchCCValOffs[BANK][1]--;
          if (menuCat[1] == 3) switchCCValOffs[BANK][2]--;
          if (menuCat[1] == 4) switchCCValOffs[BANK][3]--;
        }
      }
      else if (button6.fallingEdge()) {
        if (menuCat[2] == 1) {
          if (menuCat[1] == 1) switchCCs[BANK][0] -= 10;
          if (menuCat[1] == 2) switchCCs[BANK][1] -= 10;
          if (menuCat[1] == 3) switchCCs[BANK][2] -= 10;
          if (menuCat[1] == 4) switchCCs[BANK][3] -= 10;
        }
        else if (menuCat[2] == 2) {
          if (menuCat[1] == 1) switchCCValOns[BANK][0] -= 10;
          if (menuCat[1] == 2) switchCCValOns[BANK][1] -= 10;
          if (menuCat[1] == 3) switchCCValOns[BANK][2] -= 10;
          if (menuCat[1] == 4) switchCCValOns[BANK][3] -= 10;
        }
        else if (menuCat[2] == 3) {
          if (menuCat[1] == 1) switchCCValOffs[BANK][0] -= 10;
          if (menuCat[1] == 2) switchCCValOffs[BANK][1] -= 10;
          if (menuCat[1] == 3) switchCCValOffs[BANK][2] -= 10;
          if (menuCat[1] == 4) switchCCValOffs[BANK][3] -= 10;
        }
      }
    }
  }

  if (menuCat[1] < 1) {
    if (menuCat[2] < 1) {
      displayTEXT(12, "CHANGE BTN CC");
      displayTEXT(17, "BANK:");
      displayTEXT(18, BANK + 1);
      displayTEXT(1, "BTN1");
      displayTEXT(2, "BTN2");
      displayTEXT(3, "BTN3");
      displayTEXT(4, "BTN4");
      displayTEXT(5, " B-");
      displayTEXT(6, " B+");
      displayTEXT(8, "BACK");

      if (button1.fallingEdge()) {
        blinkLED(1);
        menuCat[1] = 1;
      }
      else if (button2.fallingEdge()) {
        blinkLED(2);
        menuCat[1] = 2;
      }
      else if (button3.fallingEdge()) {
        blinkLED(3);
        menuCat[1] = 3;
      }
      else if (button4.fallingEdge()) {
        blinkLED(4);
        menuCat[1] = 4;
      }
      else if (button5.fallingEdge()) {
        blinkLED(1);
        BANK--;
      }
      else if (button6.fallingEdge()) {
        blinkLED(2);
        BANK++;
      }
      else if (button8.fallingEdge()) {
        blinkLED(4);
        menuCat[0] = 0;
        menuCat[1] = 0;
      }
    }
  }
  else if (menuCat[1] > 0) {
    if (menuCat[2] == 1) {
      if (menuCat[1] == 1) {
        displayTEXT(0, "BTN 1 CC:");
        displayVALUE(1, switchCCs[BANK][0]);
      }
      else if (menuCat[1] == 2) {
        displayTEXT(0, "BTN 2 CC:");
        displayVALUE(1, switchCCs[BANK][1]);
      }
      else if (menuCat[1] == 3) {
        displayTEXT(0, "BTN 3 CC:");
        displayVALUE(1, switchCCs[BANK][2]);
      }
      else if (menuCat[1] == 4) {
        displayTEXT(0, "BTN 4 CC:");
        displayVALUE(1, switchCCs[BANK][3]);
      }
    }
    else if (menuCat[2] == 2) {
      if (menuCat[1] == 1) {
        displayTEXT(0, "BTN 1 ValON:");
        displayVALUE(1, switchCCValOns[BANK][0]);
      }
      else if (menuCat[1] == 2) {
        displayTEXT(0, "BTN 2 ValON:");
        displayVALUE(1, switchCCValOns[BANK][1]);
      }
      else if (menuCat[1] == 3) {
        displayTEXT(0, "BTN 3 ValON:");
        displayVALUE(1, switchCCValOns[BANK][2]);
      }
      else if (menuCat[1] == 4) {
        displayTEXT(0, "BTN 4 ValON:");
        displayVALUE(1, switchCCValOns[BANK][3]);
      }
    }
    else if (menuCat[2] == 3) {
      if (menuCat[1] == 1) {
        displayTEXT(0, "BTN 1 ValOFF:");
        displayVALUE(1, switchCCValOffs[BANK][0]);
      }
      else if (menuCat[1] == 2) {
        displayTEXT(0, "BTN 2 ValOFF:");
        displayVALUE(1, switchCCValOffs[BANK][1]);
      }
      else if (menuCat[1] == 3) {
        displayTEXT(0, "BTN 3 ValOFF:");
        displayVALUE(1, switchCCValOffs[BANK][2]);
      }
      else if (menuCat[1] == 4) {
        displayTEXT(0, "BTN 4 ValOFF:");
        displayVALUE(1, switchCCValOffs[BANK][3]);
      }
    }
    else if (menuCat[2] < 1) {
      if (menuCat[1] > 0) {
        displayTEXT(15, "BUTTON");
        displayVALUE(3, menuCat[1]);
        displayTEXT(14, "CHANGE WHICH ATTR");
        displayTEXT(1, " CC");
        displayTEXT(2, "VON");
        displayTEXT(3, "VOFF");

        if (button1.fallingEdge()) {
          blinkLED(1);
          menuCat[2] = 1;
        }
        else if (button2.fallingEdge()) {
          blinkLED(2);
          menuCat[2] = 2;
        }
        else if (button3.fallingEdge()) {
          blinkLED(3);
          menuCat[2] = 3;
        }
      }
    }

    displayTEXT(8, "BACK");
    if (button8.fallingEdge()) {
      blinkLED(4);
      if (menuCat[2] < 1) {
        menuCat[1] = 0;
      } else menuCat[2] = 0;
    }
  }
}

void menuLEVEL4() { //PC edit submenu
  valueCHECK();
  if (menuCat[1] > 0) {
    displayTEXT(1, "+1");
    displayTEXT(2, "+10");
    displayTEXT(5, "-1");
    displayTEXT(6, "-10");

    if (button1.fallingEdge()) {
      if (menuCat[1] == 1) switchPCs[BANK][0]++;
      if (menuCat[1] == 2) switchPCs[BANK][1]++;
      if (menuCat[1] == 3) switchPCs[BANK][2]++;
      if (menuCat[1] == 4) switchPCs[BANK][3]++;
    }
    else if (button2.fallingEdge()) {
      if (menuCat[1] == 1) switchPCs[BANK][0] += 10;
      if (menuCat[1] == 2) switchPCs[BANK][1] += 10;
      if (menuCat[1] == 3) switchPCs[BANK][2] += 10;
      if (menuCat[1] == 4) switchPCs[BANK][3] += 10;
    }
    else if (button5.fallingEdge()) {
      if (menuCat[1] == 1) switchPCs[BANK][0]--;
      if (menuCat[1] == 2) switchPCs[BANK][1]--;
      if (menuCat[1] == 3) switchPCs[BANK][2]--;
      if (menuCat[1] == 4) switchPCs[BANK][3]--;
    }
    else if (button6.fallingEdge()) {
      if (menuCat[1] == 1) switchPCs[BANK][0] -= 10;
      if (menuCat[1] == 2) switchPCs[BANK][1] -= 10;
      if (menuCat[1] == 3) switchPCs[BANK][2] -= 10;
      if (menuCat[1] == 4) switchPCs[BANK][3] -= 10;
    }
  }

  if (menuCat[1] == 1) {
    displayTEXT(0, "BUTTON 1 PC:");
    displayVALUE(0, switchPCs[BANK][0]);
  }
  else if (menuCat[1] == 2) {
    displayTEXT(0, "BUTTON 2 PC:");
    displayVALUE(0, switchPCs[BANK][1]);
  }
  else if (menuCat[1] == 3) {
    displayTEXT(0, "BUTTON 3 PC:");
    displayVALUE(0, switchPCs[BANK][2]);
  }
  else if (menuCat[1] == 4) {
    displayTEXT(0, "BUTTON 4 PC:");
    displayVALUE(0, switchPCs[BANK][3]);
  }

  else if (menuCat[1] < 1) {
    displayTEXT(12, "CHANGE BTN PC");
    displayTEXT(17, "BANK:");
    displayTEXT(18, BANK + 1);
    displayTEXT(1, "BTN1");
    displayTEXT(2, "BTN2");
    displayTEXT(3, "BTN3");
    displayTEXT(4, "BTN4");
    displayTEXT(5, " B-");
    displayTEXT(6, " B+");
    displayTEXT(8, "BACK");

    if (button1.fallingEdge()) {
      blinkLED(1);
      menuCat[1] = 1;
    }
    else if (button2.fallingEdge()) {
      blinkLED(2);
      menuCat[1] = 2;
    }
    else if (button3.fallingEdge()) {
      blinkLED(3);
      menuCat[1] = 3;
    }
    else if (button4.fallingEdge()) {
      blinkLED(4);
      menuCat[1] = 4;
    }
    else if (button5.fallingEdge()) {
      blinkLED(1);
      BANK--;
    }
    else if (button6.fallingEdge()) {
      blinkLED(2);
      BANK++;
    }
    else if (button8.fallingEdge()) {
      blinkLED(4);
      menuCat[0] = 0;
      menuCat[1] = 0;
    }
  }
  if (menuCat[1] > 0) {
    displayTEXT(8, "BACK");
    if (button8.fallingEdge()) {
      blinkLED(4);
      menuCat[1] = 0;
    }
  }
}

void menuLEVEL5() { //Defaults submenu
  valueCHECK();
  if ((menuCat[1] == 1) || (menuCat[1] == 2)) {
    displayTEXT(1, "+1");
    displayTEXT(2, "+5");
    displayTEXT(5, "-1");
    displayTEXT(6, "-5");
  }

  if (menuCat[1] > 0) {
    if (button1.fallingEdge()) {
      if (menuCat[1] == 1) RUNMODE_DEFAULT++;
      if (menuCat[1] == 2) MENU_TIMEOUT += 1000;
    }
    else if (button2.fallingEdge()) {
      if (menuCat[1] == 1) RUNMODE_DEFAULT += 5;
      if (menuCat[1] == 2) MENU_TIMEOUT += 5000;
    }
    else if (button5.fallingEdge()) {
      if (menuCat[1] == 1) RUNMODE_DEFAULT--;
      if (menuCat[1] == 2) MENU_TIMEOUT -= 1000;
    }
    else if (button6.fallingEdge()) {
      if (menuCat[1] == 1) RUNMODE_DEFAULT -= 5;
      if (menuCat[1] == 2) MENU_TIMEOUT -= 5000;
    }
  }

  if (menuCat[1] == 1) {
    displayTEXT(9, "DEF RUNMODE:");
    displayVALUE(0, RUNMODE_DEFAULT);
  }
  else if (menuCat[1] == 2) {
    displayTEXT(9, "MENU TIMEOUT:");
    displayVALUE(0, (MENU_TIMEOUT / 1000));
  }
  else if (menuCat[1] < 1) {
    displayTEXT(0, "CHANGE DEFAULT");
    displayTEXT(1, "RUNM"); //Default runmode
    displayTEXT(2, "MENU"); //Menu timeout

    if (button1.fallingEdge()) {
      blinkLED(1);
      menuCat[1] = 1;
    }
    if (button2.fallingEdge()) {
      blinkLED(2);
      menuCat[1] = 2;
    }
  }

  displayTEXT(8, "BACK");
  if (button8.fallingEdge()) {
    blinkLED(4);
    if (menuCat[1] < 1) resetMENU();
    else menuCat[1] = 0;
  }
}

void displayTEXT(int displayPOS, String displayCONTENT) {
  if (displayPOS == 0) display.setCursor(20, 34); //Centre of display
  else if (displayPOS == 1) display.setCursor(10, 55); //Button 1
  else if (displayPOS == 2) display.setCursor(40, 55); //Button 2
  else if (displayPOS == 3) display.setCursor(70, 55); //Button 3
  else if (displayPOS == 4) display.setCursor(100, 55); //Button 4
  else if (displayPOS == 5) display.setCursor(10, 10); //Button 5
  else if (displayPOS == 6) display.setCursor(40, 10); //Button 6
  else if (displayPOS == 7) display.setCursor(70, 10); //Button 7
  else if (displayPOS == 8) display.setCursor(100, 10); //Button 8
  else if (displayPOS == 9) display.setCursor(10, 34); //Left of centre
  else if (displayPOS == 10) display.setCursor(12, 28); //Left of centre, up from centre
  else if (displayPOS == 11) display.setCursor(18, 28); //similar but less left
  else if (displayPOS == 12) display.setCursor(26, 28); //PC heading
  else if (displayPOS == 13) display.setCursor(30, 38); //Timeout
  else if (displayPOS == 14) display.setCursor(14, 38); //Choose attribute
  else if (displayPOS == 15) display.setCursor(40, 28);
  else if (displayPOS == 16) display.setCursor(22, 28); //Note heading
  else if (displayPOS == 17) display.setCursor(35, 18); //Bank display
  else if (displayPOS == 18) display.setCursor(87, 18); //Bank display
  display.println(displayCONTENT);
}

void displayVALUE(int displayPOS, int displayCONTENT) {
  if (displayPOS == 0) display.setCursor(90, 34); //Channel values
  else if (displayPOS == 1) display.setCursor(100, 34);
  else if (displayPOS == 2) display.setCursor(95, 38);
  else if (displayPOS == 3) display.setCursor(85, 28);
  display.println(displayCONTENT);
}

void blinkLED(int ledNUM) {
  wipeCOUNT = 0;
  digitalWrite(ledPin[ledNUM - 1], LOW);
  digitalWrite(ledPin[ledNUM - 1], HIGH);
  delay(ledDelay);
  digitalWrite(ledPin[ledNUM - 1], LOW);
  delay(ledDelay / 3);
  digitalWrite(ledPin[ledNUM - 1], HIGH);
  delay(ledDelay);
  digitalWrite(ledPin[ledNUM - 1], LOW);
}

void eepromREAD() { //Read values from EEPROM
  int h;  //Temporary variable

  if (EEPROM.read(1000) == 1) { //Check the EEPROM update flag (address 1000) to see if custom values have been written. If so, load those values.
    //Channels
    MIDI_NOTE_CHAN = EEPROM.read(0);
    MIDI_CC_CHAN = EEPROM.read(1);
    MIDI_PC_CHAN = EEPROM.read(2);

    //Defaults
    RUNMODE_DEFAULT = EEPROM.read(5);
    MENU_TIMEOUT = (EEPROM.read(6) * 1000);

    h = 10;
    //Notes
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchNotes[i][j] = EEPROM.read(h);
        h++;
      }
    }

    //Velocities
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchVels[i][j] = EEPROM.read(h);
        h++;
      }
    }

    //CC
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchCCs[i][j] = EEPROM.read(h);
        h++;
      }
    }

    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchCCValOns[i][j] = EEPROM.read(h);
        h++;
      }
    }

    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchCCValOffs[i][j] = EEPROM.read(h);
        h++;
      }
    }

    //PC
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchPCs[i][j] = EEPROM.read(h);
        h++;
      }
    }


  } else { //Otherwise load the default values */
    MIDI_NOTE_CHAN = 11;
    MIDI_CC_CHAN = 11;
    MIDI_PC_CHAN = 11;

    RUNMODE_DEFAULT = 1;
    MENU_TIMEOUT = 11000;

    //Set default notes from 60 through 75

    h = 60;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchNotes[i][j] = h;
        h++;
      }
    }

    //Set all switches to default velocity of 64
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchVels[i][j] = 64;
      }
    }

    //Set default CC numbers from 20 through 35
    h = 20;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchCCs[i][j] = h;
        h++;
      }
    }

    //Set all CC on values to default of 127
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchCCValOns[i][j] = 127;
      }
    }

    //Set all CC off values to default of 0
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchCCValOffs[i][j] = 0;
      }
    }

    //Set default PC numbers from 1 through 16
    h = 1;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        switchPCs[i][j] = h;
        h++;
      }
    }
  }
}

void eepromUPDATE() {
  int h;

  EEPROM.update(1000, 1); //Set EEPROM write flag so that it is loaded next boot

  EEPROM.update(0, MIDI_NOTE_CHAN);
  EEPROM.update(1, MIDI_CC_CHAN);
  EEPROM.update(2, MIDI_PC_CHAN);

  EEPROM.update(5, RUNMODE_DEFAULT);
  EEPROM.update(6, (MENU_TIMEOUT / 1000));

  h = 10;
  //Notes
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      EEPROM.update(h, switchNotes[i][j]);
      h++;
    }
  }

  //Velocities
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      EEPROM.update(h, switchVels[i][j]);
      h++;
    }
  }

  //CC
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      EEPROM.update(h, switchCCs[i][j]);
      h++;
    }
  }

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      EEPROM.update(h, switchCCValOns[i][j]);
      h++;
    }
  }

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      EEPROM.update(h, switchCCValOffs[i][j]);
      h++;
    }
  }

  //PC
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      EEPROM.update(h, switchPCs[i][j]);
      h++;
    }
  }
}

void serialUPDATE() {

}

void valueCHECK() { //Check values when editing to keep things real
  if (BANK < 0) BANK = 0;
  else if (BANK > 3) BANK = 3;

  if (MIDI_NOTE_CHAN < 1) MIDI_NOTE_CHAN = 1 ;
  else if (MIDI_NOTE_CHAN > 16) MIDI_NOTE_CHAN = 16;
  if (MIDI_CC_CHAN < 1) MIDI_CC_CHAN = 1;
  else if (MIDI_CC_CHAN > 16) MIDI_CC_CHAN = 16;
  if (MIDI_PC_CHAN < 1) MIDI_PC_CHAN = 1;
  else if (MIDI_PC_CHAN > 16) MIDI_PC_CHAN = 16;

  if (RUNMODE_DEFAULT < 1) RUNMODE_DEFAULT = 1;
  else if (RUNMODE_DEFAULT > 5) RUNMODE_DEFAULT = 5;
  if (MENU_TIMEOUT < 1000) MENU_TIMEOUT = 1000;
  else if (MENU_TIMEOUT > 100000) MENU_TIMEOUT = 1000;

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      if (switchNotes[i][j] < 0) switchNotes[i][j] = 0;
      else if (switchNotes[i][j] > 127) switchNotes[i][j] = 127;

      if (switchVels[i][j] < 0) switchVels[i][j] = 0;
      else if (switchVels[i][j] > 127) switchVels[i][j] = 127;

      if (switchCCs[i][j] < 0) switchCCs[i][j] = 0;
      else if (switchCCs[i][j] > 127) switchCCs[i][j] = 127;

      if (switchCCValOns[i][j] < 0) switchCCValOns[i][j] = 0;
      else if (switchCCValOns[i][j] > 127) switchCCValOns[i][j] = 127;

      if (switchCCValOffs[i][j] < 0) switchCCValOffs[i][j] = 0;
      else if (switchCCValOffs[i][j] > 127) switchCCValOffs[i][j] = 127;

      if (switchPCs[i][j] < 0) switchPCs[i][j] = 0;
      else if (switchPCs[i][j] > 127) switchPCs[i][j] = 127;
    }
  }
}

void resetMENU() {
  for (int i = 0; i < 3; i++) menuCat[i] = 0; //Reset menu level tracker
}

void ledUPDATE() {
  for (int i = 0; i < 4; i++) {
    if (ledStatus[BANK][i] == 1) {
      digitalWrite(ledPin[i], HIGH);
    }
    else digitalWrite(ledPin[i], LOW);
  }
}
