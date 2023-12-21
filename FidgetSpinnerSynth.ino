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

// Arp
midier::Degree scaleDegree = 1; // counter for the arp

// HARDWARE CONFIG
const int N_FIDGET_SPINNERS = 8;
const int ENCODER_PINS[N_FIDGET_SPINNERS] = {2,3,4,5,6,7,8,10};

// SOFTWARE CONFIG, DON'T TOUCH
const int CONTROL_RATE_HZ = 128; // 128 Hz, so 15.6 ms is needed to get  max fidget spinner speed correctly
const float CONTROL_RATE_MS = 1.0/CONTROL_RATE_HZ*1000.0;
const int SPEED_CALC_DIVIDER = 15;
const int PULSES_PER_REVOLUTION = 3;

int soundMode = 0;
bool arpIsOn;
enum SoundMode {ADSR_MODE, BAMBOO_MODE};

void setup(){
  startMozzi(CONTROL_RATE_HZ); // Run in 64 Hz => 15.6 ms cycle time for encoders.
  aBamboo0.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*1.0)); // play at the speed it was recorded at
  for(int i = 0; i < N_FIDGET_SPINNERS; i++)
  {
    pinMode(ENCODER_PINS[i], INPUT);
  }

  Serial.begin(9600);
  setSoundMode(BAMBOO_MODE);
  enableArpMode();
}

void setSoundMode(int mode)
{
  if(mode == ADSR_MODE) // ADSR synth
  {
    soundMode = ADSR_MODE;
  }
  else if(mode == BAMBOO_MODE) // Bamboo samples
  {
    soundMode = BAMBOO_MODE;
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

midier::Note nextArpNote()
{
  if (scaleDegree > 8)
  {
    scaleDegree = 1;
  }
  const midier::Mode mode = midier::Mode::Ionian;
  const midier::Note scaleRoot = midier::Note::G;

  // determine the interval from the root of the scale to the chord of this scale degree
  const midier::Interval interval = midier::scale::interval(mode, scaleDegree);
  

  // calculate the root note of the chord of this scale degree
  const midier::Note chordRoot = scaleRoot + interval;
  //Serial.println((int)chordRoot);
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
byte speed[N_FIDGET_SPINNERS];

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
    Serial.println(command);
    if(command.startsWith("mode")){
      Serial.println("command starts with mode");
      if(command.endsWith("0")){
        Serial.println("command ends with 0");
        setSoundMode(0);
      }
      if(command.endsWith("1")){
        Serial.println("command ends with 1");
        setSoundMode(1);
      }
    }
    else if(command.startsWith("arp")){
      if(command.endsWith("On")){
        Serial.println("Arp enabled");
        enableArpMode();
      }
      if(command.endsWith("Off")){
        Serial.println("Arp disabled");
        disableArpMode();
      }
    }
    else if(command.equals("data")){
      //get_data();
    }
    else if(command.equals("reboot")){
      //reboot();
    }
    else{
      //Serial.println("Invalid command");
    }
  }
}

void prepareSound(int mode)
{
  switch (mode)
  {
    case ADSR_MODE:
    {
      if(noteDelay.ready())
      { 
        byte attack_level = 255;//rand(128)+127;
        byte decay_level = 255;//rand(255);
        envelope.setADLevels(attack_level,decay_level);
        envelope.setTimes(attack,decay,sustain,release_ms);
        if (speed[0] != 0)
        {
          envelope.noteOn();
        }
        
        byte midi_note = 75 + rand(-2, 2) + map(speed[0], 0, 20, -10, 10);
        aOscil.setFreq((int)mtof(midi_note));
        float altSpeed = speed[0] + 0.1;
        if (altSpeed < 1) {altSpeed = 1;}
        attack = 5;
        decay = 200/altSpeed/altSpeed;
        sustain = 1000/altSpeed;
        release_ms = 1000/altSpeed/altSpeed;
        noteDelay.start(map(speed[0]+2, 0, 20, 400, 40));//attack+decay+sustain+release_ms);
      }
    }
    
    case BAMBOO_MODE:
    {
      if(kTriggerDelay.ready()){
        kTriggerDelay.set(map(speed[0]+2, 0, 20, 300, 30));
        float pitchChange = mapFloat(speed[0]+2.0, 0.0, 20.0, 0.8, 1.5);
        if (arpIsOn)
        {
          midier::Note note = nextArpNote();
          midier::midi::Number midiNote = midier::midi::number(note, -1);
          Serial.println(mtof((int)midiNote));
          aBamboo0.setFreq(mtof((int)midiNote)); // gives 12-13-15-16-18-20-23-24, but 12-13-15 all turn out as the same note. Why??
        }
        else
        {
          aBamboo0.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS)*pitchChange);
        }
        

        if(speed[0]==0)
        {
          gains.gain0 = 0;
        }
        else
        {
          gains.gain0 = randomGain();
          aBamboo0.start();
        }
        kTriggerDelay.start();
      }
    }
  }
}

void updateControl(){
  updateAllSpeeds();
  speed[0] = 8;
  processSerialInput();
  prepareSound(soundMode);
}

AudioOutput_t updateAudio(){
  if (soundMode==ADSR_MODE)
  {
    envelope.update();
    return MonoOutput::from16Bit((int) (envelope.next() * aOscil.next()));
  }
  
  else if (soundMode == BAMBOO_MODE)
  {
    int asig = (int)
    ((long) aBamboo0.next()*gains.gain0)>>4;
    return MonoOutput::fromAlmostNBit(9, asig).clip();
  }
}

void loop(){
  audioHook();
}
