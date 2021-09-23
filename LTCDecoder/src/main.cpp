//This is a patch to decode LTC linear time code in an SMPTE audio stream.
//it uses a Teensy Audio Shield for line level audio input
//and libltc https://github.com/x42/libltc by Robin Gareus and others for the decoding itself
// The libltc code is licenced under the GPL

//Patch by Joren Six http://0110.be for IPEM, Ghent University

#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include "ltc.h"

//The teensy audio library: https://www.pjrc.com/teensy/td_libs_Audio.html
//is used for audio processing.
//The line input is used for audio input (line level input)
AudioInputI2S            i2s_input;
//To monitor the input it is connected to the line output (headphones jack)
AudioOutputI2S           i2s_output;
//To decode the time code encoded audio a queue is used
// a queue contains blocks of 128 audio samples
AudioRecordQueue         audio_queue;

#define BUFFER_SIZE (1024)

const int signal_output_pin = 12;

const int audio_queue_size = 128;

//To check the audio input, connect input to output
//The input is mono so connect the left input channel to both output channels
AudioConnection          patchCord1(i2s_input, 0, i2s_output, 0);
AudioConnection          patchCord2(i2s_input, 0, i2s_output, 1);

//Connect the queue to the input
AudioConnection          patchCord3(i2s_input, 0, audio_queue, 0);

//To control the volume
AudioControlSGTL5000 audioShield;

//The ltc sound buffer (at 8 bits)
ltcsnd_sample_t sound[BUFFER_SIZE];

// The number of audio frames per video frame at 30fps is 44100/30
int audio_frames_per_video_frames = AUDIO_SAMPLE_RATE_EXACT / 30;

//64 bits audio sample counter
//should overflow every (2^63-1) / (44100 Hz)  = 6 255 204.4 years
long long int audio_sample_counter = 0;

long long int interrupt_audio_sample_counter = 0;

//The LTC decoder
LTCDecoder *decoder;

//the current decoded LTC frame
LTCFrameExt frame;

void setup() {
  Serial.begin(115200);

  //Give the audio library some memory
  AudioMemory(12);

  //Enable, choose input and set volume
  audioShield.enable();
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);
  audioShield.volume(1.0);

  //initialize the decoder
  // use a queue size of 32
  decoder = ltc_decoder_create(audio_frames_per_video_frames, 8);

  //Start with audio flow
  audio_queue.begin();
}

void decode_running_ltc(){

  //Decode!
  ltc_decoder_write(decoder, sound, BUFFER_SIZE, audio_sample_counter);

  //while there is a frame to decode...
  while (ltc_decoder_read(decoder, &frame)) {

    // For some reason the ltc information ends up in the wrong place
    // Here we get time information from user bits
    // Currently uns
    int hours =  frame.ltc.user7 + frame.ltc.user8 * 10;
    int mins =   frame.ltc.user5 + frame.ltc.user6 * 10;
    int secs =   frame.ltc.user3 + frame.ltc.user4 * 10;
    int frames = frame.ltc.user1 + frame.ltc.user2 * 10;


    long long frame_delta = audio_sample_counter - frame.off_start ;

    Serial.printf("%02d:%02d:%02d:%02d | %8lld %8lld %8lld\n",
                    hours,
                    mins,
                    secs,
                    frames,
                    frame.off_start,
                    frame.off_end,
                    frame_delta
                  );

  }
}

int ltc_buffer_index = 0;

void loop() {

  //If there are 128 audio samples ready for processing
  if (audio_queue.available()) {

    // Fetch a block from the audio library and copy
    // 128 bytes. The audio Library
    // audio samples are 16 bits, the ltc decoder only needs 8 bits
    // so the samples are converted (bit shifted).
    int16_t* queue_buffer = audio_queue.readBuffer();
    //a buffer has a length of 128 sound samples at about 44.1kHz

    for(int j=0 ; j < audio_queue_size ; j++){
      //converts 16bit samples to 8 bits (linear encoding)
      byte sound_sample = (byte) (((int) queue_buffer[j] >> 8) & 0xff);
      sound[ltc_buffer_index] = sound_sample;
      // do not forget to increment the ltc_buffer_index
      ltc_buffer_index++;
    }
    //free the sound buffer for reuse
    audio_queue.freeBuffer();

    //increment the audio sample counter by 128
    audio_sample_counter += audio_queue_size;
  }

  //We have a full buffer of 1024 samples
  if (ltc_buffer_index == BUFFER_SIZE) {
    ltc_buffer_index=0;


    // Fetch 8 blocks from the audio library and copy
    // into a 1024 byte buffer. The audio Library
    // audio samples are 16 bits, the ltc decoder only needs 8 bits
    // so the samples are converted (bit shifted)

    //assert ltc_buffer_index == BUFFER_SIZE;

    decode_running_ltc();
  }

}
