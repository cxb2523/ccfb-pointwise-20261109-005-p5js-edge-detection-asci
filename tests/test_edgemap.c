#include "edgemap.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_failures++;                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

/* edge_levels == EDGEMAP_MIN_LEVELS (2) boundary. */
static void test_levels_boundary_min(void) {
    EdgeMap *map = edgemap_create(2, 0.1, " #");
    CHECK(map != NULL);
    CHECK(edgemap_levels(map) == 2);

    CHECK(edgemap_classify(map, 0.0) == -1);
    CHECK(edgemap_classify(map, 0.05) == -1);
    CHECK(edgemap_classify(map, 0.1) == 0);
    CHECK(edgemap_classify(map, 1.0) == 1); /* top level must be levels-1 */

    for (int i = -5; i <= 20; i++) {
        double x = i / 20.0;
        int lvl = edgemap_classify(map, x);
        CHECK(lvl == -1 || (lvl >= 0 && lvl < 2));
        char g = edgemap_glyph(map, lvl);
        CHECK(g == ' ' || g == '#');
    }

    edgemap_destroy(map);
}

/* edge_levels == EDGEMAP_MAX_LEVELS (16) boundary. */
static void test_levels_boundary_max(void) {
    const char *charset = " .:-=+*#%@";
    EdgeMap *map = edgemap_create(16, 0.0, charset);
    CHECK(map != NULL);
    CHECK(edgemap_levels(map) == 16);

    CHECK(edgemap_classify(map, 0.0) == 0); /* threshold==0, nothing clears */
    CHECK(edgemap_classify(map, 1.0) == 15);

    int prev = -1;
    for (int i = 0; i <= 1000; i++) {
        double x = i / 1000.0;
        int lvl = edgemap_classify(map, x);
        CHECK(lvl >= 0 && lvl < 16);
        CHECK(lvl >= prev); /* classify() must be monotonic non-decreasing */
        prev = lvl;
        char g = edgemap_glyph(map, lvl);
        CHECK(strchr(charset, g) != NULL);
    }

    edgemap_destroy(map);
}

/* Transition width must be an edge_levels-dependent expression that
 * strictly shrinks as edge_levels grows, never a flat constant. */
static void test_transition_width_shrinks(void) {
    double prev_w = 1e9;
    for (int levels = EDGEMAP_MIN_LEVELS; levels <= EDGEMAP_MAX_LEVELS; levels++) {
        double w = edgemap_transition_width(levels);
        CHECK(w > 0.0);
        CHECK(w <= 0.5 / (double)levels + 1e-12); /* never overlaps a neighbour cell */
        CHECK(w < prev_w + 1e-12);                /* strictly shrinking */
        prev_w = w;
    }
}

/* The boundary blend must come from cubic smoothstep, not a floor()
 * hard cut: smoothstep has zero slope at both ends and sits below the
 * linear ramp near the low edge (3t^2-2t^3 < t for 0<t<1). */
static void test_smoothstep_not_hard_cut(void) {
    CHECK(fabs(edgemap_smoothstep(0.0, 1.0, 0.0) - 0.0) < 1e-12);
    CHECK(fabs(edgemap_smoothstep(0.0, 1.0, 1.0) - 1.0) < 1e-12);
    CHECK(fabs(edgemap_smoothstep(0.0, 1.0, 0.5) - 0.5) < 1e-12);
    CHECK(edgemap_smoothstep(0.0, 1.0, 0.25) < 0.25); /* flatter than a linear ramp near the edge */
    CHECK(edgemap_smoothstep(0.0, 1.0, 0.75) > 0.75);

    /* Inside a cell's blend zone, continuous classification must move
     * smoothly (not jump by a whole level) as intensity creeps toward
     * the boundary -- the opposite of what floor() would do. */
    EdgeMap *map = edgemap_create(4, 0.0, " .:#");
    CHECK(map != NULL);
    double w = edgemap_transition_width(4);
    double cell = 0.25;
    double boundary = cell; /* edge between level 0 and level 1 */
    double a = edgemap_classify_continuous(map, boundary - w * cell * 0.5);
    double b = edgemap_classify_continuous(map, boundary - w * cell * 0.1);
    CHECK(a >= 0.0 && b >= 0.0);
    CHECK(b > a);        /* strictly increasing inside the blend zone */
    CHECK(b - a < 0.5);  /* small input step -> small output step, no jump */
    edgemap_destroy(map);
}

/* gcd(9,4) == 1: charset length and edge_levels coprime. */
static void test_charset_coprime(void) {
    const char *charset = "ABCD";
    EdgeMap *map = edgemap_create(9, 0.0, charset);
    CHECK(map != NULL);

    int seen_first = 0, seen_last = 0;
    int prev_idx = -1;
    for (int lvl = 0; lvl < 9; lvl++) {
        char g = edgemap_glyph(map, lvl);
        const char *p = strchr(charset, g);
        CHECK(p != NULL);
        int idx = (int)(p - charset);
        CHECK(idx >= prev_idx); /* monotonic: a higher level never regresses to a lighter glyph */
        prev_idx = idx;
        if (g == charset[0]) seen_first = 1;
        if (g == charset[strlen(charset) - 1]) seen_last = 1;
    }
    CHECK(seen_first && seen_last); /* full glyph range reachable */

    edgemap_destroy(map);
}

/* gcd(8,10) == 2: charset length and edge_levels NOT coprime, and
 * edge_levels < charset_len -- exactly the case where naive
 * `level % charset_len` would leave glyph indices 8 and 9 ('%' and '@',
 * the most intense glyphs) permanently unreachable. */
static void test_charset_noncoprime(void) {
    const char *charset = " .:-=+*#%@";
    size_t n = strlen(charset);
    EdgeMap *map = edgemap_create(8, 0.0, charset);
    CHECK(map != NULL);

    CHECK(edgemap_glyph(map, 0) == charset[0]);
    CHECK(edgemap_glyph(map, 7) == charset[n - 1]); /* '@' must be reachable */

    int prev_idx = -1;
    for (int lvl = 0; lvl < 8; lvl++) {
        char g = edgemap_glyph(map, lvl);
        const char *p = strchr(charset, g);
        CHECK(p != NULL);
        int idx = (int)(p - charset);
        CHECK(idx >= prev_idx);
        prev_idx = idx;
    }

    edgemap_destroy(map);
}

/* One runtime hot-swap (levels 8 -> 3), then verify every level the
 * reconfigured state machine can produce is in-bounds for the rebuilt
 * glyph table, and that rejected reconfigures leave the live state
 * completely untouched. */
static void test_hot_swap_bounds(void) {
    const char *charset = " .:-=+*#%@";
    EdgeMap *map = edgemap_create(8, 0.05, charset);
    CHECK(map != NULL);

    CHECK(edgemap_classify(map, 1.0) == 7);
    CHECK(strchr(charset, edgemap_glyph(map, 7)) != NULL);

    CHECK(edgemap_reconfigure(map, 3) == 1);
    CHECK(edgemap_levels(map) == 3);

    for (int i = -20; i <= 20; i++) {
        double x = i / 20.0;
        int lvl = edgemap_classify(map, x);
        CHECK(lvl == -1 || (lvl >= 0 && lvl < 3)); /* bounds check right after hot swap */
        char g = edgemap_glyph(map, lvl);
        CHECK(strchr(charset, g) != NULL);
    }
    CHECK(edgemap_classify(map, 1.0) == 2); /* new top level, never stale old-top 7 */

    /* Out-of-range reconfigure requests must be rejected, leaving the
     * current 3-level state fully intact (no partial swap). */
    CHECK(edgemap_reconfigure(map, 1) == 0);
    CHECK(edgemap_reconfigure(map, EDGEMAP_MAX_LEVELS + 1) == 0);
    CHECK(edgemap_levels(map) == 3);
    CHECK(edgemap_classify(map, 1.0) == 2);

    edgemap_destroy(map);
}

static void test_create_rejects_bad_args(void) {
    CHECK(edgemap_create(EDGEMAP_MIN_LEVELS - 1, 0.1, " #") == NULL);
    CHECK(edgemap_create(EDGEMAP_MAX_LEVELS + 1, 0.1, " #") == NULL);
    CHECK(edgemap_create(8, 0.1, "") == NULL);
    CHECK(edgemap_create(8, 0.1, NULL) == NULL);
    CHECK(edgemap_create(8, -0.1, " #") == NULL);
    CHECK(edgemap_create(8, 1.1, " #") == NULL);
}

int main(void) {
    test_levels_boundary_min();
    test_levels_boundary_max();
    test_transition_width_shrinks();
    test_smoothstep_not_hard_cut();
    test_charset_coprime();
    test_charset_noncoprime();
    test_hot_swap_bounds();
    test_create_rejects_bad_args();

    fprintf(stderr, "%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
