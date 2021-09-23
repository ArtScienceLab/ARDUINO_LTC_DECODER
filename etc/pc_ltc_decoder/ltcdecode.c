#include <stdio.h>
#include <time.h>
#include <math.h>
#include "ltc.h"
#include "short_header.h"
#define BUFFER_SIZE (1)

void decode_header(){
        int audio_frames_per_video_frame = 12000/60;
        ltcsnd_sample_t sound[1];
        LTCDecoder *decoder;
        LTCFrameExt frame;
        decoder = ltc_decoder_create(audio_frames_per_video_frame, 4);
        for(unsigned int i = 0 ; i < short_12_13_14_raw_len ;i++){
                sound[0] = short_12_13_14_raw[i];
               
                ltc_decoder_write(decoder, sound, 1, i);
                while (ltc_decoder_read(decoder, &frame)) {
                        SMPTETimecode stime;
                        ltc_frame_to_time(&stime, &frame.ltc, 1);
                                    
                        printf("%02d:%02d:%02d%c%02d |%8u  %8lld %8lld%s\n",
                                                        stime.hours,
                                                        stime.mins,
                                                        stime.secs,
                                                        (frame.ltc.dfbit) ? '.' : ':',
                                                        stime.frame,
                                                        i,
                                                        frame.off_start,
                                                        frame.off_end,
                                                        frame.reverse ? "  R" : ""
                                                        );
                        if(stime.frame == 29) printf("Start second at %ud \n",i+1);
                        
                }
        }
        ltc_decoder_free(decoder);
}


int main(int argc, char **argv) {

        decode_header();
        
        //for 30fps, sample rate / 30

        int audio_frames_per_video_frame = 12000/60;
        ltcsnd_sample_t sound[BUFFER_SIZE];
        size_t n;
        long int total;
        FILE* f;
        char* filename;
        LTCDecoder *decoder;
        LTCFrameExt frame;
        clock_t start, end;
        double cpu_time_used;


        if (argc > 1) {
                filename = argv[1];
                if (argc > 2) {
                        sscanf(argv[2], "%i", &audio_frames_per_video_frame);
                }
        } else {
                printf("Usage: %s <filename> [audio-frames-per-video-frame]\n", argv[0]);
                return -1;
        }
        f = fopen(filename, "r");
        if (!f) {
                fprintf(stderr, "error opening '%s'\n", filename);
                return -1;
        }
        fprintf(stderr, "* reading from: %s\n", filename);
        total = 0;

        end = clock();
        

        

        decoder = ltc_decoder_create(audio_frames_per_video_frame, 4);
  
        
        start = clock();
        do {
                n = fread(sound, sizeof(ltcsnd_sample_t), BUFFER_SIZE, f);
                ltc_decoder_write(decoder, sound, n, total);

                while (ltc_decoder_read(decoder, &frame)) {
                        SMPTETimecode stime;
                        ltc_frame_to_time(&stime, &frame.ltc, 1);
                    
                        printf("%02d:%02d:%02d%c%02d |%8ld  %8lld %8lld%s\n",
                                        stime.hours,
                                        stime.mins,
                                        stime.secs,
                                        (frame.ltc.dfbit) ? '.' : ':',
                                        stime.frame,
                                        total,
                                        frame.off_start,
                                        frame.off_end,
                                        frame.reverse ? "  R" : ""
                                        );
                        if(stime.frame == 29)
                                printf("Start second at %ld \n",total+1);
                }
                total += n;
        } while (n);
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;

        double micros_per_sample =  cpu_time_used / ((double) total) * 1000000;
        fprintf(stderr,"Processed %ld samples in %.3fs or %.3f micros per sample\n",total,cpu_time_used,micros_per_sample);

        fclose(f);
        ltc_decoder_free(decoder);
        return 0;
}
