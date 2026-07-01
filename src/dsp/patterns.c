/*
 * patterns.c — Beat Bank pattern model + ".beat" file loader.
 *
 * See patterns.h for the file format. No randomness, no generation: a pattern
 * is exactly the grid in the file.
 */

#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

const uint8_t bb_default_notes[BB_NUM_VOICES] = {
    36, /* KICK    */
    38, /* SNARE   */
    42, /* CH      */
    46, /* OH      */
    39, /* CLAP    */
    37, /* RIM     */
    45, /* TOM     */
    51, /* RIDE    */
    49, /* CRASH   */
    56, /* COWBELL */
    63, /* CONGA   */
    70, /* PERC    */
};

const char *const bb_voice_labels[BB_NUM_VOICES] = {
    "KCK", "SNR", "CH", "OH", "CLP", "RIM", "TOM", "RID", "CRS", "CWB", "CNG", "PRC"
};

const char *const bb_voice_keys[BB_NUM_VOICES] = {
    "kick", "snare", "ch", "oh", "clap", "rim", "tom", "ride", "crash", "cowbell", "conga", "perc"
};

uint8_t bb_char_velocity(char c)
{
    switch (c) {
        case 'A': case 'X': return BB_VEL_ACCENT;
        case 'x':           return BB_VEL_NORMAL;
        case 'g': case 'o': return BB_VEL_GHOST;
        default:            return 0u;
    }
}

/* ── .beat parsing ───────────────────────────────────────────────────────── */

static int voice_index(const char *key, int len)
{
    for (int v = 0; v < BB_NUM_VOICES; v++)
        if ((int)strlen(bb_voice_keys[v]) == len && strncmp(bb_voice_keys[v], key, len) == 0)
            return v;
    return -1;
}

/* Copy a value with surrounding whitespace trimmed into dst (size n). */
static void copy_trimmed(char *dst, int n, const char *src, int len)
{
    int i = 0;
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    for (; i < len && i < n - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* Copy a row value, dropping ALL whitespace, validating chars, normalising
 * 'X'->'A' and 'o'->'g'. Pads/truncates to `steps`. */
static void copy_row(char *dst, const char *src, int len, int steps)
{
    int n = 0;
    if (steps > BB_MAX_STEPS) steps = BB_MAX_STEPS;
    for (int i = 0; i < len && n < steps; i++) {
        char c = src[i];
        if (isspace((unsigned char)c)) continue;
        if      (c == 'X') c = 'A';
        else if (c == 'o') c = 'g';
        if (c != '.' && c != 'x' && c != 'A' && c != 'g') c = '.';
        dst[n++] = c;
    }
    while (n < steps) dst[n++] = '.';
    dst[n] = '\0';
}

static int pattern_has_hits(const BeatPattern *p)
{
    for (int v = 0; v < BB_NUM_VOICES; v++)
        for (const char *r = p->rows[v]; *r; r++)
            if (bb_char_velocity(*r)) return 1;
    return 0;
}

/* Finalise the in-progress pattern and append it to the bank if valid. */
static void finalise(BeatBank *bank, BeatPattern *cur, int *have)
{
    if (!*have) return;
    *have = 0;
    if (cur->steps < 1 || cur->steps > BB_MAX_STEPS) return;
    if (cur->name[0] == '\0') return;
    if (!pattern_has_hits(cur)) return;
    if (bank->count >= BB_MAX_PATTERNS) return;
    bank->patterns[bank->count++] = *cur;
}

int bb_bank_parse_buffer(BeatBank *bank, const char *text)
{
    BeatPattern cur;
    int have = 0;
    int added_start = bank->count;
    const char *p = text;

    if (!text) return 0;
    memset(&cur, 0, sizeof(cur));

    while (*p) {
        const char *line = p;
        const char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - line);

        /* skip leading whitespace for key detection */
        const char *s = line;
        int sl = line_len;
        while (sl > 0 && isspace((unsigned char)*s)) { s++; sl--; }

        if (sl == 0 || s[0] == '#') {
            /* blank line / comment — neither starts nor ends a pattern here;
             * patterns are delimited by the next "name:" or EOF. */
        } else {
            /* find "key: value" */
            const char *colon = memchr(s, ':', sl);
            if (colon) {
                int klen = (int)(colon - s);
                const char *val = colon + 1;
                int vlen = (int)(sl - klen - 1);

                if (klen == 4 && strncmp(s, "name", 4) == 0) {
                    finalise(bank, &cur, &have);
                    memset(&cur, 0, sizeof(cur));
                    cur.steps = 16;
                    copy_trimmed(cur.name, sizeof(cur.name), val, vlen);
                    have = 1;
                } else if (klen == 5 && strncmp(s, "genre", 5) == 0) {
                    copy_trimmed(cur.genre, sizeof(cur.genre), val, vlen);
                } else if (klen == 5 && strncmp(s, "steps", 5) == 0) {
                    char tmp[8];
                    copy_trimmed(tmp, sizeof(tmp), val, vlen);
                    int st = atoi(tmp);
                    cur.steps = (uint8_t)(st < 1 ? 1 : st > BB_MAX_STEPS ? BB_MAX_STEPS : st);
                } else {
                    int vi = voice_index(s, klen);
                    if (vi >= 0) copy_row(cur.rows[vi], val, vlen, cur.steps);
                }
            }
        }

        p = (*eol == '\n') ? eol + 1 : eol;
    }
    finalise(bank, &cur, &have);
    return bank->count - added_start;
}

/* ── File / directory loading ────────────────────────────────────────────── */

static char *read_whole_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long sz;
    char *buf;
    size_t got;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    sz = ftell(fp);
    if (sz < 0 || sz > 4 * 1024 * 1024) { fclose(fp); return NULL; }
    rewind(fp);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    got = fread(buf, 1, (size_t)sz, fp);
    buf[got] = '\0';
    fclose(fp);
    return buf;
}

static int ends_with_beat(const char *name)
{
    size_t n = strlen(name);
    return n > 5 && strcmp(name + n - 5, ".beat") == 0;
}

/* strdup is POSIX, not C99 — declare-free local copy to avoid feature-macro
 * pitfalls (under -std=c99 an undeclared strdup truncates its pointer to int
 * on 64-bit, corrupting the result). */
static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Scan one directory for *.beat files (sorted), parsing each into the bank. */
static void load_dir(BeatBank *bank, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *ent;
    char *names[BB_MAX_PATTERNS];
    int n = 0;

    if (!d) return;
    while ((ent = readdir(d)) != NULL && n < BB_MAX_PATTERNS) {
        if (ent->d_name[0] == '.') continue;
        if (!ends_with_beat(ent->d_name)) continue;
        names[n] = dup_str(ent->d_name);
        if (names[n]) n++;
    }
    closedir(d);

    qsort(names, n, sizeof(names[0]), cmp_str);

    for (int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        char *text = read_whole_file(path);
        if (text) { bb_bank_parse_buffer(bank, text); free(text); }
        free(names[i]);
    }
}

static void ensure_user_dir(void)
{
    /* mkdir -p of /data/UserData/schwung/beatbank/patterns (ignore EEXIST). */
    const char *parts[] = {
        "/data/UserData/schwung/beatbank",
        "/data/UserData/schwung/beatbank/patterns",
    };
    const char *howto = BB_USER_PATTERN_DIR "/_HOWTO.txt";
    FILE *fp;

    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
        mkdir(parts[i], 0775);

    /* Seed a one-time help file so users discover the format. */
    fp = fopen(howto, "r");
    if (fp) { fclose(fp); return; }
    fp = fopen(howto, "w");
    if (!fp) return;
    fputs(
        "Beat Bank — add your own drum patterns here\n"
        "===========================================\n\n"
        "Create a file ending in .beat in this folder. Each pattern is a\n"
        "block of lines; separate patterns with a blank line:\n\n"
        "    name: My Pattern\n"
        "    genre: HOUSE\n"
        "    steps: 16\n"
        "    kick:  x...x...x...x...\n"
        "    clap:  ....x.......x...\n"
        "    ch:    x.x.x.x.x.x.x.x.\n\n"
        "steps is 16 (one bar) or 32 (two bars). Row characters:\n"
        "    .  rest      x  hit      A  accent      g  ghost (soft)\n\n"
        "Voices: kick snare ch oh clap rim tom ride crash cowbell conga perc\n"
        "Omit voices that don't play. Rows must be exactly 'steps' long.\n"
        "Your files are added after the built-in patterns. Reload the\n"
        "module (or restart Schwung) to pick up changes.\n",
        fp);
    fclose(fp);
}

/* Tiny built-in fallback so the module is never silent if no files load. */
static const char *kFallback =
    "name: Boom Bap\n"      "genre: LOFI\n"   "steps: 16\n"
    "kick:  x.......x.......\n" "snare: ....A.......A...\n" "ch:    x.x.x.x.x.x.x.x.\n\n"
    "name: Two-Step DnB\n"  "genre: DNB\n"    "steps: 16\n"
    "kick:  x.........x.....\n" "snare: ....A.......A...\n" "ch:    x.x.x.x.x.x.x.x.\n\n"
    "name: Classic House\n" "genre: HOUSE\n"  "steps: 16\n"
    "kick:  x...x...x...x...\n" "clap:  ....A.......A...\n"
    "ch:    x.x.x.x.x.x.x.x.\n"  "oh:    ..x...x...x...x.\n\n"
    "name: Driving Techno\n" "genre: TECHNO\n" "steps: 16\n"
    "kick:  x...x...x...x...\n" "clap:  ....A.......A...\n"
    "ch:    x.x.x.x.x.x.x.x.\n"  "oh:    ..x...x...x...x.\n";

#define BB_MAX_GENRES 64

void bb_bank_group_by_genre(BeatBank *bank)
{
    char order[BB_MAX_GENRES][sizeof(((BeatPattern*)0)->genre)];
    int no = 0;
    BeatPattern *out;
    int k = 0;

    if (!bank || bank->count <= 1 || !bank->patterns) return;

    /* Distinct genres in first-appearance order. */
    for (int i = 0; i < bank->count; i++) {
        const char *g = bank->patterns[i].genre;
        int found = 0;
        for (int j = 0; j < no; j++) if (strcmp(order[j], g) == 0) { found = 1; break; }
        if (!found && no < BB_MAX_GENRES) {
            strncpy(order[no], g, sizeof(order[0]) - 1);
            order[no][sizeof(order[0]) - 1] = '\0';
            no++;
        }
    }

    out = (BeatPattern *)malloc((size_t)bank->count * sizeof(BeatPattern));
    if (!out) return;
    for (int j = 0; j < no; j++)
        for (int i = 0; i < bank->count; i++)
            if (strcmp(bank->patterns[i].genre, order[j]) == 0)
                out[k++] = bank->patterns[i];
    /* k == count unless a genre overflowed BB_MAX_GENRES; guard by copying k. */
    memcpy(bank->patterns, out, (size_t)k * sizeof(BeatPattern));
    free(out);
}

int bb_bank_init(BeatBank *bank, const char *module_dir)
{
    bank->count = 0;
    bank->patterns = (BeatPattern *)calloc(BB_MAX_PATTERNS, sizeof(BeatPattern));
    if (!bank->patterns) return 0;

    if (module_dir) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/patterns", module_dir);
        load_dir(bank, dir);
    }

    ensure_user_dir();
    load_dir(bank, BB_USER_PATTERN_DIR);

    if (bank->count == 0)
        bb_bank_parse_buffer(bank, kFallback);

    bb_bank_group_by_genre(bank);
    return bank->count;
}

void bb_bank_free(BeatBank *bank)
{
    if (bank && bank->patterns) {
        free(bank->patterns);
        bank->patterns = NULL;
        bank->count = 0;
    }
}
