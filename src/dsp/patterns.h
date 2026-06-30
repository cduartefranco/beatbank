/*
 * patterns.h
 * Drum-pattern model + loader for Beat Bank.
 *
 * Patterns are NOT compiled in. They are loaded at startup from external
 * ".beat" text files so users can add their own and edit the templates:
 *
 *     name: Boom Bap
 *     genre: LOFI
 *     steps: 16
 *     kick:  x.......x.......
 *     snare: ....A.......A...
 *     ch:    x.x.x.x.x.x.x.x.
 *
 * (blank line between patterns)
 *
 * Row characters:  '.' rest   'x' normal   'A' accent   'g' ghost
 *   ('X' is an alias for 'A', 'o' for 'g'.)  Spaces in a row are ignored.
 *
 * The loader scans two folders and concatenates them:
 *   1. <module_dir>/patterns/         shipped defaults (refreshed on upgrade)
 *   2. /data/UserData/schwung/beatbank/patterns/   user files (persist)
 *
 * Tempo and swing are deliberately NOT part of a pattern: Beat Bank plays
 * straight grids and follows Move's transport. Move owns the groove.
 */

#pragma once
#include <stdint.h>

/* ── Voices ─────────────────────────────────────────────────────────────── */

enum {
    BB_VOICE_KICK = 0,
    BB_VOICE_SNARE,
    BB_VOICE_CH,        /* closed hat                          */
    BB_VOICE_OH,        /* open hat                            */
    BB_VOICE_CLAP,
    BB_VOICE_RIM,       /* rimshot / side stick / clave        */
    BB_VOICE_TOM,       /* tom / low conga / surdo             */
    BB_VOICE_RIDE,
    BB_VOICE_CRASH,
    BB_VOICE_COWBELL,
    BB_VOICE_CONGA,     /* open/high conga / bongo             */
    BB_VOICE_PERC,      /* shaker / cabasa / maracas / tamb    */
    BB_NUM_VOICES
};

#define BB_MAX_STEPS     32
#define BB_MAX_PATTERNS  400

#define BB_VEL_GHOST     40u
#define BB_VEL_NORMAL    100u
#define BB_VEL_ACCENT    122u

/* Default GM drum note per voice (overridable at runtime). */
extern const uint8_t bb_default_notes[BB_NUM_VOICES];
/* Short UI labels per voice. */
extern const char *const bb_voice_labels[BB_NUM_VOICES];
/* The .beat file key for each voice ("kick", "snare", ...). */
extern const char *const bb_voice_keys[BB_NUM_VOICES];

/* User pattern folder (created on first run if absent). */
#define BB_USER_PATTERN_DIR "/data/UserData/schwung/beatbank/patterns"

/* ── Pattern + bank ─────────────────────────────────────────────────────── */

typedef struct {
    char    name[24];
    char    genre[12];
    uint8_t steps;                              /* 1..32                     */
    char    rows[BB_NUM_VOICES][BB_MAX_STEPS + 1]; /* "" = voice unused      */
} BeatPattern;

typedef struct {
    BeatPattern *patterns;   /* heap array of capacity BB_MAX_PATTERNS       */
    int          count;
} BeatBank;

/* Load the bank: scans <module_dir>/patterns and the user folder, then falls
 * back to a tiny built-in set if no files were found. Allocates bank->patterns
 * (caller owns it; free with bb_bank_free). Returns bank->count. module_dir
 * may be NULL (only the user folder + fallback are used). */
int  bb_bank_init(BeatBank *bank, const char *module_dir);
void bb_bank_free(BeatBank *bank);

/* Parse one .beat text buffer, appending patterns to the bank (used by the
 * loader and the tests). Returns the number of patterns added. */
int  bb_bank_parse_buffer(BeatBank *bank, const char *text);

/* Velocity for a row character, or 0 for a rest / unknown char. */
uint8_t bb_char_velocity(char c);
