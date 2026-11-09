/* Dev utility (not part of `make`/`make test`): synthesizes sample.wav, a
 * mono PCM16 WAV with a slowly swelling-and-fading sine tone, so the demo
 * command in README/OSCVIZ.md has something to render.
 *
 * Build/run manually:
 *   cc -std=gnu11 -O2 -o tools/gen_sample tools/gen_sample.c -lm
 *   ./tools/gen_sample sample.wav
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define OSCVIZ_PI 3.14159265358979323846

static void write_u32le(FILE *f, unsigned int v) {
    unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8),
                           (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void write_u16le(FILE *f, unsigned short v) {
    unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
    fwrite(b, 1, 2, f);
}

int main(int argc, char **argv) {
    const char *out_path = argc > 1 ? argv[1] : "sample.wav";
    const unsigned int sample_rate = 44100;
    const double duration_s = 4.0;
    const unsigned int num_samples = (unsigned int)(sample_rate * duration_s);
    const unsigned int data_size = num_samples * 2;

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "cannot open %s for writing\n", out_path);
        return 1;
    }

    fwrite("RIFF", 1, 4, f);
    write_u32le(f, 36 + data_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32le(f, 16);
    write_u16le(f, 1);           /* PCM */
    write_u16le(f, 1);           /* mono */
    write_u32le(f, sample_rate);
    write_u32le(f, sample_rate * 2); /* byte rate */
    write_u16le(f, 2);           /* block align */
    write_u16le(f, 16);          /* bits per sample */

    fwrite("data", 1, 4, f);
    write_u32le(f, data_size);

    for (unsigned int i = 0; i < num_samples; i++) {
        double t = (double)i / (double)sample_rate;
        double envelope = 0.5 * (1.0 - cos(2.0 * OSCVIZ_PI * t / duration_s)); /* 0 -> 1 -> 0 */
        double tone = sin(2.0 * OSCVIZ_PI * 220.0 * t);
        double sample = envelope * tone * 0.9;
        short s16 = (short)(sample * 32767.0);
        write_u16le(f, (unsigned short)s16);
    }

    fclose(f);
    printf("wrote %s (%u samples, %.1fs)\n", out_path, num_samples, duration_s);
    return 0;
}
