/*
 * patterns_test.c — validate the .beat parser and the bank loader.
 */

#include "patterns.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  !! FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok: %s\n", msg); } \
} while (0)

static const char *SAMPLE =
    "# a comment line\n"
    "name: Boom Bap\n"
    "genre: LOFI\n"
    "steps: 16\n"
    "kick:  x.......x.......\n"
    "snare: ....A.......A...\n"
    "ch:    x.x.x.x.x.x.x.x.\n"
    "\n"
    "name: Amen\n"
    "genre: DNB\n"
    "steps: 32\n"
    "snare: ....A..g.g..A..g....A..g.g..A..g\n"
    "ride:  x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.\n"
    "\n"
    "name: Aliases And Pad\n"
    "genre: TEST\n"
    "steps: 16\n"
    "snare: X...o\n"            /* X->A, o->g, short row padded with '.' */
    "\n"
    "name: NoHitsDropped\n"     /* no voice rows -> rejected */
    "genre: TEST\n"
    "steps: 16\n";

int main(void)
{
    BeatBank bank = { NULL, 0 };
    bank.patterns = calloc(BB_MAX_PATTERNS, sizeof(BeatPattern));

    int added = bb_bank_parse_buffer(&bank, SAMPLE);
    printf("Parser\n");
    CHECK(added == 3, "3 valid patterns parsed (empty one dropped)");
    CHECK(bank.count == 3, "bank count is 3");

    const BeatPattern *p0 = &bank.patterns[0];
    CHECK(strcmp(p0->name, "Boom Bap") == 0, "name trimmed correctly");
    CHECK(strcmp(p0->genre, "LOFI") == 0, "genre parsed");
    CHECK(p0->steps == 16, "steps parsed");
    CHECK(strcmp(p0->rows[BB_VOICE_KICK], "x.......x.......") == 0, "kick row exact");
    CHECK(strlen(p0->rows[BB_VOICE_SNARE]) == 16, "snare row length 16");
    CHECK(bb_char_velocity(p0->rows[BB_VOICE_SNARE][4]) == BB_VEL_ACCENT, "accent char -> accent vel");

    const BeatPattern *p1 = &bank.patterns[1];
    CHECK(p1->steps == 32, "32-step pattern");
    CHECK(strlen(p1->rows[BB_VOICE_RIDE]) == 32, "32-step ride row length");
    CHECK(p1->rows[BB_VOICE_KICK][0] == '\0', "unused voice row is empty");

    const BeatPattern *p2 = &bank.patterns[2];
    CHECK(p2->rows[BB_VOICE_SNARE][0] == 'A', "'X' normalised to 'A'");
    CHECK(p2->rows[BB_VOICE_SNARE][4] == 'g', "'o' normalised to 'g'");
    CHECK(strlen(p2->rows[BB_VOICE_SNARE]) == 16, "short row padded to steps");

    free(bank.patterns);

    /* Loader fallback: with no readable folders we should still get patterns. */
    printf("\nLoader fallback\n");
    BeatBank fb = { NULL, 0 };
    int n = bb_bank_init(&fb, "/nonexistent/module/dir");
    CHECK(n >= 4, "fallback yields the built-in patterns");
    CHECK(fb.count >= 4 && strcmp(fb.patterns[0].name, "Boom Bap") == 0,
          "first fallback pattern is Boom Bap");
    bb_bank_free(&fb);

    if (failures) { printf("\nFAIL: %d check(s) failed\n", failures); return 1; }
    printf("\nOK: parser + loader valid\n");
    return 0;
}
