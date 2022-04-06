// This code is placed in the public domain by its author, Miles Phillips
// May 2020.
// Based on public domain code by Ian Harvey.

// Hardware Hookup:
// Mux Breakout ----------- Arduino
//      S0 ------------------- 2
//      S1 ------------------- 3
//      S2 ------------------- 4
//      Z -------------------- A0
//     VCC ------------------- 5V
//     GND ------------------- GND
//     (VEE should be connected to GND)
// 
// The multiplexers independent I/O (Y0-Y7) can each be wired
// up to a potentiometer or any other analog signal-producing
// component.


// incorporating trigger code from Piezo Drum Pad v0.1
// refactored 'ch' to 'pad' and 'NCHANNELS' to 'PADS'
// 0.3 - Adding MUX

#include "MIDIUSB.h" // Needs MIDIUSB library to be installed

// MUX hardware arrangement

const int selectPins[3] = {2, 3, 4}; // S0~2, S1~3, S2~4
const int zOutput = 5; 
const int zInput = A0; // Connect common (Z) to A0 (analog input)

//----

static void MIDI_setup();
static void MIDI_noteOn(int ch, int note, int velocity);
static void MIDI_noteOff(int ch, int note);

const int MIDI_CHANNEL=1;
const int BAUD_RATE=115200;

const int scalePin = A1;
const int keyPin = A2;
const int PADS = 8;
const int KEYRANGE = 13; // Range in semitones
const int NSCALES = 7; // Number of modes/scales
const int BASENOTE = 50; // Lowest available note (MIDI number)

// const int inPins[PADS] = { A2, A3 }; // Not needed with multiplexer
const int thresholdLevel[PADS] = { 1, 1, 1, 1, 1, 1, 1, 1 }; // ADC reading to trigger; lower => more sensitive
const int maxLevel[PADS] = { 60, 60, 60, 60, 60, 60, 60, 60 }; // ADC reading for full velocity; lower => more sensitive

const int SCALES[NSCALES][8] = {
  {0,2,4,5,7,9,11,12}, // Ionian, Major
  {0,4,7,11,12,16,18,19}, // Aegean
  {0,3,7,10,12,14,15,19}, // Voyager
  {0,3,7,8,10,13,15,16}, // Equinox
  {0,5,7,8,10,12,14,15}, // Dorian
  {0,7,8,10,12,14,15,19}, // Phrygian
  {0,5,7,8,12,13,17,19}, // Ake Bono 
};

int midiNotes[PADS];
int scaleCounter;
int key;
int scale;

static unsigned int vmax[PADS] = { 0 };
static unsigned int trigLevel[PADS];
static unsigned int counter[PADS] = { 0 };

static unsigned int CTR_NOTEON = 10; // Roughly 5ms sampling peak voltage
static unsigned int CTR_NOTEOFF = CTR_NOTEON + 30; // Duration roughly 15ms 
// 0 -> not triggered
// 1..CTR_NOTEON -> sampling note on
// CTR_NOTEON+1 .. CTR_NOTEOFF -> note off


static int statusPin = 2;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(BAUD_RATE);
  analogReference(DEFAULT);
  pinMode(statusPin, OUTPUT);
  pinMode(zInput, INPUT);
  digitalWrite(statusPin, LOW);
  
  for (int i = 0; i < PADS; i++)
  {
  //   pinMode(inPins[i], INPUT);  // Not needed with MUX
  //   analogRead(inPins[i]);
    trigLevel[i] = thresholdLevel[i];
  }

  //mux setup

  for (int i=0; i<3; i++)
  {
    pinMode(selectPins[i], OUTPUT);
    digitalWrite(selectPins[i], HIGH);
  }

  MIDI_setup();
}

void loop() 
{    
  // volumeCheck();//check the volume knob 

  drumCheck();//check if any of the capacitive pads have been touched

  // dcValCheck();//check the decay knob

  // buttonCheck();//check for button presses to change the scale 

  scaleCounter++;
  
  if(scaleCounter == 200)//check scale and key periodically
  {
     scaleCheck();
     scaleCounter = 0;
  }

  // counter++;
  // if(counter == 10000);//don't check battery all the time, slows opperation
  // {
  //   getBattery();
  //   counter = 0;
  // }
  
  // oledPrint();//print to TeensyView

}

void scaleCheck() {
  int sv = analogRead(scalePin);
  int s = map(sv, 0, 1023, 0, NSCALES);
  int kv = analogRead(keyPin);
  int k = map(kv, 0, 1023, 0, KEYRANGE);
  if ( s != scale || k != key )
  {
    int n;
    for (n=0; n < PADS; n++)
    {
      midiNotes[n] = BASENOTE + k + SCALES[s][n];
    }
    digitalWrite(statusPin, HIGH);
    s = scale;
    k = key;
  }
}


void drumCheck() {
  int pad;
  for (pad=0; pad < PADS; pad++)
  {
    selectMuxPin(pad);
    unsigned int v = analogRead(zInput);
    if ( counter[pad] == 0 )
    {
      if ( v >= trigLevel[pad] )
      {
        vmax[pad] = v;
        counter[pad] = 1;
        digitalWrite(statusPin, HIGH);
      }
    }
    else
    {
      if ( v > vmax[pad] )
        vmax[pad] = v;
      counter[pad]++;
      
      if ( counter[pad] == CTR_NOTEON )
      {
        long int vel = ((long int)vmax[pad]*127)/maxLevel[pad];
        //Serial.println(vel);
        if (vel < 5) vel = 5;
        if (vel > 127) vel = 127;
        MIDI_noteOn(MIDI_CHANNEL, midiNotes[pad], vel);
        trigLevel[pad] = vmax[pad];
      }
      else if ( counter[pad] >= CTR_NOTEOFF )
      {
        MIDI_noteOff(MIDI_CHANNEL, midiNotes[pad]);
        counter[pad] = 0;
        digitalWrite(statusPin, LOW);
      }
    }

    // The signal from the piezo is a damped oscillation decaying with
    // time constant 8-10ms. Prevent false retriggering by raising 
    // trigger level when first triggered, then decaying it to the 
    // threshold over several future samples.

    // removed for 8 pads
    
    // trigLevel[pad] = ((trigLevel[pad] * 2) + (thresholdLevel[pad] * 1)) / 3;
    trigLevel[pad] = thresholdLevel[pad];
  }

}

// MIDI Code
//
// See https://www.midi.org/specifications/item/table-1-summary-of-midi-message

void MIDI_setup()
{

}

void MIDI_noteOn(int ch, int note, int velocity)
{
  midiEventPacket_t noteOn = {0x09, 0x90 | (ch-1), note & 0x7F, velocity & 0x7F};
  MidiUSB.sendMIDI(noteOn);
  MidiUSB.flush();
}

void MIDI_noteOff(int ch, int note)
{
  midiEventPacket_t noteOff = {0x08, 0x80 | (ch-1), note, 1};
  MidiUSB.sendMIDI(noteOff);
  MidiUSB.flush();
}

// The selectMuxPin function sets the S0, S1, and S2 pins
// accordingly, given a pin from 0-7.
void selectMuxPin(byte pin)
{
  if (pin > 7) return; // Exit if pin is out of scope
  for (int i=0; i<3; i++)
  {
    if (pin & (1<<i))
      digitalWrite(selectPins[i], HIGH);
    else
      digitalWrite(selectPins[i], LOW);
  }
}
