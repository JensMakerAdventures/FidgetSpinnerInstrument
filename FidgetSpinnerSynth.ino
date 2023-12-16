#include <MozziGuts.h>
#include <Sample.h> // Sample template
#include <samples/bamboo/bamboo_00_2048_int8.h> // wavetable data
#include <samples/bamboo/bamboo_01_2048_int8.h> // wavetable data
#include <samples/bamboo/bamboo_02_2048_int8.h> // wavetable data
#include <EventDelay.h>
#include <mozzi_rand.h>

// use: Sample <table_size, update_rate> SampleName (wavetable)
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo0(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo1(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo2(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo3(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo4(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo5(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo6(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo7(BAMBOO_00_2048_DATA);
Sample <BAMBOO_00_2048_NUM_CELLS, AUDIO_RATE>aBamboo8(BAMBOO_00_2048_DATA);

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
  aBamboo1.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.95));
  aBamboo2.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.90));
  aBamboo3.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.85)); // play at the speed it was recorded at
  aBamboo4.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.8));
  aBamboo5.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.75));
  aBamboo6.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.70)); // play at the speed it was recorded at
  aBamboo7.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.65));
  aBamboo8.setFreq((float) BAMBOO_00_2048_SAMPLERATE / (float) (BAMBOO_00_2048_NUM_CELLS*0.6));
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
  byte gain2;
  byte gain3;
  byte gain4;
  byte gain5;
  byte gain6;
  byte gain7;
  byte gain8;

}
gains;

long count = 0;
bool prevState = false;
bool state = false;
long speedCalcCount = 0;
float speed = 0;

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
    Serial.println(speed);
  }

  //Serial.println(count);

  // Encoder -> Synth stuff
  
  

  // synth stuff
  if(kTriggerDelay.ready()){
    kTriggerDelay.set(int(map(speed+2, 0, 20, 300, 30)));
    int control = int(map(speed+2, 0, 20, 0, 8));

    switch(control) { // this was switch(rand(0, 8))
    case 0:
      gains.gain0 = 0;//randomGain();
      //aBamboo0.start();
      break;
    case 1:
      gains.gain1 = randomGain();
      aBamboo1.start();
      break;
    case 2:
      gains.gain2 = randomGain();
      aBamboo2.start();
      break;
    case 3:
      gains.gain3 = randomGain();
      aBamboo3.start();
      break;
    case 4:
      gains.gain4 = randomGain();
      aBamboo4.start();
      break;
    case 5:
      gains.gain5 = randomGain();
      aBamboo5.start();
      break;
    case 6:
      gains.gain6 = randomGain();
      aBamboo6.start();
      break;
    case 7:
      gains.gain7 = randomGain();
      aBamboo7.start();
      break;
    case 8:
      gains.gain8 = randomGain();
      aBamboo8.start();
      break;
    }
    kTriggerDelay.start();
  }
}


AudioOutput_t updateAudio(){
  int asig= (int)
    ((long) aBamboo0.next()*gains.gain0 +
      aBamboo1.next()*gains.gain1 +
      aBamboo2.next()*gains.gain2 +
      aBamboo3.next()*gains.gain3 +
      aBamboo4.next()*gains.gain4 +
      aBamboo5.next()*gains.gain5 +
      aBamboo6.next()*gains.gain6 +
      aBamboo7.next()*gains.gain7 +
      aBamboo8.next()*gains.gain8
      )>>4;
  // clip to keep sample loud but still in range
  return MonoOutput::fromAlmostNBit(9, asig)/2;//.clip();
}


void loop(){
  audioHook();
}
