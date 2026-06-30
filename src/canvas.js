/*
 * canvas.js — Beat Bank fullscreen pattern view (Schwung canvas overlay).
 *
 * Opened from the MIDI FX menu's "Pattern View" entry. Shows the step grid,
 * scrolls the library with the jog wheel, and gives an explicit one-shot
 * Print (records one clean bar into Move when its track is armed).
 *
 * Host contract: expose globalThis.canvas_overlay with onOpen/tick/draw/onMidi.
 * The host steals jog-click and Back to close the canvas, so controls live on
 * the jog-turn and pads (which reach onMidi).
 */

'use strict';

const NUM_VOICES = 12;
const VOICE_LABELS = ['KCK', 'SNR', 'CH', 'OH', 'CLP', 'RIM', 'TOM', 'RID', 'CRS', 'CWB', 'CNG', 'PRC'];

const CC_JOG = 14;
const PAD_PRINT = 68;   /* bottom-left pad */
const PAD_PLAY  = 69;   /* next pad        */

const g = {
  count: 1, pattern: 0, steps: 16, name: '', genre: '',
  rows: new Array(NUM_VOICES).fill(''),
  playStep: 0, run: 1, printing: 0, rev: -1,
};

function gp(ctx, k) { const v = ctx.getParam(k); return v === undefined || v === null ? '' : v; }
function gpi(ctx, k, d) { const v = parseInt(gp(ctx, k), 10); return Number.isFinite(v) ? v : d; }

function loadPattern(ctx, force) {
  const rev = gpi(ctx, 'preview_rev', 0);
  if (!force && rev === g.rev) return;
  g.rev = rev;
  g.count = Math.max(1, gpi(ctx, 'pattern_count', 1));
  g.pattern = gpi(ctx, 'pattern', 0);
  g.steps = Math.max(1, Math.min(32, gpi(ctx, 'steps', 16)));
  g.name = gp(ctx, 'pattern_name');
  g.genre = gp(ctx, 'pattern_genre');
  for (let v = 0; v < NUM_VOICES; v++) g.rows[v] = gp(ctx, 'row' + v);
}

function selectPattern(ctx, delta) {
  const n = g.count;
  const next = (((g.pattern + delta) % n) + n) % n;
  if (next === g.pattern) return;
  g.pattern = next;
  ctx.setParam('pattern', String(next));
  loadPattern(ctx, true);
}

function draw(ctx) {
  ctx.print(0, 0, (g.genre || '').slice(0, 7), 1);
  ctx.print(40, 0, (g.name || '').slice(0, 9), 1);
  ctx.print(98, 0, (g.pattern + 1) + '/' + g.count, 1);

  const used = [];
  for (let v = 0; v < NUM_VOICES; v++) if (g.rows[v] && g.rows[v].length) used.push(v);

  const steps = g.steps;
  const gridX = 20, gridW = 106;
  const cellW = Math.max(2, Math.floor(gridW / steps));
  const topY = 9;
  const rowH = Math.min(6, Math.floor((46 - topY) / Math.max(1, used.length)));

  if (g.playStep < steps) ctx.drawRect(gridX + g.playStep * cellW, topY - 2, cellW, 1, 1);

  for (let r = 0; r < used.length; r++) {
    const v = used[r], row = g.rows[v], y = topY + r * rowH;
    ctx.print(0, y, VOICE_LABELS[v], 1);
    for (let s = 0; s < steps && s < row.length; s++) {
      const c = row[s];
      if (c === '.') continue;
      const x = gridX + s * cellW, w = Math.max(1, cellW - 1);
      if (c === 'g') {
        ctx.fillRect(x, y + 2, w, Math.max(1, rowH - 4), 1);
      } else {
        ctx.fillRect(x, y + 1, w, rowH - 2, 1);
        if (c === 'A') ctx.drawRect(x, y, w, 1, 1);
      }
    }
  }

  const state = g.printing ? 'PRINT' : (g.run ? 'LOOP' : 'STOP');
  ctx.print(0, 50, 'P1:Print  P2:' + (g.run ? 'Stop' : 'Play'), 1);
  ctx.print(0, 57, 'jog:pattern   [' + state + ']', 1);
}

globalThis.canvas_overlay = {
  onOpen(ctx) {
    loadPattern(ctx, true);
    g.playStep = gpi(ctx, 'play_step', 0) & 31;
    g.run = gpi(ctx, 'play', 1);
  },
  tick(ctx) {
    g.playStep = gpi(ctx, 'play_step', g.playStep) & 31;
    g.run = gpi(ctx, 'play', g.run);
    g.printing = gpi(ctx, 'printing', 0);
    loadPattern(ctx, false);
  },
  draw(ctx) {
    draw(ctx);
    return true;
  },
  onMidi(ctx, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 2) return;
    const type = d[0] & 0xF0, b1 = d[1], b2 = d.length > 2 ? d[2] : 0;

    if (type === 0xB0 && b1 === CC_JOG) {
      selectPattern(ctx, b2 > 0 && b2 < 64 ? 1 : -1);
      return;
    }
    if (type === 0x90 && b2 > 0) {
      if (b1 === PAD_PRINT) {
        ctx.setParam('print', '1');
        g.printing = 1;
      } else if (b1 === PAD_PLAY) {
        const nr = g.run ? 0 : 1;
        ctx.setParam('play', String(nr));
        g.run = nr;
      }
    }
  },
  onClose() {},
  onExit() {},
};
