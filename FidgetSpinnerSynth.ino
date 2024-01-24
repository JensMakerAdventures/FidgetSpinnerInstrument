// GLOBAL INCLUDES
#include <MozziGuts.h>
#include <EventDelay.h>
#include <mozzi_rand.h>
#include <mozzi_midi.h>
#include <Midier.h>

// HARDWARE CONFIG
const int N_FIDGET_SPINNERS = 8;
const int ENCODER_PINS[N_FIDGET_SPINNERS] = {2,3,5,7,14,15,16,10};
const int N_POTENTIOMETERS = 7;
const int POTENTIOMETER_PINS[N_POTENTIOMETERS] = {A0, A1, A2, A3, A6, A7, A8}; // pay attention here, the order is linked to KnobFunction order!
enum class KnobFunction {PITCH, TEMPO, MODE, ARPTYPE, SOUNDTYPE, RHYTHM, EFFECT, LENGTH};
bool potMetersAreInverted = true;

// SOFTWARE CONFIG, DON'T TOUCH
const int CONTROL_RATE_HZ = 128; // 128 Hz, so 15.6 ms is needed to get  max fidget spinner speed correctly
const float CONTROL_RATE_MS = 1.0/CONTROL_RATE_HZ*1000.0;
const int SPEED_CALC_DIVIDER = 25;
const int POT_CALC_DIVIDER = 4;
const int POT_HYSTERESIS = 10;
const int PULSES_PER_REVOLUTION = 3;
const int lowBPM = 60;
const int highBPM = 180;
const int lowBPMdelay = 60000 / lowBPM; // [ms] for quarter notes
const int highBPMdelay = 60000 / highBPM; // [ms] for quarter notes
const int potScaleDown = 5;
const int potValueMax = 1023;

// MIDI
#include <midi_serialization.h>
#include <usbmidi.h>
int ccValuesSpinners[N_FIDGET_SPINNERS];
int ccValuesPotentiometers[N_POTENTIOMETERS];
int ccChannelSpinners = 1;
int ccChannelPotentiometers = 2;
int ccChannelNotes = 3;
int ccOffset = 14; // Start at control 14, others are usually for something else.
int middleOctave = 4; // Octave to play around on speaker and midi notes to send

// Wavetable synth
#include <Oscil.h>
#include <ADSR.h>
#include <tables/sin512_int8.h>
#include <tables/triangle512_int8.h>
#include <tables/saw_analogue512_int8.h>
#include <tables/square_analogue512_int8.h>

Oscil <512, AUDIO_RATE> aOscil;
EventDelay noteDelay; // for triggering the envelope
ADSR <AUDIO_RATE, AUDIO_RATE> envelope;
unsigned int attack, decay, sustain, release_ms;

// Multinote
// audio volumes updated each control interrupt and reused in audio till next control
#include <tables/cos8192_int8.h>
char v[N_FIDGET_SPINNERS];
float multiNotes[N_FIDGET_SPINNERS];

Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos0(SQUARE_ANALOGUE512_DATA);
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos1(SQUARE_ANALOGUE512_DATA);
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos2(SQUARE_ANALOGUE512_DATA);
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos3(SQUARE_ANALOGUE512_DATA);
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos4(SQUARE_ANALOGUE512_DATA);
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos5(SQUARE_ANALOGUE512_DATA);
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos6(SQUARE_ANALOGUE512_DATA);
Oscil<SQUARE_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aCos7(SQUARE_ANALOGUE512_DATA);

// Bamboo sound sampler
#include <Sample.h> // Sample template
#include <samples/bamboo/bamboo_00_2048_int8.h> // wavetable data
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo0(BAMBOO_00_2048_DATA); // use: Sample <table_size, update_rate> SampleName (wavetable)
EventDelay kTriggerDelay; // for scheduling audio gain changes

// Misc.
enum class SoundType {SINE, TRIANGLE, SAW, SQUARE, BAMBOO, LENGTH}; 
SoundType soundType; // KNOB

// Filter effect
#include <ResonantFilter.h>
LowPassFilter rf; // Used for nice low pass
LowPassFilter rf2; // used for glitching
uint8_t resonance; 
enum class FxType {NONE, FILTER, TREMOLO, WARP, GLITCH, LENGTH}; 
FxType fxType;

// Warp effect
Oscil<COS8192_NUM_CELLS, CONTROL_RATE> kWarp(COS8192_DATA); // speed is constant and set in setup

// Tremolo effect
Oscil<COS8192_NUM_CELLS, CONTROL_RATE> kTremolo(COS8192_DATA);
char vTremolo;

//Arp
enum class ArpType {UPWARDS, MULTI_NOTE, SPEED_BASED, LENGTH};
ArpType arpType;
midier::Degree scaleDegree = 1; // counter for the arp
midier::Mode arpMode = midier::Mode::Ionian; // KNOB
midier::Note scaleRoot = midier::Note::G; // KNOB
int noteTime = 100; // [ms] KNOB calculated from knobs tempo and notespeed if constant note time is enabled
int tempo = 120; // KNOB [BPM]
int noteSpeedMultiplier = 1; // 1/4th (1) 1/8th (2) 1/16th (4) 1/32th (8) notes multiplication factor
int prevPotVal[N_POTENTIOMETERS] = {-1, -1, -1, -1}; // Setting all to -1 makes sure in setup we encounter a change and everthing initializes OK.
int potVal[N_POTENTIOMETERS];
long potValTemp[N_POTENTIOMETERS];
long potCalcCount;
long speedCalcCount;

long counts[N_FIDGET_SPINNERS];
bool prevState[N_FIDGET_SPINNERS];
bool state[N_FIDGET_SPINNERS];
byte speed[N_FIDGET_SPINNERS]; // rotations per second of the spinner
byte maxSpeed = 20; // rotations per second
int variableArpSpeed = 0;

void setup(){
  startMozzi(CONTROL_RATE_HZ); // Run in 64 Hz => 15.6 ms cycle time for encoders.
  for(int i = 0; i < N_FIDGET_SPINNERS; i++)
  {
    pinMode(ENCODER_PINS[i], INPUT);
  }

  adcDisconnectAllDigitalIns(); // Mozzi docs says this helps power consumption and noise
  Serial.begin(9600);
  processPotentiometers();
  kWarp.setFreq(3); // for now we have a constant frequency for the warp effect (this one is high because used in audio loop, not control loop)
  kTremolo.setFreq(3); // constant lfo frequency for tremolo effect (since this is run in control loop, not audio loop)
}

void sendCC(uint8_t channel, uint8_t control, uint8_t value) {
	USBMIDI.write(0xB0 | (channel & 0xf));
	USBMIDI.write(control & 0x7f);
	USBMIDI.write(value & 0x7f);
  /*
  Serial.print("Midi channel: ");
  Serial.println((int)channel);
  Serial.print("Midi control: ");
  Serial.println((int)control);
  Serial.print("Midi value: ");
  Serial.println((int)value);
  */
}

void sendNote(uint8_t channel, uint8_t note, uint8_t velocity) {
	USBMIDI.write((velocity != 0 ? 0x90 : 0x80) | (channel & 0xf));
	USBMIDI.write(note & 0x7f);
	USBMIDI.write(velocity &0x7f);
}

midier::Note nextArpNote()
{
  for(int i = 0; i < 8; i++)
  {
    if (scaleDegree > 8)
    {
      scaleDegree = 1;
    }
    if(speed[scaleDegree-1] != 0)
    {
        // determine the interval from the root of the scale to the chord of this scale degree
      const midier::Interval interval = midier::scale::interval(arpMode, scaleDegree);
      
      // calculate the root note of the chord of this scale degree
      const midier::Note chordRoot = scaleRoot + interval;   
      sendNote(ccChannelNotes, (int)midier::midi::number(chordRoot, middleOctave), map(speed[scaleDegree-1], 0, maxSpeed, 0, 127));
      scaleDegree++;
      return chordRoot;
    }  
    scaleDegree++;
  }
}


byte randomGain(){
  return rand(200) + 30;
}

struct gainstruct{
  byte gain0;
  byte gain1;
}
gains;

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void updateAllSpeeds()
{
  for(int i = 0; i < N_FIDGET_SPINNERS; i++)
  {
    updateEncoderCounts(i);
  }

  speedCalcCount++;
  
  // Calculate the speed averaging the pulses made over the last cycles
  if(speedCalcCount == SPEED_CALC_DIVIDER)
  {
    for(int i = 0; i < N_FIDGET_SPINNERS; i++)
    {
      speed[i] = byte((float)counts[i] / (float)PULSES_PER_REVOLUTION / ((float)SPEED_CALC_DIVIDER*((float)CONTROL_RATE_MS/1000.0)));
      if (speed[i] > maxSpeed) {speed[i] = maxSpeed;}
    }
    memset(counts,0,sizeof(counts)); // set all counts back to zero
    speedCalcCount = 0;
  }
}

void updateEncoderCounts(int i) // index 0-N_FIDGET_SPINNERS
{
  if ((i<0) | (i > N_FIDGET_SPINNERS - 1)) {return;}

  state[i] = digitalRead(ENCODER_PINS[i]);
  if (state[i] != prevState[i])
  {
    counts[i]++;
  }
  prevState[i] = state[i];
}

byte calcVariableArpSpeed()
{
  int sum = 0;
  byte nonZeroEntries = 0;
  for (int i = 0; i < N_FIDGET_SPINNERS; i++)
  {
    if (speed[i]!=0)
    {
      sum = sum + speed[i];
      if (speed[i] != 0)
      {
        nonZeroEntries++;
      }
    }
  }

  if (sum > maxSpeed)
  {
    sum = maxSpeed;
  }
  if(nonZeroEntries != 0)
   {
    sum = sum / nonZeroEntries + 2*nonZeroEntries;
  }
  else
  {
    sum = 0;
  }
  return sum;  
}

void prepareSound(SoundType mode)
{
  if (arpType == ArpType::MULTI_NOTE)
  {
    for(int i = 0; i < 8; i++)
    {
      v[i] = speed[i]*6;
      
      // determine the interval from the root of the scale to the chord of this scale degree
      const midier::Interval interval = midier::scale::interval(arpMode, i+1);
      
      // calculate the root note of the chord of this scale degree
      const midier::Note chordRoot = scaleRoot + interval;

      midier::midi::Number midiNote = midier::midi::number(chordRoot, middleOctave);

      switch(i)
      {
        case(0):
        {
          aCos0.setFreq((float)mtof((int)midiNote));
          break;
        }
        case(1):
        {
          aCos1.setFreq((float)mtof((int)midiNote));
          break;
        }
        case(2):
        {
          aCos2.setFreq((float)mtof((int)midiNote));
          break;
        }
        case(3):
        {
          aCos3.setFreq((float)mtof((int)midiNote));
          break;
        }
        case(4):
        {
          aCos4.setFreq((float)mtof((int)midiNote));
          break;
        }
        case(5):
        {
          aCos5.setFreq((float)mtof((int)midiNote));
          break;
        }
        case(6):
        {
          aCos6.setFreq((float)mtof((int)midiNote));
          break;
        }
        case(7):
        {
          aCos7.setFreq((float)mtof((int)midiNote));
          break;
        }
      }
    }
  }
  if (arpType == ArpType::SPEED_BASED | arpType == ArpType::UPWARDS)
  {
    variableArpSpeed = calcVariableArpSpeed();
    if (variableArpSpeed == 0)
    {
      return;
    }

    if (mode==SoundType::SINE | mode==SoundType::TRIANGLE | mode==SoundType::SQUARE | mode==SoundType::SAW)
    {
      if((noteDelay.ready()))
      {       
        if (arpType == ArpType::UPWARDS)
        {
          midier::Note note = nextArpNote();
          midier::midi::Number midiNote = midier::midi::number(note, middleOctave);
          aOscil.setFreq((float)mtof((int)midiNote)); // (float) cast IS needed, when using the int setFreq function it rounds a bunch of notes (12, 13, 14) all to playing at 12 somehow    
        }
        if(arpType == ArpType::SPEED_BASED)
        {
          byte midi_note = 75 + rand(-2, 2) + map(variableArpSpeed, 0, maxSpeed, -10, 10);
          aOscil.setFreq((int)mtof(midi_note));
        }
        attack = 5;
        decay = 50;
        sustain = 50;
        release_ms = noteTime - 30 - attack - decay-sustain; // 30 ms of transition time
        //release_ms = 50;
        byte attack_level = 255;
        byte decay_level = 255;
        envelope.setADLevels(attack_level,decay_level);
        envelope.setTimes(attack,decay,sustain,release_ms);  
        envelope.noteOn();
        if(arpType == ArpType::UPWARDS)
        {
          noteDelay.start(noteTime);
        }
        if(arpType == ArpType::SPEED_BASED)
        {
          noteDelay.start(map(variableArpSpeed, 0, maxSpeed, 350, 30));
        }
      }
      return;
    }
  
    if(mode == SoundType::BAMBOO)
    {
      if(kTriggerDelay.ready()){
        if(arpType == ArpType::UPWARDS)
        {
          kTriggerDelay.set(noteTime);
        }
        if(arpType == ArpType::SPEED_BASED)
        {
          kTriggerDelay.set(map(variableArpSpeed, 0, maxSpeed, 500, 30));
        }
        
        midier::midi::Number midiNote;
        if (arpType == ArpType::UPWARDS)
        {
          midier::Note note = nextArpNote();
          midiNote = midier::midi::number(note, 2) + 6; // prescale 2 octaves up, mtof makes mistakes in lower ranges. Offset 6 necessary to be in tune with synths.
          aBamboo0.setFreq((float)(mtof((int)midiNote)/8.0*0.98)); // (float) cast IS needed, when using the int setFreq function it rounds a bunch of notes (12, 13, 14) all to playing at 12 somehow. scale down earlier scale up. 0.98 multiplier is for fine tuning to other synths
        }
        if(arpType == ArpType::SPEED_BASED)
        {
          float pitchChange = mapFloat((float)variableArpSpeed, 0.0, (float)maxSpeed, 0.8, 1.5);
          aBamboo0.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS)*pitchChange);
        }
        gains.gain0 = randomGain();
        aBamboo0.start();
        kTriggerDelay.start();
      }
    }
  }
}

void setNewArpTime()
{
  noteTime = map(tempo, lowBPM, highBPM, lowBPMdelay, highBPMdelay) / noteSpeedMultiplier;
}

void handlePotValChange(int pot)
{
  switch((KnobFunction)pot)
  {
    case KnobFunction::PITCH:
    {
      scaleRoot = midier::Note::C;
      int semiTonesToAdd = map(potVal[pot], 0, potValueMax+POT_HYSTERESIS, 0, 12);
      scaleRoot = (midier::Note)((int)scaleRoot + semiTonesToAdd);
      break;
    }
    case KnobFunction::MODE:
    {
      arpMode = (midier::Mode) 0;
      int arpModeStepsToAdd = map(potVal[pot], 0, potValueMax+POT_HYSTERESIS, 0, (int)midier::Mode::Count);
      arpMode = (midier::Mode)((int)arpMode + arpModeStepsToAdd);
      break;
    }
    case KnobFunction::ARPTYPE:
    {
      arpType = (ArpType)map(potVal[pot], 0, potValueMax+POT_HYSTERESIS, 0, (int)ArpType::LENGTH);
      break;
    }

    case KnobFunction::SOUNDTYPE:
    {
      soundType = (SoundType)map(potVal[pot], 0, potValueMax+POT_HYSTERESIS, 0, (int)SoundType::LENGTH); 
      switch (soundType)
      {
        case SoundType::SAW:
        {
          aOscil.setTable(SAW_ANALOGUE512_DATA);
          break;
        }
        case SoundType::SQUARE:
        {
          aOscil.setTable(SQUARE_ANALOGUE512_DATA);
          break;
        }
        case SoundType::SINE:
        {
          aOscil.setTable(SIN512_DATA);
          break;
        }
        case SoundType::TRIANGLE:
        {
          aOscil.setTable(TRIANGLE512_DATA);
          break;
        }
      }
      break;
    }

    case KnobFunction::TEMPO:
    {
      tempo = map(potVal[pot], 0, potValueMax+POT_HYSTERESIS, lowBPM, highBPM);
      setNewArpTime();
      break;
    }

    case KnobFunction::RHYTHM:
    {
      if(potVal[pot] < (potValueMax+POT_HYSTERESIS)*1/4)
      {
        noteSpeedMultiplier = 1; // quarter notes
        setNewArpTime();
        break;
      }
      if(potVal[pot] < (potValueMax+POT_HYSTERESIS)*2/4)
      {
        noteSpeedMultiplier = 2; // eight notes
        setNewArpTime();
        break;
      }
      if(potVal[pot] < (potValueMax+POT_HYSTERESIS)*3/4)
      {
        noteSpeedMultiplier = 4; // 1/16th notes
        setNewArpTime();
        break;
      }
      else
      {
        noteSpeedMultiplier = 8; // 1/32th notes
        setNewArpTime();
        break;
      }
      break;
    }
  }
}
int test;
void processPotentiometers()
{
  for(int i = 0; i < N_POTENTIOMETERS; i++)
  {
    if(potMetersAreInverted){
      potValTemp[i] += (potValueMax-mozziAnalogRead(POTENTIOMETER_PINS[i]));
    }
    else{
      potValTemp[i] += mozziAnalogRead(POTENTIOMETER_PINS[i]);
    }
  }

  potCalcCount++;

  if(potCalcCount == POT_CALC_DIVIDER)
  {
    for(int i = 0; i < N_POTENTIOMETERS; i++)
    { 
      prevPotVal[i] = potVal[i];
      int temp = (int)((float)potValTemp[i]/POT_CALC_DIVIDER);
      if(abs(temp - potVal[i]) > POT_HYSTERESIS) // only if the button has moved enough we accept the new value
      {
        potVal[i] = temp;
      }

      if(potVal[i] != prevPotVal[i])
      {
        handlePotValChange(i);
      }
    }
    memset(potValTemp,0,sizeof(potValTemp)); // set all counts back to zero
    potCalcCount = 0;
  }
}

void sendMidiStates()
{
  int value;
  for(int i = 0; i < N_FIDGET_SPINNERS; i++)
  {
    value = map(speed[i], 0, maxSpeed, 0, 127);
    if (ccValuesSpinners[i] != value) 
    {
      sendCC(ccChannelSpinners, i+ccOffset, value);
      ccValuesSpinners[i] = value;
    }
  }

  for(int i = 0; i < N_POTENTIOMETERS; i++)
  {
    value = map(potVal[i], 0, 1023, 0, 127);
    if (ccValuesPotentiometers[i] != value) 
    {
      sendCC(ccChannelPotentiometers, i+ccOffset, value);
      ccValuesPotentiometers[i] = value;
    }
  }
  //Handle USB communication
  USBMIDI.poll();
  // Flush the output.
	USBMIDI.flush();
}
float overdrive;
void controlFX()
{
  if(potVal[6] < potValueMax*1/(int)FxType::LENGTH)
  {
    fxType = FxType::NONE;
    return;
  }
  if(potVal[6] < potValueMax*2/(int)FxType::LENGTH)
  {
    fxType = FxType::FILTER;
    byte cutoff_freq = map(potVal[6], potValueMax*1/(int)FxType::LENGTH, potValueMax*2/(int)FxType::LENGTH, 80, 250);
    resonance = 220; // range 0-255, 255 is most resonant, 255 clips at the given attenuation so 220 was chosen
    rf.setCutoffFreqAndResonance(cutoff_freq, resonance);
    return;
  }
  if(potVal[6] < potValueMax*3/(int)FxType::LENGTH)
  {
    fxType = FxType::TREMOLO;
    vTremolo = kTremolo.next();
    return;
  }
  if(potVal[6] < potValueMax*4/(int)FxType::LENGTH)
  {
    fxType = FxType::WARP;
    return;
  }  
  if(potVal[6] < potValueMax*5/(int)FxType::LENGTH)
  {
    fxType = FxType::GLITCH;
    byte cutoff_freq = map(potVal[6], potValueMax*3/(int)FxType::LENGTH, potValueMax*4/(int)FxType::LENGTH, 200, 300);
    resonance = 255; // go nuts
    rf2.setCutoffFreqAndResonance(cutoff_freq, resonance);
    overdrive = 1.00+(float(potVal[6])-(float)500.0)/200.0;
    return;
  }
}

void updateControl()
{
  controlFX();
  prepareSound(soundType);
  updateAllSpeeds();  
  processPotentiometers();
  sendMidiStates();
}

AudioOutput_t updateAudio()
{

  if(arpType == ArpType::UPWARDS | arpType == ArpType::SPEED_BASED)
  {
    if(soundType==SoundType::SINE | soundType==SoundType::TRIANGLE | soundType==SoundType::SQUARE | soundType==SoundType::SAW)
    {
      envelope.update();
      switch(fxType)
      {
        case FxType::FILTER:
        {
          return MonoOutput::from16Bit((int) (envelope.next() * rf.next(aOscil.next()>>1)));
        }
        case FxType::NONE:
        {
          return MonoOutput::from16Bit((int) (envelope.next() * aOscil.next()));
        }
        case FxType::WARP:
        {
          return MonoOutput::from16Bit((int) (((long)envelope.next() * (long)aOscil.next() * (long)kWarp.next())>>8));
        }
        case FxType::TREMOLO:
        {
          return MonoOutput::from16Bit((int) (((long)envelope.next() * (long)aOscil.next() * vTremolo)>>7));
        }
        case FxType::GLITCH:
        {
          return MonoOutput::fromAlmostNBit(9,((int) (envelope.next() * rf2.next(aOscil.next()>>1)))).clip()>>3;
        }
      }
    }
    switch(soundType)
    {
      case SoundType::BAMBOO:
      {
        switch(fxType)
        {
          case FxType::FILTER:
          {
            int asig = (int)
            ((long) rf.next(aBamboo0.next()>>1)*gains.gain0)>>4;
            return MonoOutput::fromAlmostNBit(9, asig).clip();
          }
          case FxType::NONE:
          {
            int asig = (int)
            ((long) aBamboo0.next()*gains.gain0)>>4;
            return MonoOutput::fromAlmostNBit(9, asig).clip();
          }
          case FxType::TREMOLO:
          {
            int asig = (int)
            ((long) aBamboo0.next()*gains.gain0*vTremolo>>4)>>7;
            return MonoOutput::fromAlmostNBit(9, asig).clip();
          }
          case FxType::WARP:
          {
            int asig = (int)
            ((long) aBamboo0.next()*gains.gain0*kWarp.next())>>8;
            return MonoOutput::fromAlmostNBit(9, asig).clip();
          }
          case FxType::GLITCH:
          {
            int asig = (int)
            ((long) 8.0 * overdrive *aBamboo0.next()*gains.gain0)>>6;
            return MonoOutput::fromAlmostNBit(9, asig).clip()>>1;
          }
        }
      }
    }
  }
  if(arpType == ArpType::MULTI_NOTE)
  {
    long asig = (long)
      aCos0.next()*v[0] +
      aCos1.next()*v[1] +
      aCos2.next()*v[2] +
      aCos3.next()*v[3] +
      aCos4.next()*v[4] +
      aCos5.next()*v[5] +
      aCos6.next()*v[6] +
      aCos7.next()*v[7];

    switch(fxType)
    {
      case FxType::FILTER:
      {
        int asig2 = (int)
        ((long) rf.next(asig>>9));
        return MonoOutput::fromAlmostNBit(9, asig2).clip();
      }
      case FxType::NONE:
      {
        return MonoOutput::fromAlmostNBit(18, asig<<1).clip();
      }
      case FxType::TREMOLO:
      {
        int asig2 = (int)
        ((long) asig*vTremolo>>10)>>6;
        return MonoOutput::fromAlmostNBit(9, asig2).clip();
      }
      case FxType::WARP:
      {
        int asig2 = (int)
        ((long) asig>>12*kWarp.next())>>10;
        return MonoOutput::fromAlmostNBit(9, asig2).clip();
      }
      case FxType::GLITCH:
      {
        return MonoOutput::fromAlmostNBit(9,((int) (rf2.next(asig>>1)))).clip()>>2;
      }
    }
  }
}

void loop(){
  audioHook();
}
