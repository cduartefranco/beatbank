/*
 * beatbank_plugin.c — Beat Bank: a library of fixed "standard" drum patterns.
 *
 * API: midi_fx_api_v1_t  (entry point: move_midi_fx_init)
 *
 * Deterministic step sequencer. It plays one of the patterns loaded from
 * external .beat files (see patterns.c) into the drum kit that follows it in
 * the chain. NO randomness, NO generation.
 *
 * Tempo and swing are intentionally absent — Beat Bank follows Move's
 * transport and tempo over MIDI clock and plays the grid straight, so it
 * never competes with Move's own groove. It is silent unless Move's MIDI
 * Clock Out is on and the transport is running.
 *
 *   0xFA reset to step 0 . 0xF8 advance (6 clocks per 16th) . 0xFC flush.
 *
 * Emission model: the clock is COUNTED in process_midi (0xF8), but note
 * events are QUEUED there and EMITTED from tick(). This matters for the
 * "Print into Move" workflow: the chain's Schw+Move (pre) mode injects a
 * MIDI FX's tick() output into Move's native track, but NOT its clock-driven
 * process_midi() output. Emitting from tick() lets the same pattern drive the
 * slot synth (normal mode) and Move's native kit (pre mode). The emission
 * point lags the counted clock by at most one audio block (~2.9 ms) — well
 * within record-quantise. See README "Print into Move's sequencer".
 */

#include "midi_fx_api_v1.h"
#include "plugin_api_v1.h"
#include "../dsp/patterns.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MIDI_NOTE_ON   0x90u
#define MIDI_NOTE_OFF  0x80u

#define CLOCKS_PER_STEP 6u   /* 24 PPQN / 4 sixteenths */
#define GATE_CLOCKS     2u   /* note length in clocks (< one step) */
#define OUT_CHANNEL     0u   /* the chain/slot rewrites the channel on output */

#define OUTQ_SIZE       128u /* pending emit ring (power-of-two not required) */

/* Per-voice note-override param keys, e.g. "kick_note". */
static char g_note_keys[BB_NUM_VOICES][16];

static const host_api_v1_t *g_host = NULL;

/* The pattern bank is read-only and shared across all instances. */
static BeatBank g_bank = { NULL, 0 };
static int      g_bank_loaded = 0;

typedef struct {
    uint8_t status;
    uint8_t d1;
    uint8_t d2;
} OutEvent;

typedef struct {
    uint8_t active;
    uint8_t note;
    uint8_t clocks_left;
} PendingNoteOff;

typedef struct {
    int     pattern;                       /* selected index into the bank   */
    uint8_t note[BB_NUM_VOICES];           /* output note per voice          */

    uint8_t cur_step;                      /* next step to fire              */
    uint8_t clock_running;                 /* Move transport running         */
    uint8_t run;                           /* 1 = loop, 0 = stopped          */
    int     print_remaining;               /* >0 = one-shot "Print" in flight */
    uint8_t midi_clocks_until_tick;
    uint32_t preview_revision;

    PendingNoteOff pending[BB_NUM_VOICES];

    OutEvent outq[OUTQ_SIZE];              /* events awaiting emission in tick */
    unsigned outq_head, outq_tail;

    int audition_voice;                    /* -1, or a voice to test-fire once */
} BeatBankInstance;

/* ── Emit queue ──────────────────────────────────────────────────────────── */

static void q_push(BeatBankInstance *bi, uint8_t status, uint8_t d1, uint8_t d2)
{
    unsigned next = (bi->outq_tail + 1u) % OUTQ_SIZE;
    if (next == bi->outq_head) return;     /* full: drop (never happens in practice) */
    bi->outq[bi->outq_tail].status = status;
    bi->outq[bi->outq_tail].d1 = d1;
    bi->outq[bi->outq_tail].d2 = d2;
    bi->outq_tail = next;
}

static void q_note_on(BeatBankInstance *bi, uint8_t note, uint8_t vel)
{
    q_push(bi, (uint8_t)(MIDI_NOTE_ON | OUT_CHANNEL), note, vel);
}

static void q_note_off(BeatBankInstance *bi, uint8_t note)
{
    q_push(bi, (uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), note, 0);
}

/* ── Bank helpers ────────────────────────────────────────────────────────── */

static const BeatPattern *pattern_at(int idx)
{
    if (g_bank.count <= 0) return NULL;
    if (idx < 0) idx = 0;
    if (idx >= g_bank.count) idx = g_bank.count - 1;
    return &g_bank.patterns[idx];
}

static uint8_t pattern_steps(const BeatBankInstance *bi)
{
    const BeatPattern *p = pattern_at(bi->pattern);
    if (!p || p->steps < 1) return 16u;
    return p->steps > BB_MAX_STEPS ? BB_MAX_STEPS : p->steps;
}

/* ── Sequencing (queues events; emission happens in tick) ────────────────── */

static void flush_all(BeatBankInstance *bi)
{
    for (int v = 0; v < BB_NUM_VOICES; v++) {
        if (bi->pending[v].active) {
            q_note_off(bi, bi->pending[v].note);
            bi->pending[v].active = 0;
            bi->pending[v].clocks_left = 0;
        }
    }
}

static void advance_pending_clocks(BeatBankInstance *bi)
{
    for (int v = 0; v < BB_NUM_VOICES; v++) {
        PendingNoteOff *p = &bi->pending[v];
        if (!p->active) continue;
        if (p->clocks_left > 0) p->clocks_left--;
        if (p->clocks_left == 0) {
            q_note_off(bi, p->note);
            p->active = 0;
        }
    }
}

static void fire_step(BeatBankInstance *bi)
{
    const BeatPattern *p = pattern_at(bi->pattern);
    uint8_t steps = pattern_steps(bi);
    uint8_t step = bi->cur_step;

    if (!p) return;
    if (step >= steps) step = 0;

    for (int v = 0; v < BB_NUM_VOICES; v++) {
        const char *row = p->rows[v];
        PendingNoteOff *pn = &bi->pending[v];
        uint8_t vel;

        if (pn->active) {                 /* close any still-open note first */
            q_note_off(bi, pn->note);
            pn->active = 0;
        }
        if (!row[0]) continue;
        if (step >= (uint8_t)strlen(row)) continue;
        vel = bb_char_velocity(row[step]);
        if (vel == 0) continue;

        q_note_on(bi, bi->note[v], vel);
        pn->active = 1;
        pn->note = bi->note[v];
        pn->clocks_left = GATE_CLOCKS;
    }

    bi->cur_step = (uint8_t)((step + 1) % steps);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static void *bb_create_instance(const char *module_dir, const char *config_json)
{
    BeatBankInstance *bi;
    (void)config_json;

    if (!g_bank_loaded) {
        bb_bank_init(&g_bank, module_dir);
        for (int v = 0; v < BB_NUM_VOICES; v++)
            snprintf(g_note_keys[v], sizeof(g_note_keys[v]), "%s_note", bb_voice_keys[v]);
        g_bank_loaded = 1;
    }

    bi = (BeatBankInstance *)calloc(1, sizeof(BeatBankInstance));
    if (!bi) return NULL;

    bi->pattern = 0;
    for (int v = 0; v < BB_NUM_VOICES; v++) bi->note[v] = bb_default_notes[v];
    bi->cur_step = 0;
    bi->clock_running = 0;
    bi->run = 1;                 /* loop by default (live play works zero-config) */
    bi->print_remaining = 0;
    bi->midi_clocks_until_tick = CLOCKS_PER_STEP;
    bi->preview_revision = 1;
    bi->outq_head = bi->outq_tail = 0;
    bi->audition_voice = -1;
    return bi;
}

static void bb_destroy_instance(void *instance)
{
    free(instance);
}

/* ── MIDI clock processing (counts clock, queues events) ─────────────────── */

static int bb_process_midi(void *instance,
                           const uint8_t *in_msg, int in_len,
                           uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    (void)out_msgs; (void)out_lens; (void)max_out;
    if (!bi || in_len == 0) return 0;

    if (in_msg[0] == 0xFAu) {                 /* Start */
        flush_all(bi);
        bi->cur_step = 0;
        bi->midi_clocks_until_tick = CLOCKS_PER_STEP;
        bi->clock_running = 1;
    } else if (in_msg[0] == 0xFBu) {          /* Continue */
        bi->clock_running = 1;
    } else if (in_msg[0] == 0xF8u) {          /* Clock tick */
        if (!bi->clock_running) return 0;
        advance_pending_clocks(bi);   /* always close gates, even when stopped */
        if (bi->midi_clocks_until_tick > 0) bi->midi_clocks_until_tick--;
        if (bi->midi_clocks_until_tick == 0) {
            if (bi->run || bi->print_remaining > 0) {
                fire_step(bi);
                if (bi->print_remaining > 0) bi->print_remaining--;  /* one-shot bar */
            }
            bi->midi_clocks_until_tick = CLOCKS_PER_STEP;
        }
    } else if (in_msg[0] == 0xFCu) {          /* Stop */
        bi->clock_running = 0;
        flush_all(bi);
    }
    return 0;   /* all output is emitted from tick() */
}

/* ── Tick: drains the event queue (so the chain can play synth + inject) ──── */

static int bb_tick(void *instance, int frames, int sample_rate,
                   uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    int count = 0;
    (void)frames; (void)sample_rate;
    if (!bi) return 0;

    /* Audition: fire the requested voice's current note once so the user can
     * map it to the kit by ear (injected to Move in Schw+Move mode). */
    {
        int av = bi->audition_voice;
        bi->audition_voice = -1;
        if (av >= 0 && av < BB_NUM_VOICES) {
            q_note_on(bi, bi->note[av], 100);
            q_note_off(bi, bi->note[av]);
        }
    }

    if (g_host && g_host->get_clock_status) {
        int status = g_host->get_clock_status();
        if ((status == MOVE_CLOCK_STATUS_STOPPED ||
             status == MOVE_CLOCK_STATUS_UNAVAILABLE) && bi->clock_running) {
            bi->clock_running = 0;
            flush_all(bi);
        }
    }

    while (bi->outq_head != bi->outq_tail && count < max_out) {
        OutEvent *e = &bi->outq[bi->outq_head];
        out_msgs[count][0] = e->status;
        out_msgs[count][1] = e->d1;
        out_msgs[count][2] = e->d2;
        out_lens[count] = 3;
        count++;
        bi->outq_head = (bi->outq_head + 1u) % OUTQ_SIZE;
    }
    return count;
}

/* ── Parameter I/O ───────────────────────────────────────────────────────── */

static int parse_int(const char *s, int lo, int hi, int dflt)
{
    int v;
    if (!s) return dflt;
    v = atoi(s);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static void bb_set_param(void *instance, const char *key, const char *val)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    if (!bi || !key || !val) return;

    if (strcmp(key, "pattern") == 0) {
        int hi = g_bank.count > 0 ? g_bank.count - 1 : 0;
        int idx = parse_int(val, 0, hi, 0);
        if (idx != bi->pattern) {
            bi->pattern = idx;
            if (bi->cur_step >= pattern_steps(bi)) bi->cur_step = 0;
            bi->preview_revision++;
        }
        return;
    }
    if (strcmp(key, "audition") == 0) {
        bi->audition_voice = parse_int(val, 0, BB_NUM_VOICES - 1, 0);
        return;
    }
    if (strcmp(key, "play") == 0) {           /* loop on/off */
        uint8_t r = (uint8_t)(parse_int(val, 0, 1, 1) != 0);
        if (!r) { bi->print_remaining = 0; flush_all(bi); }
        bi->run = r;
        return;
    }
    if (strcmp(key, "print") == 0) {          /* fire exactly one bar, then stop */
        flush_all(bi);
        bi->run = 0;
        bi->cur_step = 0;
        bi->midi_clocks_until_tick = 1;       /* start on the next clock */
        bi->print_remaining = pattern_steps(bi);
        return;
    }
    for (int v = 0; v < BB_NUM_VOICES; v++) {
        if (strcmp(key, g_note_keys[v]) == 0) {
            bi->note[v] = (uint8_t)parse_int(val, 0, 127, bb_default_notes[v]);
            /* Auto-audition: editing a drum note fires it so the user can map
             * it to the kit by ear. set_param for notes is only called on user
             * edits (load/restore goes through the saved state blob), so this
             * never fires spuriously on patch load. */
            bi->audition_voice = v;
            return;
        }
    }
}

/* Parse "<prefix>@<index>" → index, or -1 if it doesn't match. */
static int indexed_key(const char *key, const char *prefix)
{
    size_t pl = strlen(prefix);
    if (strncmp(key, prefix, pl) != 0 || key[pl] != '@') return -1;
    return atoi(key + pl + 1);
}

static int bb_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    const BeatPattern *p;
    int gi;

    if (!bi || !key || !buf || buf_len <= 0) return -1;
    p = pattern_at(bi->pattern);

    if (strcmp(key, "pattern") == 0)       return snprintf(buf, buf_len, "%d", bi->pattern);
    if (strcmp(key, "pattern_count") == 0) return snprintf(buf, buf_len, "%d", g_bank.count);
    if (strcmp(key, "pattern_name") == 0)  return snprintf(buf, buf_len, "%s", p ? p->name : "");
    if (strcmp(key, "pattern_label") == 0) return snprintf(buf, buf_len, "%s  %s", p ? p->name : "", p ? p->genre : "");
    if (strcmp(key, "play") == 0)          return snprintf(buf, buf_len, "%u", bi->run);
    if (strcmp(key, "printing") == 0)      return snprintf(buf, buf_len, "%d", bi->print_remaining > 0 ? 1 : 0);
    if (strcmp(key, "pattern_genre") == 0) return snprintf(buf, buf_len, "%s", p ? p->genre : "");
    if (strcmp(key, "steps") == 0)         return snprintf(buf, buf_len, "%u", pattern_steps(bi));
    if (strcmp(key, "play_step") == 0)     return snprintf(buf, buf_len, "%u", bi->cur_step);
    if (strcmp(key, "preview_rev") == 0)   return snprintf(buf, buf_len, "%u", bi->preview_revision);

    gi = indexed_key(key, "name");
    if (gi >= 0) { const BeatPattern *q = pattern_at(gi); return snprintf(buf, buf_len, "%s", q ? q->name : ""); }
    gi = indexed_key(key, "genre");
    if (gi >= 0) { const BeatPattern *q = pattern_at(gi); return snprintf(buf, buf_len, "%s", q ? q->genre : ""); }

    if (strncmp(key, "row", 3) == 0) {
        int v = atoi(key + 3);
        if (v >= 0 && v < BB_NUM_VOICES && p)
            return snprintf(buf, buf_len, "%s", p->rows[v]);
        return snprintf(buf, buf_len, "%s", "");
    }

    for (int v = 0; v < BB_NUM_VOICES; v++)
        if (strcmp(key, g_note_keys[v]) == 0)
            return snprintf(buf, buf_len, "%u", bi->note[v]);

    if (strcmp(key, "sync_warn") == 0) {
        if (g_host && g_host->get_clock_status) {
            int status = g_host->get_clock_status();
            if (status == MOVE_CLOCK_STATUS_UNAVAILABLE)
                return snprintf(buf, buf_len, "Enable MIDI Clock Out");
            if (status == MOVE_CLOCK_STATUS_STOPPED)
                return snprintf(buf, buf_len, "press Play");
        }
        return snprintf(buf, buf_len, "%s", "");
    }

    return -1;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

static midi_fx_api_v1_t g_api = {
    .api_version      = MIDI_FX_API_VERSION,
    .create_instance  = bb_create_instance,
    .destroy_instance = bb_destroy_instance,
    .process_midi     = bb_process_midi,
    .tick             = bb_tick,
    .set_param        = bb_set_param,
    .get_param        = bb_get_param,
};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
