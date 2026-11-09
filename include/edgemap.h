#ifndef OSCVIZ_EDGEMAP_H
#define OSCVIZ_EDGEMAP_H

#include <stddef.h>

#define EDGEMAP_MIN_LEVELS 2
#define EDGEMAP_MAX_LEVELS 16

/* Opaque level-grading state machine. Holds the threshold, the owned
 * charset copy and a precomputed level->glyph lookup table sized to the
 * current edge_levels. The lookup table is exactly what gets freed and
 * rebuilt by edgemap_reconfigure() on a runtime `levels N` hot swap. */
typedef struct EdgeMap EdgeMap;

/* Builds a grading state machine. charset must be non-NULL, non-empty and
 * NUL-terminated; it is copied, the caller retains ownership of the
 * original. edge_levels must be in [EDGEMAP_MIN_LEVELS, EDGEMAP_MAX_LEVELS].
 * Returns NULL on invalid arguments or allocation failure (nothing is
 * partially constructed in that case). */
EdgeMap *edgemap_create(int edge_levels, double edge_threshold, const char *charset);

void edgemap_destroy(EdgeMap *map);

/* Rebuilds the glyph lookup table for a new level count while keeping
 * threshold/charset. The new table is fully built and validated *before*
 * anything in `map` is mutated; on failure (bad new_levels or OOM) `map`
 * is left byte-for-byte as it was and 0 is returned, so a caller can
 * always keep rendering with the previous, still-valid configuration.
 * Returns 1 on success. */
int edgemap_reconfigure(EdgeMap *map, int new_levels);

int edgemap_levels(const EdgeMap *map);

/* Classifies a normalized intensity into a discrete level using smoothstep
 * -blended boundaries (see edgemap.c) instead of a hard floor(). Inputs
 * below edge_threshold return -1 ("cleared"). Out-of-range inputs are
 * clamped rather than treated as an error. The return value is always in
 * {-1} U [0, edge_levels-1], so it is always a safe index into the current
 * glyph table. */
int edgemap_classify(const EdgeMap *map, double intensity);

/* Same classification, but returns the pre-rounding continuous level
 * (range [-1.0, edge_levels-1] where -1.0 means cleared) so tests/tools can
 * inspect the smoothstep blend directly instead of only its rounded
 * output. edgemap_classify() is exactly lround()-ish rounding of this. */
double edgemap_classify_continuous(const EdgeMap *map, double intensity);

/* Maps a level in {-1} U [0, edge_levels-1] to a glyph from the configured
 * charset via the proportional bucket allocation documented in edgemap.c.
 * Always returns a glyph that is actually present in charset. */
char edgemap_glyph(const EdgeMap *map, int level);

/* Cubic smoothstep (3t^2 - 2t^3), clamped outside [edge0, edge1]. Exposed
 * for unit testing. */
double edgemap_smoothstep(double edge0, double edge1, double x);

/* Half-width (in normalized [0,1] units, same axis as the post-threshold
 * position) of the smoothstep blend zone around each level boundary, for
 * a given level count. Exposed so tests can verify it strictly narrows as
 * edge_levels grows, per spec. */
double edgemap_transition_width(int edge_levels);

#endif
