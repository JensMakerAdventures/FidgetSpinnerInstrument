// GLOBAL INCLUDES
#include <MozziGuts.h>
#include <EventDelay.h>
#include <mozzi_rand.h>
#include <mozzi_midi.h>
#include <Midier.h>

// ADSR synth (type 0)
#include <Oscil.h>
#include <ADSR.h>
#include <tables/sin8192_int8.h>
Oscil <8192, AUDIO_RATE> aOscil(SIN8192_DATA);;
EventDelay noteDelay; // for triggering the envelope
ADSR <AUDIO_RATE, AUDIO_RATE> envelope;
boolean note_is_on = true;
unsigned int duration, attack, decay, sustain, release_ms;

// Bamboo sound sampler (type 1)
#include <Sample.h> // Sample template
#include <samples/bamboo/bamboo_00_2048_int8.h> // wavetable data
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo0(BAMBOO_00_2048_DATA); // use: Sample <table_size, update_rate> SampleName (wavetable)
EventDelay kTriggerDelay; // for scheduling audio gain changes

// HARDWARE CONFIG
const int N_FIDGET_SPINNERS = 8;
const int ENCODER_PINS[N_FIDGET_SPINNERS] = {2,3,4,5,6,7,8,10};
const int N_POTENTIOMETERS = 4;
const int POTENTIOMETER_PINS[N_POTENTIOMETERS] = {A0, A2, A4, A6}; // pay attention here, the order is linked to KnobFunction order!
enum class KnobFunction {PITCH, MODE, ARPTYPE, SOUNDTYPE, LENGTH}; 

// SOFTWARE CONFIG, DON'T TOUCH
const int CONTROL_RATE_HZ = 128; // 128 Hz, so 15.6 ms is needed to get  max fidget spinner speed correctly
const float CONTROL_RATE_MS = 1.0/CONTROL_RATE_HZ*1000.0;
const int SPEED_CALC_DIVIDER = 25;
const int PULSES_PER_REVOLUTION = 3;

// Misc.
enum class SoundType {ADSR, BAMBOO, LENGTH}; 
SoundType soundType; // KNOB
//Arp
bool arpIsOn;
midier::Degree scaleDegree = 1; // counter for the arp
midier::Mode arpMode = midier::Mode::Ionian; // KNOB
midier::Note scaleRoot = midier::Note::G; // KNOB
int prevPotVal[N_POTENTIOMETERS] = {-1, -1, -1, -1}; // Setting all to -1 makes sure in setup we encounter a change and everthing initializes OK.
int potVal[N_POTENTIOMETERS];

void setup(){
  startMozzi(CONTROL_RATE_HZ); // Run in 64 Hz => 15.6 ms cycle time for encoders.
  aBamboo0.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*1.0)); // play at the speed it was recorded at
  for(int i = 0; i < N_FIDGET_SPINNERS; i++)
  {
    pinMode(ENCODER_PINS[i], INPUT);
  }

  adcDisconnectAllDigitalIns(); // Mozzi docs says this helps power consumption and noise
  Serial.begin(9600);
  processPotentiometers();
}

void setsoundType(SoundType mode)
{
  if(mode == SoundType::ADSR) // ADSR synth
  {
    soundType = SoundType::ADSR;
  }
  else if(mode == SoundType::BAMBOO) // Bamboo samples
  {
    soundType = SoundType::BAMBOO;
    kTriggerDelay.set(300); // countdown ms, within resolution of CONTROL_RATE 
  }
}

void enableArpMode()
{
  arpIsOn = true;
  scaleDegree = 1;
}

void disableArpMode()
{
  arpIsOn = false;
}

void nextArpMode()
{
  if ((int)arpMode > (int)midier::Mode::Count)
  {
    arpMode = (midier::Mode)0;
    return;
  }
  arpMode = (midier::Mode)((int)arpMode + 1);
}

void nextRootNote()
{
  if (scaleRoot == midier::Note::G)
  {
    scaleRoot = (midier::Note)0;
  }
  scaleRoot = (midier::Note)((int)scaleRoot + 1);
}

midier::Note nextArpNote()
{
  //Serial.println("nextArpNote() called");
  if (scaleDegree > 8)
  {
    scaleDegree = 1;
  }
  //if(speed[scaleDegree-1] != 0)
  //{
    // determine the interval from the root of the scale to the chord of this scale degree
    const midier::Interval interval = midier::scale::interval(arpMode, scaleDegree);
    
    // calculate the root note of the chord of this scale degree
    const midier::Note chordRoot = scaleRoot + interval;

    
  //}
  scaleDegree++;
  return chordRoot;
}


byte randomGain(){
  return rand(200) + 55;
}

struct gainstruct{
  byte gain0;
  byte gain1;
}
gains;

long speedCalcCount;
long count[N_FIDGET_SPINNERS];
bool prevState[N_FIDGET_SPINNERS];
bool state[N_FIDGET_SPINNERS];
byte speed[N_FIDGET_SPINNERS]; // rotations per second of the spinner

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void updateAllSpeeds()
{
  for(int i = 0; i < N_FIDGET_SPINNERS; i++)
  {
    updateSpeed(i);
  }
  speedCalcCount++;
}

void updateSpeed(int i) // index 0-N_FIDGET_SPINNERS
{
  if ((i<0) | (i > N_FIDGET_SPINNERS - 1)) {return;}

  state[i] = digitalRead(ENCODER_PINS[i]);
  if (state[i] != prevState[i])
  {
    count[i]++;
  }
  prevState[i] = state[i];
  
  // Calculate the speed averating the pulses made over the last cycles
  if(speedCalcCount == SPEED_CALC_DIVIDER)
  {
    speed[i] = byte((float)count[i] / (float)PULSES_PER_REVOLUTION / ((float)SPEED_CALC_DIVIDER*((float)CONTROL_RATE_MS/1000.0)));
    if (speed[i] > 20) {speed[i] = 20;}
    speedCalcCount = 0;
    memset(count,0,sizeof(count));
  }
}

void processSerialInput()
{
  String command;
  if(Serial.available())
  {
    command = Serial.readStringUntil('\n');
    if(command.startsWith("mode")){
      if(command.endsWith("0")){
        setsoundType(SoundType::ADSR);
      }
      if(command.endsWith("1")){
        setsoundType(SoundType::BAMBOO);
      }
    }
    else if(command.startsWith("arp")){
      if(command.endsWith("On")){
        enableArpMode();
      }
      if(command.endsWith("Off")){
        disableArpMode();
      }
    }
    else if(command.equals("nextArpMode")){
      nextArpMode();
    }
    else if(command.equals("nextRootNote")){
      nextRootNote();
    }
    else{
    }
  }
}

void prepareSound(SoundType mode)
{
  switch (mode)
  {
    case SoundType::ADSR:
    {
      if((noteDelay.ready() & (speed[0] != 0)))
      {       
        if (arpIsOn)
        {
          midier::Note note = nextArpNote();
          midier::midi::Number midiNote = midier::midi::number(note, 4);
          aOscil.setFreq((float)mtof((int)midiNote)); // (float) cast IS needed, when using the int setFreq function it rounds a bunch of notes (12, 13, 14) all to playing at 12 somehow
        }
        else
        {
          byte midi_note = 75 + rand(-2, 2) + map(speed[0], 0, 20, -10, 10);
          aOscil.setFreq((int)mtof(midi_note));
        }
        float altSpeed = speed[0] + 0.1;
        if (altSpeed < 1) {altSpeed = 1;}
        attack = 5;
        decay = 200;
        sustain = 1000;
        release_ms = 1000;
        byte attack_level = 255;//rand(128)+127;
        byte decay_level = 255;//rand(255);
        envelope.setADLevels(attack_level,decay_level);
        envelope.setTimes(attack,decay,sustain,release_ms);  
        envelope.noteOn();  
        noteDelay.start(map(speed[0], 0, 20, 350, 30));       
      }
    break;
    }
    
    case SoundType::BAMBOO:
    {
      if(kTriggerDelay.ready() && speed[0]!=0){
        kTriggerDelay.set(map(speed[0], 0, 20, 500, 30));
        midier::midi::Number midiNote;
        if (arpIsOn)
        {
          midier::Note note = nextArpNote();
          midiNote = midier::midi::number(note, -1);
          aBamboo0.setFreq((float)mtof((int)midiNote)); // (float) cast IS needed, when using the int setFreq function it rounds a bunch of notes (12, 13, 14) all to playing at 12 somehow
        }
        else
        {
          float pitchChange = mapFloat((float)speed[0], 0.0, 20.0, 0.8, 1.5);
          aBamboo0.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS)*pitchChange);
        }
        gains.gain0 = randomGain();
        aBamboo0.start();
        kTriggerDelay.start();
        Serial.println("Note started");
        Serial.println( (int) midiNote);
      }
    break;
    }
  }
}

void handlePotValChange(int pot)
{
  switch((KnobFunction)pot)
  {
    case KnobFunction::PITCH:
    {
      scaleRoot = midier::Note::C;
      int semiTonesToAdd = map(potVal[pot], 0, 31, 0, 12);
      scaleRoot = (midier::Note)((int)scaleRoot + semiTonesToAdd);
      break;
    }
    case KnobFunction::MODE:
    {
      arpMode = (midier::Mode) 0;
      int arpModeStepsToAdd = map(potVal[pot], 0, 31, 0, (int)midier::Mode::Count);
      arpMode = (midier::Mode)((int)arpMode + arpModeStepsToAdd);
      break;
    }
    case KnobFunction::ARPTYPE:
    {
      if(potVal[pot] > 15) {arpIsOn = true;}
      else {arpIsOn = false;}
      break;
    }

    case KnobFunction::SOUNDTYPE:
    {
      soundType = (SoundType)map(potVal[pot], 0, 15, 0, (int)SoundType::LENGTH-1);
      break;
    }
  }
}

void processPotentiometers()
{
  for(int i = 0; i < N_POTENTIOMETERS; i++)
  {
    potVal[i] = mozziAnalogRead(POTENTIOMETER_PINS[i])>>5; // trade off precision for noise reduction. Still this gives problems with noise, find better solution!
    if(potVal[i] != prevPotVal[i])
    {
      handlePotValChange(i);
    }
    prevPotVal[i] = potVal[i];
  }
}

void updateControl(){
  //updateAllSpeeds();
  speed[0] = 8;
  soundType = SoundType::BAMBOO;
  arpIsOn = true;
  //processPotentiometers();
  processSerialInput();
  prepareSound(soundType);
}

AudioOutput_t updateAudio(){
  if (soundType==SoundType::ADSR)
  {
    envelope.update();
    return MonoOutput::from16Bit((int) (envelope.next() * aOscil.next()));
  }
  
  else if (soundType == SoundType::BAMBOO)
  {
    int asig = (int)
    ((long) aBamboo0.next()*gains.gain0)>>4;
    return MonoOutput::fromAlmostNBit(9, asig).clip();
  }
}

void loop(){
  audioHook();
}
