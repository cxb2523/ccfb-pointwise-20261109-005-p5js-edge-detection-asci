# oscviz

C library + terminal program that renders a mono PCM16 WAV's short-time
energy envelope as a live ASCII edge-intensity meter.

## Layout

- `include/envelope.h`, `src/envelope.c` — WAV framing (1024 samples/frame,
  512 hop) and Hann-windowed RMS envelope extraction, normalized to [0,1].
- `include/edgemap.h`, `src/edgemap.c` — the level-grading state machine:
  threshold clamp, smoothstep-blended level classification, proportional
  glyph allocation, and the hot-swap (`edgemap_reconfigure`).
- `src/main.c` — CLI + render loop + non-blocking stdin command reader.
- `tests/test_edgemap.c` — acceptance tests, run under valgrind by `make test`.
- `tools/gen_sample.c` — dev-only generator for the demo `sample.wav` (not
  part of `make`/`make test`).

Targets POSIX (Linux/macOS/WSL) — the CLI uses `select()`/`nanosleep()`.

## Build & run

```
make                 # builds liboscviz.a and oscviz
./oscviz sample.wav --levels 8 --charset ' .:-=+*#%@'
```

While it's running, type `levels 3` + Enter to hot-swap the grading state
machine live; the next frame renders with the new level count.

```
make test            # builds tests/test_edgemap, runs it under
                      # `valgrind --error-exitcode=1`; fails the build on
                      # any failed assertion OR any memory error/leak.
```

## The three tensions, and how the design resolves each

**1. Grading precision vs. hard cuts.** `[edge_threshold, 1.0]` is split
into `edge_levels` equal cells. A plain `floor()` would hard-snap right at
each cell boundary. Instead `edgemap_classify_continuous()` (edgemap.c)
computes the cell index `L` and in-cell fraction, and only within a small
window near the *upper* edge of each cell does it blend `L -> L+1` through
the cubic smoothstep `3t²-2t³` (zero slope at both ends, so it doesn't
introduce new discontinuities of its own). The blend window's width is
`TRANSITION_FRACTION * (1/edge_levels)` — a fraction *of the cell*, not an
absolute number — so it mechanically narrows as `edge_levels` grows
(verified by `test_transition_width_shrinks`, which asserts a strictly
decreasing sequence across all 15 supported level counts). A flat,
levels-independent width was treated as a spec violation and isn't present
anywhere in the width calculation.

**2. Full charset coverage regardless of gcd(edge_levels, charset_len).**
Naive `charset[level % charset_len]` breaks in both directions over the
bounded range `level ∈ [0, edge_levels-1]`:
- `edge_levels ≤ charset_len` (e.g. 8 levels, 10-glyph charset): modulo
  never wraps, so glyphs `edge_levels..charset_len-1` — the most intense
  glyphs — are never emitted no matter how loud the input gets.
- `edge_levels > charset_len` (e.g. 9 levels, 4-glyph charset, which is
  coprime — gcd 1 doesn't save you here): the low glyphs get hit twice as
  often as the rest.

`edgemap.c`'s `proportional_glyph_index()` instead rescales the level
range onto the glyph range by floor division:
`idx = level*(charset_len-1) / (edge_levels-1)`. This hits glyph `0` at
level `0`, glyph `charset_len-1` at the top level, is monotonic in
between, and reaches the full glyph range for every `(edge_levels,
charset_len)` pair — coprime or not. `test_charset_coprime` (9 vs 4) and
`test_charset_noncoprime` (8 vs 10) both assert full-range coverage and
monotonicity. The full derivation is in the comment above
`proportional_glyph_index` in edgemap.c.

**3. Hot-swap speed, memory safety and output continuity, together.**
`edgemap_reconfigure()` builds the new `edge_levels`-sized glyph table into
a local pointer first; `map` is only touched after that build succeeds, and
then `glyph_lut`/`edge_levels` are updated back-to-back with no call in
between that could read a half-updated pair. The old table is freed exactly
once, after being unlinked. A rejected request (bad `new_levels` or OOM)
never touches `map` at all. Combined with `main.c` only ever calling this
between frames of its own single-threaded render loop (never while a frame
is "in flight"), there is no reader that can ever observe mismatched
state — memory safety and continuity fall out of the sequencing, not a
lock. Speed is O(new_levels) ≤ 16, i.e. effectively O(1). `make test`'s
valgrind run with `--error-exitcode=1` is the enforcement mechanism: any
leak, double free or stale-pointer read fails the build.
