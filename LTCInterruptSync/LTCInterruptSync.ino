//
// This program is used to sync with an incoming LTC timecode and send out a 
// pulse every second.
//
// The LTC timecode is expected to have 30 frames/s and use the encodeing as described 
// here: https://en.wikipedia.org/wiki/Linear_timecode
//
// The incoming LTC signal should be biased (zero on 3.3V/2) and 
// amplified in a way that interrupts reliably capture state transitions. See here for a circuit:
// https://electronics.stackexchange.com/questions/445142/how-can-i-vertically-shift-the-voltage-of-a-zero-centered-signal-such-that-i-can
//
// Transitions are expected every 208us
// 
// 1000000 us / (80 bits / frame * 30 frames * 2 transitions per bit) = 208.33 us / transition
//
// A 1 bit is encoded by two transitions 208us apart, a 0 bit by a single 416.66us transition
// 
// The program uses an interrupt to capture state transitions. It simply counts the number of
// microseconds passed sinc the previous interrupt to decode 0 and 1s. 
// The interrupt also sets a boolean to indicate if there is a new value. 
// The loop checks for new values and decodes bit words for the bare minimum needed info.
//
// In the loop an attempt is made to find the LTC "sync word" in bit 64 - 79. Once
// the sync word is found a bit counter is incremented and we then know which bit
// corresponds with which info according the LTC standard
//
// To be able to send a pulse at the first bit of the first frame of every second we need to 
// know when the last bit of the frame before (frame 29) has passed. So we decode only the 
// "sync word", the "frame unit" and "frame tens" for this purpose.
//
// The sync pulse is sent every second when bit zero is decoded from every first frame. 
// The sync pulse is high for one second and then low for a second...

// The output signal with the first of the 120 periods slightly shorter

const byte out_pin = 12;
const byte led_pin = LED_BUILTIN;
byte out_state = LOW;

elapsedMicros micros_elapsed_since_transition;
volatile bool first_transition;

volatile bool new_value_available = false;
volatile int new_value = 0;

const int max_diff = 15; //us
const int expected_duration_single = 208;//us
const int expected_duration_double = expected_duration_single*2;

void pin_change(){
  int micros_since =  micros_elapsed_since_transition;
  if(abs( micros_since - expected_duration_single) < max_diff){
    if(first_transition){
      first_transition = false;
    }else{
      //second transition
      first_transition = true;
      new_value = 1;
      new_value_available = true;
    }
  }else if(abs(micros_since - expected_duration_double) < max_diff){
    first_transition = true;
    new_value = 0;
    new_value_available = true;
  }else{
    //first bit change? or something is wrong!
  }
  micros_elapsed_since_transition=0;
}

void setup(){
  pinMode(out_pin, OUTPUT);
  pinMode(led_pin, OUTPUT);
  
  attachInterrupt(digitalPinToInterrupt(A0), pin_change, CHANGE);
}


//the sync word is available in bit 64 - 79, 
//see https://en.wikipedia.org/wiki/Linear_timecode
int sync_word = 0b0011111111111101;
int sync_mask = 0b1111111111111111;

int frame_unit_mask = 0b1111;

int current_word_value = 0;
elapsedMicros since_sync_found;
elapsedMicros since_sec_pulse;
int current_bit = 0;

int frame_unit = 0;
bool last_frame = false;

// This decodes only the needed LTC information and should be very fast:
// only a couple of comparisons and bit shifts/masks are used.
void decode_ltc(int new_val){
  //shift one bit and mask
  current_word_value = (current_word_value << 1) & sync_mask;
  //a bit shift adds a zero, if a 1 is needed add 1
  if(new_val == 1) current_word_value |= 1;
  
  if(current_word_value == sync_word){
    
    since_sync_found = 0;
    current_bit = -1;//bit 79
    
  } else if(since_sync_found<33333){
  
    //bit index in LTC message
    current_bit++;

    if(current_bit == 0 && last_frame){

      //current bit is zero and we just passed frame 29: 
      //send pulse 
      out_state = !out_state;
      digitalWrite(out_pin, out_state);
      digitalWrite(led_pin, out_state);
      
      last_frame = false;
      
      //TODO: check since_sec_pulse here for values outside of 1000_000 +- 50us
      
      since_sec_pulse=0;
      
    } else if(current_bit == 3){

      //decode frame units
      frame_unit = current_word_value & frame_unit_mask;

      //note that the the bits arrive in reverse order
      if(frame_unit == 0b1001){
        frame_unit = 9;
      }
      
    } else if(current_bit == 9 && new_val == 1 && frame_unit == 9){
      
      //decode frame tens
      //if bit 9 is one and frame unit is 9 we are at frame 29
      last_frame = true;
      
    }    
  }
}

void loop(){  
  if(new_value_available){
    new_value_available = false;
    //copy to prevent overwrite in interrupt just in case
    int new_val = new_value;
    decode_ltc(new_val);
  }
}
