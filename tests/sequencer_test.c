/*
 * sequencer_test.c — drive the Beat Bank plugin through MIDI clock and verify
 * note placement. Loads a real .beat file via the bank loader (module_dir).
 *
 * Covers:
 *   - move-clock path (0xFA start + 0xF8 ticks), straight 16th stepping
 *   - correct step placement (kick on beats, full-grid hat every 6 clocks)
 *   - pattern switch via set_param
 *   - 0xFC flushes held notes
 */

#include "midi_fx_api_v1.h"
#include "plugin_api_v1.h"
#include "patterns.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host);

static host_api_v1_t g_host = { .api_version = 1, .sample_rate = 44100 };

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  !! FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok: %s\n", msg); } \
} while (0)

static const char *MODROOT = "build/native/test_modroot";

static void write_test_patterns(void)
{
    mkdir("build", 0775);
    mkdir("build/native", 0775);
    mkdir(MODROOT, 0775);
    mkdir("build/native/test_modroot/patterns", 0775);
    FILE *fp = fopen("build/native/test_modroot/patterns/00-test.beat", "wb");
    if (!fp) { printf("  !! FAIL: cannot write test .beat\n"); failures++; return; }
    fputs(
        "name: All Hits\n"
        "genre: TEST\n"
        "steps: 16\n"
        "ch:    xxxxxxxxxxxxxxxx\n"
        "kick:  x...x...x...x...\n"
        "\n"
        "name: Backbeat\n"
        "genre: TEST\n"
        "steps: 16\n"
        "kick:  x...x...x...x...\n"
        "snare: ....A.......A...\n",
        fp);
    fclose(fp);
}

/* Run one bar; record clock numbers where a note-on for match_note fires
 * (match_note < 0 = any). Note events are emitted from tick(), so we pump
 * tick() right after each clock to drain the queue (emulating the chain). */
static int run_bar(midi_fx_api_v1_t *api, void *inst, int match_note, int *clk, int max)
{
    uint8_t out[MIDI_FX_MAX_OUT_MSGS][3];
    int lens[MIDI_FX_MAX_OUT_MSGS];
    uint8_t b;
    int count = 0;
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
    for (int c = 1; c <= 96; c++) {
        b = 0xF8;
        api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
        int n = api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
        for (int i = 0; i < n; i++) {
            if ((out[i][0] & 0xF0) == 0x90 && out[i][2] > 0 &&
                (match_note < 0 || out[i][1] == match_note)) {
                if (count < max) clk[count] = c;
                count++;
                break;
            }
        }
    }
    return count;
}

int main(void)
{
    write_test_patterns();

    midi_fx_api_v1_t *api = move_midi_fx_init(&g_host);
    int clk[64];

    /* First create_instance loads the global bank from our module dir. */
    void *inst = api->create_instance(MODROOT, NULL);
    char buf[32];
    int count = (api->get_param(inst, "pattern_count", buf, sizeof(buf)), atoi(buf));
    printf("Loaded %d patterns from .beat file\n", count);
    CHECK(count >= 2, "loaded the two test patterns");

    /* Pattern 0 = All Hits: every step fires (ch), kick on beats. */
    api->set_param(inst, "pattern", "0");

    printf("\nStraight 16ths: a step every 6 clocks\n");
    int ns = run_bar(api, inst, -1, clk, 64);
    CHECK(ns == 16, "16 step-fires in the bar");
    CHECK(ns >= 3 && clk[0] == 6 && clk[1] == 12 && clk[2] == 18, "fires at 6,12,18,...");
    CHECK(ns == 16 && clk[15] == 96, "bar ends at clock 96");

    printf("\nKick lands on beats 1-4 (clocks 6,30,54,78)\n");
    int nk = run_bar(api, inst, 36, clk, 64);
    CHECK(nk == 4 && clk[0] == 6 && clk[1] == 30 && clk[2] == 54 && clk[3] == 78,
          "kick at clocks 6,30,54,78");

    /* Switch to pattern 1 = Backbeat: snare (38) on beats 2 & 4. */
    printf("\nPattern switch -> snare backbeat\n");
    api->set_param(inst, "pattern", "1");
    int nsn = run_bar(api, inst, 38, clk, 64);
    CHECK(nsn == 2 && clk[0] == 30 && clk[1] == 78, "snare at clocks 30,78 (beats 2 & 4)");

    /* Stop flushes (note-offs are queued by 0xFC, emitted on the next tick). */
    printf("\nStop flushes held notes\n");
    uint8_t out[MIDI_FX_MAX_OUT_MSGS][3]; int lens[MIDI_FX_MAX_OUT_MSGS]; uint8_t b;
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
    for (int c = 1; c <= 5; c++) { b = 0xF8; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS); }
    b = 0xF8; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);  /* step 0 fires (kick) */
    api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);              /* drain the note-on */
    b = 0xFC; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS); /* queues note-off(s) */
    int n = api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
    int offs = 0;
    for (int i = 0; i < n; i++)
        if ((out[i][0] & 0xF0) == 0x80 || ((out[i][0] & 0xF0) == 0x90 && out[i][2] == 0)) offs++;
    CHECK(offs >= 1, "stop emits note-off(s)");

    /* Audition: set_param("audition", v) fires voice v's note on the next tick. */
    printf("\nAudition fires the voice's note\n");
    api->set_param(inst, "kick_note", "36");
    api->set_param(inst, "audition", "0");   /* voice 0 = kick */
    int an = api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
    int got_on = 0, got_off = 0;
    for (int i = 0; i < an; i++) {
        if ((out[i][0] & 0xF0) == 0x90 && out[i][1] == 36 && out[i][2] > 0) got_on = 1;
        if (((out[i][0] & 0xF0) == 0x80 || (out[i][2] == 0)) && out[i][1] == 36) got_off = 1;
    }
    CHECK(got_on && got_off, "audition emits note-on + note-off for the voice note");

    /* One-shot Print: fires exactly one bar (16 step-fires) then stops. */
    printf("\nPrint fires exactly one bar then stops\n");
    api->set_param(inst, "pattern", "0");     /* All Hits: every step fires */
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
    api->set_param(inst, "print", "1");
    int fires = 0;
    for (int c = 1; c <= 240; c++) {           /* well past one 16-step bar */
        b = 0xF8; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
        int n = api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
        for (int i = 0; i < n; i++)
            if ((out[i][0] & 0xF0) == 0x90 && out[i][2] > 0) { fires++; break; }
    }
    CHECK(fires == 16, "print emits exactly 16 step-fires (one bar) then stops");

    /* play=0 stops the loop. */
    printf("\nplay=0 stops, play=1 resumes\n");
    api->set_param(inst, "play", "0");
    int after_stop = run_bar(api, inst, -1, clk, 64);
    CHECK(after_stop == 0, "stopped: no fires");
    api->set_param(inst, "play", "1");
    int after_play = run_bar(api, inst, -1, clk, 64);
    CHECK(after_play == 16, "resumed: 16 fires");

    api->destroy_instance(inst);

    if (failures) { printf("\nFAIL: %d check(s) failed\n", failures); return 1; }
    printf("\nOK: sequencer behaves correctly\n");
    return 0;
}
