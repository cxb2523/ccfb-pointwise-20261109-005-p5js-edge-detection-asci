#include "envelope.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Avoid relying on M_PI (not part of strict C) for the Hann window. */
#define OSCVIZ_PI 3.14159265358979323846

static int read_u32le(FILE *f, unsigned int *out) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *out = (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
           ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
    return 0;
}

static int read_u16le(FILE *f, unsigned short *out) {
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return -1;
    *out = (unsigned short)(b[0] | (b[1] << 8));
    return 0;
}

/* Minimal RIFF/WAVE chunk walk: we only need "fmt " and "data", and we
 * stop as soon as both have been seen so trailing metadata chunks (LIST,
 * id3, ...) never need to be understood. */
static int parse_wav_header(FILE *f, unsigned short *audio_format,
                             unsigned short *num_channels,
                             unsigned short *bits_per_sample,
                             long *data_offset, unsigned int *data_size) {
    char tag[4];
    unsigned int riff_size;
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "RIFF", 4) != 0) return -1;
    if (read_u32le(f, &riff_size) != 0) return -1;
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "WAVE", 4) != 0) return -1;

    int have_fmt = 0;
    *data_offset = -1;

    for (;;) {
        char id[4];
        unsigned int size;
        if (fread(id, 1, 4, f) != 4) break;
        if (read_u32le(f, &size) != 0) break;

        if (memcmp(id, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            if (size < 16 || fread(fmt, 1, 16, f) != 16) return -1;
            *audio_format = (unsigned short)(fmt[0] | (fmt[1] << 8));
            *num_channels = (unsigned short)(fmt[2] | (fmt[3] << 8));
            *bits_per_sample = (unsigned short)(fmt[14] | (fmt[15] << 8));
            have_fmt = 1;
            long extra = (long)size - 16;
            if (extra > 0 && fseek(f, extra, SEEK_CUR) != 0) return -1;
        } else if (memcmp(id, "data", 4) == 0) {
            *data_offset = ftell(f);
            *data_size = size;
            if (fseek(f, (long)size + (long)(size & 1), SEEK_CUR) != 0) return -1;
        } else {
            if (fseek(f, (long)size + (long)(size & 1), SEEK_CUR) != 0) return -1;
        }

        if (have_fmt && *data_offset >= 0) break;
    }

    (void)read_u16le; /* kept for symmetry/future chunk fields, silence unused warning */
    return (have_fmt && *data_offset >= 0) ? 0 : -1;
}

int envelope_from_wav(const char *path, EnvelopeTrack *out) {
    if (!path || !out) return -1;
    out->values = NULL;
    out->count = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    unsigned short audio_format = 0, num_channels = 0, bits_per_sample = 0;
    long data_offset = -1;
    unsigned int data_size = 0;

    if (parse_wav_header(f, &audio_format, &num_channels, &bits_per_sample,
                          &data_offset, &data_size) != 0) {
        fclose(f);
        return -1;
    }

    /* PCM16 mono only, per spec. */
    if (audio_format != 1 || num_channels != 1 || bits_per_sample != 16) {
        fclose(f);
        return -1;
    }

    size_t num_samples = data_size / 2;
    if (num_samples == 0) {
        fclose(f);
        return 0; /* valid empty track */
    }

    unsigned char *raw = malloc(data_size);
    if (!raw) {
        fclose(f);
        return -1;
    }
    if (fseek(f, data_offset, SEEK_SET) != 0 ||
        fread(raw, 1, data_size, f) != data_size) {
        free(raw);
        fclose(f);
        return -1;
    }
    fclose(f);

    short *samples = malloc(num_samples * sizeof(short));
    if (!samples) {
        free(raw);
        return -1;
    }
    for (size_t i = 0; i < num_samples; i++) {
        unsigned char lo = raw[2 * i];
        unsigned char hi = raw[2 * i + 1];
        samples[i] = (short)((unsigned short)lo | ((unsigned short)hi << 8));
    }
    free(raw);

    const size_t frame_len = OSCVIZ_FRAME_LEN;
    const size_t hop = OSCVIZ_HOP_LEN;
    size_t frame_count = (num_samples <= frame_len)
                              ? 1
                              : 1 + (num_samples - frame_len) / hop;

    double *energy = malloc(frame_count * sizeof(double));
    if (!energy) {
        free(samples);
        return -1;
    }

    double peak = 0.0;
    for (size_t fidx = 0; fidx < frame_count; fidx++) {
        size_t start = fidx * hop;
        double sum_sq = 0.0;
        for (size_t n = 0; n < frame_len; n++) {
            size_t sidx = start + n;
            double sample = (sidx < num_samples) ? (double)samples[sidx] / 32768.0 : 0.0;
            /* Hann window: tapers each frame's edges to zero so the RMS
             * estimate isn't dominated by spectral leakage from whatever
             * happens to sit at the frame boundary. */
            double w = 0.5 - 0.5 * cos(2.0 * OSCVIZ_PI * (double)n / (double)(frame_len - 1));
            double wv = sample * w;
            sum_sq += wv * wv;
        }
        double rms = sqrt(sum_sq / (double)frame_len);
        energy[fidx] = rms;
        if (rms > peak) peak = rms;
    }

    if (peak > 0.0) {
        for (size_t fidx = 0; fidx < frame_count; fidx++) {
            energy[fidx] /= peak;
        }
    }

    free(samples);
    out->values = energy;
    out->count = frame_count;
    return 0;
}

void envelope_free(EnvelopeTrack *track) {
    if (!track) return;
    free(track->values);
    track->values = NULL;
    track->count = 0;
}
