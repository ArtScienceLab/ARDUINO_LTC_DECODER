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

const byte out_pin_30Hz = 11;
const byte out_pin_1Hz = 12;
const byte led_pin = LED_BUILTIN;
byte out_state_30Hz = LOW;
byte out_state_1Hz = LOW;

const int max_diff = 15; //us
const int expected_duration_single = 208;//us
const int expected_duration_double = expected_duration_single*2;

elapsedMicros micros_elapsed_since_transition;
volatile bool first_transition;

volatile bool new_bit_available = false;
volatile int new_bit = 0;


struct smpte_timecode_data{
  int hours;
  int minutes;
  int seconds;
  int frames;
};

void ltc_transition_detected(){
  int micros_since =  micros_elapsed_since_transition;
  if(abs( micros_since - expected_duration_single) < max_diff){
    if(first_transition){
      first_transition = false;
    }else{
      //second transition
      first_transition = true;
      new_bit = 1;
      new_bit_available = true;
    }
  }else if(abs(micros_since - expected_duration_double) < max_diff){
    first_transition = true;
    new_bit = 0;
    new_bit_available = true;
  }else{
    //first bit change? or something is wrong!
  }
  micros_elapsed_since_transition=0;
}

void setup(){
  pinMode(out_pin_1Hz, OUTPUT);
  pinMode(out_pin_30Hz, OUTPUT);
  pinMode(led_pin, OUTPUT);

  Serial.begin(115200);
  
  attachInterrupt(digitalPinToInterrupt(A0), ltc_transition_detected, CHANGE);
}


//the sync word is available in bit 64 - 79, 
//see https://en.wikipedia.org/wiki/Linear_timecode
const uint64_t sync_word = 0b0011111111111101;
const uint64_t sync_mask = 0b1111111111111111;

uint64_t bit_string = 0;

elapsedMicros since_sync_found;
elapsedMicros since_sec_pulse;

smpte_timecode_data current_frame_info;

int bit_index = 0;


// Reverses the bit order of the bit string in v, keeping only the given amount
// of bits
//
// Adapted from the code here: http://graphics.stanford.edu/~seander/bithacks.html#BitReverseObvious
//
// The following is true:
//  uint64_t a = 0b0011001;
//  uint64_t expected_result = 0b1001100;
//  expected_result==reverse_bit_order(a,7);
//
uint64_t reverse_bit_order(uint64_t v,int significant_bits){
  uint64_t r = v; // r will be reversed bits of v; first get LSB of v
  int s = sizeof(v) * 8 - 1; // extra shift needed at end
  for (v >>= 1; v; v >>= 1){   
    r <<= 1;
    r |= v & 1;
    s--;
  }
  r <<= s;
  return r >> (64 - significant_bits);
}

// Decode a part of a bit string in word_value
// The value of bit string starting at start_bit to and incliding stop_bit is returned.
//
// The following is true:
//  uint64_t a = 0b1011001;
//  uint64_t expected_result = 0b1011;
//  expected_result == decode_part(a,3,6);
//
int decode_part(uint64_t bit_string, int start_bit,int stop_bit){
  // This shift puts the start bit on the first place
  uint64_t shifted_bit_string = bit_string >> start_bit;
  
  // Create a bit-mask of the required length
  // including stop bit so add 1
  int bit_string_slice_length = stop_bit - start_bit + 1;
  uint64_t bit_mask = (1<<bit_string_slice_length)-1;
  
  // Apply the mask, effectively ignoring all other bits
  uint64_t masked_bit_string = shifted_bit_string & bit_mask;
  
  return  (int) masked_bit_string;
}

elapsedMicros since_complete;

// This is called every time a frame is complete (when bit 79 has been decoded).
void ltc_frame_complete(){
  
  if(current_frame_info.frames == 29){
    out_state_1Hz = !out_state_1Hz;
    digitalWrite(out_pin_1Hz,out_state_1Hz); 
    Serial.print("1Hz ");
    
  }
  
  out_state_30Hz = !out_state_30Hz;
  digitalWrite(out_pin_30Hz,out_state_30Hz);

  int elapsed_micros = since_complete;
  
  digitalWrite(led_pin,!digitalRead(led_pin));
  
  Serial.printf("%d %02d:%02d:%02d.%02d\n",elapsed_micros, current_frame_info.hours,current_frame_info.minutes,current_frame_info.seconds,current_frame_info.frames);
  since_complete = 0;
}

// This decodes only the needed LTC information and should be very fast:
// Some bit shifts and bit masking are used.
// The slowest operation is reversing a 64 bit string.
void decode_ltc(int new_bit){
  
  // Shift one bit
  bit_string = (bit_string << 1);
  //a bit shift adds a zero, if a 1 is needed add 1
  if(new_bit == 1) bit_string |= 1;

  //if the current_word matches the sync word
  if( (bit_string & sync_mask) == sync_word){
    
    bit_string = 0;
    since_sync_found = 0;
    bit_index = -1;//bit 79

    //a full frame has passed
    ltc_frame_complete();

  } else if(since_sync_found<33333){
    //bit index in LTC message
    bit_index++;

    //info above bit 60 is ignored.
    if(bit_index == 60){
      
      //bit string length = index + 1 = 61
      uint64_t reversed_bit_string = reverse_bit_order(bit_string,61);
      
      //see https://en.wikipedia.org/wiki/Linear_timecode

      //This decodes only time info, user fields are ignored
      current_frame_info.frames  = decode_part(reversed_bit_string,0,3)   + 10 * decode_part(reversed_bit_string,8,9);
      current_frame_info.seconds = decode_part(reversed_bit_string,16,19) + 10 * decode_part(reversed_bit_string,24,26);
      current_frame_info.minutes = decode_part(reversed_bit_string,32,35) + 10 * decode_part(reversed_bit_string,40,42);
      current_frame_info.hours   = decode_part(reversed_bit_string,48,51) + 10 * decode_part(reversed_bit_string,56,57);
      
      bit_string = 0;
    }
  }
}

void loop(){
  // Check is the interrupt found a new bit
  // Decoding is done out of the interrupt
  // to keep it as short as possible
  if(new_bit_available){
    new_bit_available = false;
    decode_ltc(new_bit);
  }
}
