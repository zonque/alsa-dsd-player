/*
 * Copyright (c) 2013 Daniel Mack
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

/*
 * See README
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

#if 1
/* 8-bit DSD */
#define ALSA_FORMAT SND_PCM_FORMAT_DSD_U8
#define SAMPLE_SIZE (sizeof(uint8_t) * 2)
#define SAMPLE_RATE_DIV 1
#else
/* 16-bit DSD */
#define ALSA_FORMAT SND_PCM_FORMAT_DSD_U16
#define SAMPLE_SIZE (sizeof(uint16_t) * 2)
#define SAMPLE_RATE_DIV 2
#endif

#define CHANNELCOUNT 2
#define FRAMECOUNT (1024 * 128)
#define BUFSIZE (FRAMECOUNT * SAMPLE_SIZE * CHANNELCOUNT)

static int open_stream(snd_pcm_t **handle, const char *name, int dir,
		       unsigned int rate)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	unsigned int format = ALSA_FORMAT;
	unsigned int period_time = 1000000;
	const char *dirname = (dir == SND_PCM_STREAM_PLAYBACK) ? "PLAYBACK" : "CAPTURE";
	int err;

	if ((err = snd_pcm_open(handle, name, dir, 0)) < 0) {
		fprintf(stderr, "%s (%s): cannot open audio device (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot allocate hardware parameter structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_any(*handle, hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot initialize hardware parameter structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_access(*handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		fprintf(stderr, "%s (%s): cannot set access type(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_format(*handle, hw_params, format)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample format(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_rate_near(*handle, hw_params, &rate, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample rate(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_period_time_near(*handle, hw_params, &period_time, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set period time(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_channels(*handle, hw_params, 2)) < 0) {
		fprintf(stderr, "%s (%s): cannot set channel count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params(*handle, hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot set parameters(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	snd_pcm_hw_params_free(hw_params);

	if ((err = snd_pcm_sw_params_malloc(&sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot allocate software parameters structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_current(*handle, sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot initialize software parameters structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, FRAMECOUNT / 2)) < 0) {
		fprintf(stderr, "%s (%s): cannot set minimum available count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_set_start_threshold(*handle, sw_params, 0U)) < 0) {
		fprintf(stderr, "%s (%s): cannot set start mode(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params(*handle, sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot set software parameters(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	return 0;
}

/* HACK: fast forward to the first data chunk */
static int dff_fast_forward(int fd)
{
	uint32_t chunk_header = 0;
	uint64_t dummy;
	uint8_t b;
	int ret;

	while (1) {
		ret = read(fd, &b, sizeof(b));
		if (ret != sizeof(b))
			break;

		chunk_header <<= 8;
		chunk_header |= b;

		if (chunk_header == 0x44534420) { /* 'DSD ' */
			ret = read(fd, &dummy, sizeof(dummy));
			break;
		}
	}

	return ret;
}

#if 0
static uint8_t bitrev (uint8_t x)
{
	x = ((x & 0xf0) >> 4) | ((x & 0x0f) << 4);
	x = ((x & 0xcc) >> 2) | ((x & 0x33) << 2);
	x = ((x & 0xaa) >> 1) | ((x & 0x55) << 1);

	return x;
}
#endif

int main(int argc, char *argv[])
{
	int fd, err;
	unsigned int len;
	snd_pcm_t *playback_handle;
	char *readbuf, *playbuf, *name;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
		return EXIT_FAILURE;
	}

	name = argv[1];
	fd = open(name, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open file (%m)\n");
		return fd;
	}

	len = strlen(name);
	if (len > 4 && strcmp(name + len - 4, ".dff") == 0) {
		err = dff_fast_forward(fd);
		if (err < 0)
			return err;
	}

	if ((err = open_stream(&playback_handle, "hw:MPD3",
				SND_PCM_STREAM_PLAYBACK,
				352800 / SAMPLE_RATE_DIV)) < 0)
		return err;

	if ((err = snd_pcm_prepare(playback_handle)) < 0) {
		fprintf(stderr, "cannot prepare audio interface for use(%s)\n",
			 snd_strerror(err));
		return err;
	}

	playbuf = malloc(BUFSIZE);
	if (!playbuf) {
		fprintf(stderr, "Unable to malloc() %d bytes\n", BUFSIZE);
		return -1;
	}

	readbuf = malloc(BUFSIZE);
	if (!readbuf) {
		fprintf(stderr, "Unable to malloc() %d bytes\n", BUFSIZE);
		return -1;
	}

	memset(playbuf, 0, BUFSIZE);

	while (1) {
		int r, frames;
		unsigned int i;

		if ((err = snd_pcm_wait(playback_handle, 1000)) < 0) {
			fprintf(stderr, "poll failed(%s)\n", strerror(errno));
			break;
		}

		frames = snd_pcm_avail_update(playback_handle);
		if (frames == 0)
			break;

		if (frames < 0)
			break;

		if (frames > FRAMECOUNT)
			frames = FRAMECOUNT;

		r = read(fd, readbuf, frames * SAMPLE_SIZE * CHANNELCOUNT);
		if (r != (int) (frames * SAMPLE_SIZE * CHANNELCOUNT))
			break;

		if (SAMPLE_SIZE > 2) {
			/* layout in file: 	L0 R0 L1 R1 L2 R2 ... */
			/* layout in playbuf:	L0 L1 R0 R1 L2 L3 ... */
			for (i = 0; i < frames * SAMPLE_SIZE * CHANNELCOUNT; i += 4) {
				playbuf[i + 0] = readbuf[i + 2];
				playbuf[i + 1] = readbuf[i + 0];
				playbuf[i + 2] = readbuf[i + 3];
				playbuf[i + 3] = readbuf[i + 1];
			}
		} else {
			memcpy(playbuf, readbuf, frames * SAMPLE_SIZE * CHANNELCOUNT);
		}

#if 0
		for (i = 0; i < frames * SAMPLE_SIZE * CHANNELCOUNT; i++)
			playbuf[i] = bitrev(playbuf[i]);
#endif

		snd_pcm_writei(playback_handle, playbuf, frames * CHANNELCOUNT);
	}

	snd_pcm_close(playback_handle);
	close(fd);

	return 0;
}
