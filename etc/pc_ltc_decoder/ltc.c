/*
   libltc - en+decode linear timecode

   Copyright (C) 2006-2015 Robin Gareus <robin@gareus.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library.
   If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ltc.h"
#include "decoder.h"


#if (defined _MSC_VER && _MSC_VER < 1800) || (defined __AVR__)
static double rint(double v) {
	// NB. this is identical to round(), not rint(), but the difference is not relevant here
	return floor(v + 0.5);
}
#endif

/* -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * Decoder
 */

LTCDecoder* ltc_decoder_create(int apv, int queue_len) {
	LTCDecoder* d = (LTCDecoder*) calloc(1, sizeof(LTCDecoder));
	if (!d) return NULL;

	d->queue_len = queue_len;
	d->queue = (LTCFrameExt*) calloc(d->queue_len, sizeof(LTCFrameExt));
	if (!d->queue) {
		free(d);
		return NULL;
	}
	d->biphase_state = 1;
	d->snd_to_biphase_period = apv / 80;
	d->snd_to_biphase_lmt = (d->snd_to_biphase_period * 3) / 4;

	d->snd_to_biphase_min = SAMPLE_CENTER;
	d->snd_to_biphase_max = SAMPLE_CENTER;
	d->frame_start_prev = -1;
	d->biphase_tic = 0;

	return d;
}

int ltc_decoder_free(LTCDecoder *d) {
	if (!d) return 1;
	if (d->queue) free(d->queue);
	free(d);

	return 0;
}

void ltc_decoder_write(LTCDecoder *d, ltcsnd_sample_t *buf, size_t size, ltc_off_t posinfo) {
	decode_ltc(d, buf, size, posinfo);
}

#define LTC_CONVERSION_BUF_SIZE 1024

#define LTCWRITE_TEMPLATE(FN, FORMAT, CONV) \
void ltc_decoder_write_ ## FN (LTCDecoder *d, FORMAT *buf, size_t size, ltc_off_t posinfo) { \
	ltcsnd_sample_t tmp[LTC_CONVERSION_BUF_SIZE]; \
	size_t copyStart = 0; \
	while (copyStart < size) { \
		int i; \
		int c = size - copyStart; \
		c = (c > LTC_CONVERSION_BUF_SIZE) ? LTC_CONVERSION_BUF_SIZE : c; \
		for (i=0; i < c; i++) { \
			tmp[i] = CONV; \
		} \
		decode_ltc(d, tmp, c, posinfo + (ltc_off_t)copyStart); \
		copyStart += c; \
	} \
}

LTCWRITE_TEMPLATE(double, double, 128 + (buf[copyStart+i] * 127.0))
LTCWRITE_TEMPLATE(float, float, 128 + (buf[copyStart+i] * 127.f))
/* this relies on the compiler to use an arithmetic right-shift for signed values */
LTCWRITE_TEMPLATE(s16, short, 128 + (buf[copyStart+i] >> 8))
/* this relies on the compiler to use a logical right-shift for unsigned values */
LTCWRITE_TEMPLATE(u16, unsigned short, (buf[copyStart+i] >> 8))

#undef LTC_CONVERSION_BUF_SIZE

int ltc_decoder_read(LTCDecoder* d, LTCFrameExt* frame) {
	if (!frame) return -1;
	if (d->queue_read_off != d->queue_write_off) {
		memcpy(frame, &d->queue[d->queue_read_off], sizeof(LTCFrameExt));
		d->queue_read_off++;
		if (d->queue_read_off == d->queue_len)
			d->queue_read_off = 0;
		return 1;
	}
	return 0;
}

void ltc_decoder_queue_flush(LTCDecoder* d) {
	while (d->queue_read_off != d->queue_write_off) {
		d->queue_read_off++;
		if (d->queue_read_off == d->queue_len)
			d->queue_read_off = 0;
	}
}

int ltc_decoder_queue_length(LTCDecoder* d) {
	return (d->queue_write_off - d->queue_read_off + d->queue_len) % d->queue_len;
}



unsigned long ltc_frame_get_user_bits(LTCFrame *f){
	unsigned long data = 0;
	data += f->user8;
	data <<= 4;
	data += f->user7;
	data <<= 4;
	data += f->user6;
	data <<= 4;
	data += f->user5;
	data <<= 4;
	data += f->user4;
	data <<= 4;
	data += f->user3;
	data <<= 4;
	data += f->user2;
	data <<= 4;
	data += f->user1;
	return data;
}




void ltc_frame_set_parity(LTCFrame *frame, enum LTC_TV_STANDARD standard) {
	int i;
	unsigned char p = 0;

	if (standard != LTC_TV_625_50) { /* 30fps, 24fps */
		frame->biphase_mark_phase_correction = 0;
	} else { /* 25fps */
		frame->binary_group_flag_bit2 = 0;
	}

	for (i=0; i < LTC_FRAME_BIT_COUNT / 8; ++i){
		p = p ^ (((unsigned char*)frame)[i]);
	}
#define PRY(BIT) ((p>>BIT)&1)

	if (standard != LTC_TV_625_50) { /* 30fps, 24fps */
		frame->biphase_mark_phase_correction =
			PRY(0)^PRY(1)^PRY(2)^PRY(3)^PRY(4)^PRY(5)^PRY(6)^PRY(7);
	} else { /* 25fps */
		frame->binary_group_flag_bit2 =
			PRY(0)^PRY(1)^PRY(2)^PRY(3)^PRY(4)^PRY(5)^PRY(6)^PRY(7);
	}
}

ltc_off_t ltc_frame_alignment(double samples_per_frame, enum LTC_TV_STANDARD standard) {
	switch (standard) {
		case LTC_TV_525_60:
			return rint(samples_per_frame * 4.0 / 525.0);
		case LTC_TV_625_50:
			return rint(samples_per_frame * 1.0 / 625.0);
		default:
			return 0;
	}
}
