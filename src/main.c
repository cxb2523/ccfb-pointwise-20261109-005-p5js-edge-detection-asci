/* oscviz: terminal ASCII edge-intensity renderer for a mono PCM16 WAV
 * audio envelope. Targets POSIX (Linux/macOS/WSL): uses select()/usleep()
 * for a non-blocking stdin poll so `levels N` can be typed while frames
 * are still being rendered. */
#define _POSIX_C_SOURCE 200809L

#include "envelope.h"
#include "edgemap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
            "usage: %s <file.wav> [--levels N] [--charset S] [--threshold F] [--fps N]\n",
            prog);
}

static int stdin_line_ready(void) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval tv = {0, 0};
    return select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) > 0;
}

/* Processes one line of stdin command input. Unrecognized lines are
 * ignored (they're almost certainly leftover terminal input, not malice,
 * so silently skipping is friendlier than aborting the render loop). */
static void handle_command(char *line, EdgeMap *map) {
    char *nl = strpbrk(line, "\r\n");
    if (nl) *nl = '\0';

    int n;
    if (sscanf(line, "levels %d", &n) == 1) {
        if (edgemap_reconfigure(map, n)) {
            fprintf(stderr, "[oscviz] levels -> %d\n", edgemap_levels(map));
        } else {
            fprintf(stderr,
                    "[oscviz] rejected 'levels %d' (must be %d..%d); keeping %d\n",
                    n, EDGEMAP_MIN_LEVELS, EDGEMAP_MAX_LEVELS, edgemap_levels(map));
        }
    }
}

static void render_frame(const EdgeMap *map, size_t frame_idx, double value) {
    int levels = edgemap_levels(map);
    int level = edgemap_classify(map, value);

    putchar('[');
    for (int i = 0; i < levels; i++) {
        putchar(i <= level ? edgemap_glyph(map, i) : ' ');
    }
    printf("] frame=%zu lvl=%d/%d\n", frame_idx, level, levels);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    const char *wav_path = argv[1];
    int levels = 8;
    double threshold = 0.05;
    const char *charset = " .:-=+*#%@";
    double fps = 30.0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--levels") == 0 && i + 1 < argc) {
            levels = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--charset") == 0 && i + 1 < argc) {
            charset = argv[++i];
        } else if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) {
            threshold = atof(argv[++i]);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = atof(argv[++i]);
        } else {
            fprintf(stderr, "[oscviz] unknown argument '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    EnvelopeTrack track;
    if (envelope_from_wav(wav_path, &track) != 0) {
        fprintf(stderr, "[oscviz] failed to load '%s' (need mono PCM16 WAV)\n", wav_path);
        return 1;
    }

    EdgeMap *map = edgemap_create(levels, threshold, charset);
    if (!map) {
        fprintf(stderr, "[oscviz] invalid --levels/--charset/--threshold\n");
        envelope_free(&track);
        return 1;
    }

    fprintf(stderr,
            "[oscviz] %zu frames, levels=%d, charset_len=%zu. "
            "Type 'levels N' + Enter to hot-swap while running.\n",
            track.count, edgemap_levels(map), strlen(charset));

    double frame_seconds = fps > 0 ? 1.0 / fps : 0.0;
    struct timespec frame_delay = {
        .tv_sec = (time_t)frame_seconds,
        .tv_nsec = (long)((frame_seconds - (time_t)frame_seconds) * 1e9),
    };
    char line[128];

    for (size_t i = 0; i < track.count; i++) {
        render_frame(map, i, track.values[i]);

        while (stdin_line_ready()) {
            if (!fgets(line, sizeof(line), stdin)) break;
            handle_command(line, map);
        }

        if (frame_seconds > 0.0) nanosleep(&frame_delay, NULL);
    }

    edgemap_destroy(map);
    envelope_free(&track);
    return 0;
}
