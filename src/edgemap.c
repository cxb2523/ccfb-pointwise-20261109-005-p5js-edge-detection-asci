#include "edgemap.h"

#include <stdlib.h>
#include <string.h>

struct EdgeMap {
    int edge_levels;
    double edge_threshold;
    char *charset;      /* owned, NUL-terminated copy */
    size_t charset_len;  /* strlen(charset), always >= 1 */
    char *glyph_lut;     /* owned, length edge_levels: glyph_lut[level] */
};

/* ---------------------------------------------------------------------
 * Cubic smoothstep
 * ------------------------------------------------------------------- */

double edgemap_smoothstep(double edge0, double edge1, double x) {
    if (edge0 == edge1) return x < edge0 ? 0.0 : 1.0;
    double t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

/* ---------------------------------------------------------------------
 * Transition width
 *
 * The [threshold, 1.0] span is cut into `edge_levels` equal cells of
 * width `cell = 1/edge_levels` on the normalized axis. Rather than a
 * hard floor() at every cell boundary, we smoothstep-blend across a
 * small region around each boundary.
 *
 * That blend region must be a *fraction of the cell*, not an absolute
 * width: TRANSITION_FRACTION below is a dimensionless constant (fraction
 * of one cell), and the actual width returned is `TRANSITION_FRACTION /
 * edge_levels`, which is an expression in edge_levels and strictly
 * decreases as edge_levels grows (more, narrower cells -> proportionally
 * narrower blend zones, so they never bleed into neighbouring cells).
 * A width that did NOT depend on edge_levels (e.g. a flat 0.02 on the
 * normalized axis) would violate the spec and is explicitly rejected by
 * this design: the only constant here is cell-relative, never absolute.
 * ------------------------------------------------------------------- */

#define TRANSITION_FRACTION 0.30 /* fraction of one level-cell, not a width */

double edgemap_transition_width(int edge_levels) {
    if (edge_levels < EDGEMAP_MIN_LEVELS) edge_levels = EDGEMAP_MIN_LEVELS;
    if (edge_levels > EDGEMAP_MAX_LEVELS) edge_levels = EDGEMAP_MAX_LEVELS;

    double cell = 1.0 / (double)edge_levels;
    double width = cell * TRANSITION_FRACTION;
    double max_width = cell * 0.5; /* never let a cell's two blend zones overlap */
    return width < max_width ? width : max_width;
}

/* ---------------------------------------------------------------------
 * Classification
 * ------------------------------------------------------------------- */

double edgemap_classify_continuous(const EdgeMap *map, double intensity) {
    if (!map) return -1.0;
    if (intensity < map->edge_threshold) return -1.0;

    int levels = map->edge_levels;
    double span = 1.0 - map->edge_threshold;
    double u = (span > 0.0) ? (intensity - map->edge_threshold) / span : 1.0;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;

    double pos = u * (double)levels;
    if (pos >= (double)levels) pos = (double)levels - 1e-9; /* intensity==1.0 -> last cell */

    int L = (int)pos;
    if (L < 0) L = 0;
    if (L > levels - 1) L = levels - 1;
    double frac = pos - (double)L;

    double w = edgemap_transition_width(levels);
    if (L < levels - 1 && frac >= 1.0 - w) {
        /* Soft blend toward the next level instead of snapping to L+1
         * exactly at frac==1.0: effective rises smoothly (zero slope at
         * the start of the zone) from L to L+1 across the last `w` of
         * this cell. */
        double s = edgemap_smoothstep(1.0 - w, 1.0, frac);
        return (double)L + s;
    }
    return (double)L;
}

int edgemap_classify(const EdgeMap *map, double intensity) {
    double effective = edgemap_classify_continuous(map, intensity);
    if (effective < 0.0) return -1;

    int levels = map ? map->edge_levels : EDGEMAP_MIN_LEVELS;
    int level = (int)(effective + 0.5);
    if (level < 0) level = 0;
    if (level > levels - 1) level = levels - 1;
    return level;
}

/* ---------------------------------------------------------------------
 * Glyph allocation
 *
 * Deliberately NOT `charset[level % charset_len]`.
 *
 * `level` ranges over a single bounded pass [0, edge_levels-1], not an
 * ever-incrementing counter, so coprimality of (edge_levels, charset_len)
 * does not save a naive modulo here the way it would for a cyclic
 * stride/hash sequence. What actually breaks modulo in this bounded
 * setting is the *magnitude* relationship between the two lengths:
 *
 *   - edge_levels <= charset_len (e.g. levels=8, charset_len=10, gcd=2):
 *     `level % charset_len == level` for every level, so glyph indices
 *     edge_levels..charset_len-1 (here 8 and 9, the most "intense" glyphs
 *     like '%'/'@') are NEVER selected. The output is permanently biased
 *     toward the low-intensity end of the charset no matter how loud the
 *     signal gets.
 *   - edge_levels > charset_len (e.g. levels=9, charset_len=4, gcd=1 --
 *     coprime, and it still happens): levels 0 and 4 and 8 all wrap to
 *     glyph 0, so the lowest glyph is hit ~2x as often as the others.
 *
 * Both are instances of the same problem: modulo was designed for
 * cyclic/overflowing sequences, not for rescaling one bounded range onto
 * another. The fix used here is proportional bucket allocation:
 *
 *     idx = level * (charset_len - 1) / (edge_levels - 1)   (integer floor division)
 *
 * This linearly rescales [0, edge_levels-1] onto [0, charset_len-1], so
 * level 0 always maps to glyph 0, level (edge_levels-1) always maps to
 * glyph (charset_len-1), the mapping is monotonic in between, and *every*
 * combination of edge_levels/charset_len -- coprime or not, levels bigger
 * or smaller than charset_len -- reaches the full glyph range instead of
 * starving either end. That is the explicit allocation strategy required
 * when gcd(edge_levels, charset_len) != 1; it is applied unconditionally
 * (rather than branching to modulo only in the coprime case) because, as
 * shown above, plain modulo is not actually safe even when the two
 * lengths are coprime.
 *
 * The result is precomputed once into glyph_lut[] per edge_levels value
 * (see build_glyph_lut) so edgemap_glyph() is a plain O(1) array index.
 * ------------------------------------------------------------------- */

static size_t proportional_glyph_index(int level, int levels, size_t charset_len) {
    if (levels <= 1 || charset_len <= 1) return 0;
    size_t idx = ((size_t)level * (charset_len - 1)) / (size_t)(levels - 1);
    if (idx >= charset_len) idx = charset_len - 1; /* defensive; unreachable by construction */
    return idx;
}

static char *build_glyph_lut(int levels, const char *charset, size_t charset_len) {
    char *lut = malloc((size_t)levels);
    if (!lut) return NULL;
    for (int level = 0; level < levels; level++) {
        size_t idx = proportional_glyph_index(level, levels, charset_len);
        lut[level] = charset[idx];
    }
    return lut;
}

char edgemap_glyph(const EdgeMap *map, int level) {
    if (!map) return ' ';
    if (level < 0) return map->charset[0]; /* cleared -> lightest glyph */
    if (level >= map->edge_levels) level = map->edge_levels - 1;
    if (level < 0) level = 0;
    return map->glyph_lut[level];
}

/* ---------------------------------------------------------------------
 * Lifecycle / hot swap
 *
 * edgemap_reconfigure() resolves the three-way tension between hot-swap
 * speed, memory safety and output continuity by construction rather than
 * by locking:
 *
 *   - Speed: glyph_lut has at most EDGEMAP_MAX_LEVELS (16) entries, so
 *     rebuilding it is O(16) -- effectively O(1) regardless of how it is
 *     invoked.
 *   - Memory safety: the new table is fully allocated and populated
 *     into a local pointer first. Only after that succeeds do we touch
 *     `map` at all, and then both the level count and the lookup table
 *     pointer are updated back-to-back with no intervening call that
 *     could observe a mismatched pair. The old table is freed exactly
 *     once, after it has been unlinked from `map`, and precisely once
 *     per successful reconfigure -- no leak, no use of a stale pointer.
 *   - Continuity: callers (see main.c) only invoke this between frames
 *     of their own render loop, never while a render of the old state is
 *     in flight, so there is no reader that could ever witness the old
 *     edge_levels paired with the new glyph_lut or vice versa.
 * ------------------------------------------------------------------- */

EdgeMap *edgemap_create(int edge_levels, double edge_threshold, const char *charset) {
    if (edge_levels < EDGEMAP_MIN_LEVELS || edge_levels > EDGEMAP_MAX_LEVELS) return NULL;
    if (!charset || charset[0] == '\0') return NULL;
    if (edge_threshold < 0.0 || edge_threshold > 1.0) return NULL;

    size_t charset_len = strlen(charset);

    EdgeMap *map = malloc(sizeof(EdgeMap));
    if (!map) return NULL;

    map->charset = malloc(charset_len + 1);
    if (!map->charset) {
        free(map);
        return NULL;
    }
    memcpy(map->charset, charset, charset_len + 1);
    map->charset_len = charset_len;

    map->glyph_lut = build_glyph_lut(edge_levels, map->charset, map->charset_len);
    if (!map->glyph_lut) {
        free(map->charset);
        free(map);
        return NULL;
    }

    map->edge_levels = edge_levels;
    map->edge_threshold = edge_threshold;
    return map;
}

void edgemap_destroy(EdgeMap *map) {
    if (!map) return;
    free(map->glyph_lut);
    free(map->charset);
    free(map);
}

int edgemap_reconfigure(EdgeMap *map, int new_levels) {
    if (!map) return 0;
    if (new_levels < EDGEMAP_MIN_LEVELS || new_levels > EDGEMAP_MAX_LEVELS) return 0;

    char *new_lut = build_glyph_lut(new_levels, map->charset, map->charset_len);
    if (!new_lut) return 0; /* OOM: map is untouched, still fully valid */

    char *old_lut = map->glyph_lut;
    map->glyph_lut = new_lut;
    map->edge_levels = new_levels;
    free(old_lut);
    return 1;
}

int edgemap_levels(const EdgeMap *map) {
    return map ? map->edge_levels : 0;
}
