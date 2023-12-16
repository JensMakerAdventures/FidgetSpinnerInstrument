#include <MozziGuts.h>
#include <Sample.h> // Sample template
#include <samples/bamboo/bamboo_00_2048_int8.h> // wavetable data
#include <samples/bamboo/bamboo_01_2048_int8.h> // wavetable data
#include <samples/bamboo/bamboo_02_2048_int8.h> // wavetable data
#include <EventDelay.h>
#include <mozzi_rand.h>

#include <Midier.h>

// use: Sample <table_size, update_rate> SampleName (wavetable)
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo0(BAMBOO_00_2048_DATA);

// for scheduling audio gain changes
EventDelay kTriggerDelay;

const int ENCODER_PIN = 2;
const int CONTROL_RATE_HZ = 128; // Hz, so 15.6 ms
const float CONTROL_RATE_MS = 1.0/CONTROL_RATE_HZ*1000.0;
const int SPEED_CALC_MULTIPLIER = 15;
const int PULSES_PER_REVOLUTION = 3;

void setup(){
  startMozzi(CONTROL_RATE_HZ); // Run in 64 Hz => 15.6 ms cycle time for encoders.
  aBamboo0.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*1.0)); // play at the speed it was recorded at
  kTriggerDelay.set(300); // countdown ms, within resolution of CONTROL_RATE
  pinMode(ENCODER_PIN, INPUT);
  Serial.begin(9600);
}


byte randomGain(){
  //return lowByte(xorshift96())<<1;
  return rand(200) + 55;
}

// referencing members from a struct is meant to be a bit faster than seperately
// ....haven't actually tested it here...
struct gainstruct{
  byte gain0;
  byte gain1;
}
gains;

long count = 0;
bool prevState = false;
bool state = false;
long speedCalcCount = 0;
float speed = 0;

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void updateControl(){
  // encoder stuff
  state = digitalRead(ENCODER_PIN);
  if (state != prevState)
  {
    count++;
  }
  prevState = state;
  speedCalcCount++;

  // Calculate the speed averating the pulses made over the last cycles
  if(speedCalcCount == SPEED_CALC_MULTIPLIER)
  {
    speed = (float)count / (float)PULSES_PER_REVOLUTION / ((float)SPEED_CALC_MULTIPLIER*((float)CONTROL_RATE_MS/1000.0));
    if (speed > 20) {speed = 20;}
    speedCalcCount = 0;
    count = 0;
    //Serial.println(speed);
  }



  // synth stuff
  if(kTriggerDelay.ready()){
    kTriggerDelay.set(map(speed+2, 0, 20, 300, 30));
    float pitchChange = mapFloat(speed+2.0, 0.0, 20.0, 0.8, 1.5);
    Serial.println(pitchChange);
    aBamboo0.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS)*pitchChange);

    if(speed==0)
    {
      gains.gain0 = 0;
    }
    else
    {
      //Serial.println("playing note");
      gains.gain0 = randomGain();
      aBamboo0.start();
    }
    kTriggerDelay.start();
  }
}


AudioOutput_t updateAudio(){
  int asig= (int)((long) aBamboo0.next()*gains.gain0)>>4;
  // clip to keep sample loud but still in range
  return MonoOutput::fromAlmostNBit(9, asig)/2;//.clip();
}


void loop(){
  audioHook();
}
