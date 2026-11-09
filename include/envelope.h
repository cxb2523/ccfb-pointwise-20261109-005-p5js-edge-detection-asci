#ifndef OSCVIZ_ENVELOPE_H
#define OSCVIZ_ENVELOPE_H

#include <stddef.h>

/* Fixed framing per spec: frame length 1024 samples, hop 512 samples
 * (50% overlap). Exposed so callers/tests can reason about frame_count
 * without duplicating the formula. */
#define OSCVIZ_FRAME_LEN 1024
#define OSCVIZ_HOP_LEN   512

typedef struct {
    double *values; /* short-time energy per frame, normalized to [0,1] */
    size_t  count;  /* number of frames; 0 for an empty/silent file */
} EnvelopeTrack;

/* Loads a mono PCM16 WAV file and computes its short-time energy envelope:
 * frames of OSCVIZ_FRAME_LEN samples every OSCVIZ_HOP_LEN samples, each
 * Hann-windowed before an RMS estimate, then the whole track is rescaled
 * by its own peak frame so the loudest frame maps to exactly 1.0 (silent
 * files are left at all-zero rather than dividing by zero).
 *
 * Returns 0 on success and fills *out (caller must release it with
 * envelope_free()). Returns -1 if the file cannot be opened, is not a
 * well-formed RIFF/WAVE file, or is not mono 16-bit PCM. */
int envelope_from_wav(const char *path, EnvelopeTrack *out);

void envelope_free(EnvelopeTrack *track);

#endif
