"""A page to listen to what was found.

SNAPSHOTS: every snapshot by energy; beside each what it is like, a step up or down, what followed it, and for each of
its lanes the same lane from other snapshots that would fit the rest of it (SWAP: pressed, the snapshot plays with
that lane taken from the other one).
PARTS: every lane of every snapshot on its own (the same part in several snapshots once), lane by lane; beside each
the parts of other lanes that go with it (pressed, the two play together).

Parts are played from the parts kept beside the page, together and in time (each at the tempo of the one it is put
with). Served from this machine by the analysis itself (http://localhost:8765/report.html)."""
from __future__ import annotations

import json
import os
import time
from pathlib import Path

from .measure import LANES


def _safe(s: str) -> str:
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in s)


def write_report(root: Path, rows: list[dict], measured: list[dict], found: dict, snaps: dict | None = None):
    names = {r["uid"]: (r.get("name") or r["uid"][-6:]) for r in rows}
    bpm = {r["uid"]: r.get("bpm") for r in rows}
    clips = {r["uid"]: f"clips/{_safe(r['uid'])}.ogg" for r in rows if (root / "clips" / f"{_safe(r['uid'])}.ogg").exists()}
    have = {}
    for r in rows:
        d = root / "parts" / _safe(r["uid"])
        if d.is_dir():
            have[r["uid"]] = sorted(f.stem for f in d.glob("*.ogg"))
    data = {
        "names": names,
        "bpm": bpm,
        "clips": clips,
        "have": have,
        "safe": {u: _safe(u) for u in names},
        "lanes": LANES,
        "found": found.get("snapshots", {}),
        "parts": found.get("parts", {}),
        "counts": found.get("counts", {}),
        "made": time.strftime("%Y-%m-%d %H:%M", time.localtime(found.get("at", 0) / 1000)),
        "unmeasured": [names.get(m["uid"], m["uid"]) for m in measured if not m.get("mix") or m["mix"].get("silent")],
    }
    page = PAGE.replace("/*DATA*/null", json.dumps(data, separators=(",", ":")).replace("</", "<\\/"))
    tmp = root / "report.html.part"
    tmp.write_text(page)
    os.replace(tmp, root / "report.html")


PAGE = r"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>SF-000 Analysis</title><link rel="icon" href="data:,">
<style>
  :root { color-scheme: dark; --bg:#141414; --card:#1f1f1f; --ink:#e9e6df; --dim:#8d8a84; --hot:#f2c230; --line:#2c2c2c; --new:#7fc8a9; }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--ink); font:14px/1.4 -apple-system, "Helvetica Neue", Arial, sans-serif; }
  header { position:sticky; top:0; z-index:2; background:#101010f2; border-bottom:1px solid var(--line); padding:12px 22px; display:flex; gap:16px; align-items:center; flex-wrap:wrap; }
  h1 { font-size:15px; letter-spacing:.12em; margin:0; font-weight:700; }
  .dim { color:var(--dim); }
  input { background:#0c0c0c; border:1px solid var(--line); color:var(--ink); padding:6px 9px; border-radius:6px; width:200px; }
  .tabs { display:flex; gap:4px; }
  .tab, .lanebtn { border:1px solid #3a3a3a; background:transparent; color:var(--dim); border-radius:5px; padding:5px 11px; font-size:12px; letter-spacing:.08em; cursor:pointer; }
  .tab.on, .lanebtn.on { color:#111; background:var(--ink); border-color:var(--ink); }
  [hidden] { display:none !important; }
  .chip small.tag { color:var(--dim); letter-spacing:.06em; }
  .chip small.tag.chosen { color:var(--hot); }
  .lanebar { display:flex; gap:4px; flex-wrap:wrap; padding:10px 22px 0; }
  main { padding:8px 22px 80px; }
  .row { display:grid; grid-template-columns: 230px 1fr; gap:14px; padding:10px 0; border-bottom:1px solid var(--line); }
  .me { display:flex; flex-direction:column; gap:6px; }
  .bar { height:6px; background:#2a2a2a; border-radius:3px; overflow:hidden; }
  .bar i { display:block; height:100%; background:var(--hot); }
  .rel { display:grid; grid-template-columns: 112px 1fr; gap:4px 10px; align-items:start; }
  .rel b { font-weight:600; font-size:11px; letter-spacing:.1em; color:var(--dim); padding-top:5px; }
  .rel b.sub { padding-left:10px; font-weight:500; }
  .chips { display:flex; flex-wrap:wrap; gap:5px; }
  .chip { border:1px solid #3a3a3a; background:var(--card); color:var(--ink); border-radius:5px; padding:4px 8px; font-size:12px; cursor:pointer; white-space:nowrap; }
  .chip small { color:var(--dim); margin-left:5px; }
  .chip:hover { border-color:#666; }
  .chip.picked { border-color:var(--hot); }
  .chip.playing { background:var(--hot); color:#111; border-color:var(--hot); }
  .chip.playing small { color:#333; }
  .big { font-size:14px; font-weight:600; padding:7px 10px; text-align:left; }
  .num { font-variant-numeric: tabular-nums; font-size:11px; }
  #combo { position:fixed; left:0; right:0; bottom:0; z-index:3; background:#0d0d0df5; border-top:1px solid #3a3a3a; padding:10px 22px; display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
  #combo .lane { border:1px solid #3a3a3a; background:var(--card); color:var(--ink); border-radius:5px; padding:4px 8px; font-size:11px; cursor:pointer; white-space:nowrap; }
  #combo .lane small { color:var(--hot); margin-left:4px; }
  #combo .lane.off { opacity:.45; text-decoration:line-through; }
  #combo .lane.from { border-color:var(--hot); }
  #combo .lgrp { display:inline-flex; align-items:center; gap:2px; }
  #combo .back { border:1px solid #3a3a3a; border-radius:4px; padding:3px 5px; font-size:10px; letter-spacing:.06em; color:var(--dim); cursor:pointer; }
  #combo .back:hover { color:var(--ink); border-color:var(--ink); }
  #combo input { width:150px; }
  #combo .act { border:1px solid var(--ink); background:transparent; color:var(--ink); border-radius:5px; padding:5px 11px; font-size:12px; letter-spacing:.08em; cursor:pointer; }
  #combo .act.go { background:var(--hot); color:#111; border-color:var(--hot); font-weight:700; }
  #combo .act.playing { background:var(--hot); color:#111; border-color:var(--hot); font-weight:700; }
  #combo .said { color:var(--dim); font-size:12px; }
  main { padding-bottom:140px !important; }
  .mrow { display:flex; gap:10px; align-items:center; flex-wrap:wrap; padding:10px 0; }
  .act { border:1px solid var(--ink); background:transparent; color:var(--ink); border-radius:5px; padding:5px 11px; font-size:12px; letter-spacing:.08em; cursor:pointer; }
  .act.go { background:var(--hot); color:#111; border-color:var(--hot); font-weight:700; }
  .act.on { background:var(--ink); color:#111; }
  #allplay { min-width:64px; }
  #allplay.playing { background:var(--hot); color:#111; border-color:var(--hot); font-weight:700; }
  .act:disabled { opacity:.4; cursor:default; }
  svg text { font:10px -apple-system, "Helvetica Neue", Arial, sans-serif; fill:var(--dim); }
  .pt { cursor:pointer; }
  h2 { font-size:12px; letter-spacing:.12em; color:var(--dim); margin:22px 0 4px; font-weight:600; }
  .ab { display:inline-flex; gap:3px; margin-left:auto; }
  .ab span { border:1px solid #3a3a3a; border-radius:4px; padding:0 5px; font-size:10px; color:var(--dim); cursor:pointer; }
  .ab span:hover { color:var(--ink); border-color:var(--ink); }
  .note { color:var(--dim); font-size:12px; padding:8px 0 0; max-width:900px; }
</style></head><body>
<header>
  <h1>SF-000 ANALYSIS</h1>
  <div class="tabs"><button class="tab on" data-v="snaps">SNAPSHOTS</button><button class="tab" data-v="parts">PARTS</button><button class="tab" data-v="morph">MORPH</button></div>
  <input id="q" placeholder="find by name" autocomplete="off">
  <button class="act" id="allplay" disabled title="stops what is playing, and starts it again (the space bar does the same)">PLAY</button>
  <span class="dim" id="now"></span>
  <span class="dim" id="sum"></span>
</header>
<div class="lanebar" id="lanebar" hidden></div>
<main id="list"></main>
<div id="combo" hidden></div>
<script>
const D = /*DATA*/null;
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ORDER = ['bass', 'chord', 'pad', 'lead', 'rumble', 'voice', 'kick', 'closed_hat', 'open_hat', 'clap', 'perc'];
const LN = { kick:'KICK', closed_hat:'HAT', open_hat:'OPEN HAT', clap:'CLAP', perc:'PERC', bass:'BASS', rumble:'RUMBLE', lead:'LEAD', chord:'CHORD', pad:'PAD', voice:'VOICE' };

// ---------------------------------------------------------------- playing: parts together, in time
let ctx = null, playing = null, playingKey = null;
let last = null;                     // what played last, so that the one key at the top starts it again: { key, layers, tempo }
const buffers = new Map();
async function buf(url) {
  if (!buffers.has(url)) buffers.set(url, fetch(url).then(r => { if (!r.ok) throw new Error(url); return r.arrayBuffer(); }).then(a => ctx.decodeAudioData(a)));
  return buffers.get(url);
}
const partUrl = (uid, key) => `parts/${D.safe[uid]}/${key}.ogg`;
const hasPart = (uid, key) => (D.have[uid] || []).includes(key);
function stop() {
  if (playing) playing.forEach(s => { try { s.stop(); } catch (e) {} });
  playing = null; playingKey = null;
  document.querySelectorAll('.playing').forEach(c => c.classList.remove('playing'));
  showPlay();
}
// One key at the top for all of it, whatever is on the page: a chip that is playing may be out of sight (found by
// name, another tab), and it must still be seen to be playing and be stoppable. Lit, it says STOP and what is playing;
// pressed, that stops; pressed again, the same starts again.
function nameOf(key) {
  const [kind, ...r] = String(key || '').split(':');
  if (kind === 'clip' || kind === 'whole') return D.names[r.join(':')] || '';
  if (kind === 'solo') { const k = r.pop(); return `${D.names[r.join(':')] || ''}: ${LN[k] || k} alone`; }
  if (kind === 'combo') return combo && key === comboKey() ? `${D.names[combo.base] || ''}, the combination` : 'a combination';
  if (kind === 'part') return D.parts[r.join(':')] ? partName(r.join(':')) : 'a part';
  if (kind === 'pair') return 'two parts together';
  if (kind === 'point') { const p = M.scan && M.scan.id === r[0] ? (M.scan.points || []).find(x => x.key === r[1]) : null; return p ? `the morph at ${Math.round(p.f * 100)}%` : 'a point of the morph'; }
  return '';
}
function showPlay() {
  const b = document.getElementById('allplay'), n = document.getElementById('now');
  if (!b) return;
  b.textContent = playingKey ? 'STOP' : 'PLAY';
  b.classList.toggle('playing', !!playingKey);
  b.disabled = !playingKey && !last;
  n.textContent = playingKey ? nameOf(playingKey) : (last ? nameOf(last.key) : '');
  const pl = document.getElementById('cplay');       // (the combination's own key says the same thing the same way)
  if (pl) { const on = !!(combo && playingKey === comboKey()); pl.textContent = on ? 'STOP' : 'PLAY'; pl.classList.toggle('playing', on); }
}
function playStop() {
  if (playingKey) return stop();
  if (last) return playLayers(last.key, document.getElementById('allplay'), last.layers, last.tempo);
}
// A part at another tempo is stretched to this one without its pitch moving (as the instrument would play its notes):
// grains of 2048 samples laid every 512, each taken from where it best continues the one before.
const stretched = new Map();
function stretch(b, ratio) {
  if (Math.abs(ratio - 1) < 0.002) return b;
  const N = 2048, H = 512, W = 256, outLen = Math.round(b.length / ratio);
  const out = ctx.createBuffer(b.numberOfChannels, outLen, b.sampleRate);
  const win = new Float32Array(N); for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N);
  const ins = [...Array(b.numberOfChannels)].map((_, c) => b.getChannelData(c));
  const outs = [...Array(b.numberOfChannels)].map((_, c) => out.getChannelData(c));
  const mono = ins.length > 1 ? ins[0].map((v, i) => 0.5 * (v + ins[1][i])) : ins[0];
  const norm = new Float32Array(outLen);
  let prev = 0;
  for (let o = 0; o < outLen; o += H) {
    const ideal = Math.round(o * ratio);
    let best = ideal;
    if (o > 0) {
      // the grain that continues best what was laid: compared with what follows the last grain where it was taken from
      const want = prev + H;
      let top = -Infinity;
      for (let d = -W; d <= W; d += 4) {
        const at = ideal + d; if (at < 0 || at + N >= b.length) continue;
        let sum = 0; for (let i = 0; i < 512; i += 2) { const a = want + i; if (a < b.length) sum += mono[at + i] * mono[a]; }
        if (sum > top) { top = sum; best = at; }
      }
    }
    best = Math.max(0, Math.min(b.length - N, best));
    for (let c = 0; c < ins.length; c++) for (let i = 0; i < N && o + i < outLen; i++) outs[c][o + i] += ins[c][(best + i) % b.length] * win[i];
    for (let i = 0; i < N && o + i < outLen; i++) norm[o + i] += win[i];
    prev = best;
  }
  for (let c = 0; c < outs.length; c++) for (let i = 0; i < outLen; i++) if (norm[i] > 1e-3) outs[c][i] /= norm[i];
  return out;
}
// layers: [{url, bpm}], played at the tempo given (each starts on a bar line, so their bars line up)
async function playLayers(key, el, layers, tempo) {
  if (playingKey === key) { stop(); return; }
  stop();
  if (!layers.length) return;
  ctx = ctx || new (window.AudioContext || window.webkitAudioContext)();
  if (ctx.state === 'suspended') await ctx.resume();
  playingKey = key; last = { key, layers, tempo }; el.classList.add('playing'); markPlaying();
  let bs;
  try { bs = await Promise.all(layers.map(l => buf(l.url))); } catch (e) { stop(); return; }
  if (playingKey !== key) return;
  const at = ctx.currentTime + 0.08;
  playing = bs.map((b, i) => {
    const s = ctx.createBufferSource(); s.loop = true;
    const ratio = layers[i].bpm && tempo ? tempo / layers[i].bpm : 1;
    const key = layers[i].url + '@' + ratio.toFixed(4);
    if (!stretched.has(key)) stretched.set(key, stretch(b, ratio));
    s.buffer = stretched.get(key);
    s.connect(ctx.destination); s.start(at); return s;
  });
}
function markPlaying() {
  // What is lit is what sounds: every chip that is the thing playing, wherever on the page it is, through any redraw;
  // and nothing else. (A snapshot playing is lit in each place it is listed. The combination playing: each lane in it
  // from another snapshot is lit where it was pressed.)
  document.querySelectorAll('.chip.playing').forEach(c => c.classList.remove('playing'));
  showPlay();
  const key = playingKey || '', [kind, ...r] = key.split(':'), rest = r.join(':');
  const light = (sel, ok) => document.querySelectorAll(sel).forEach(c => { if (!ok || ok(c)) c.classList.add('playing'); });
  if (kind === 'clip' || kind === 'whole') light('.chip[data-act="clip"], .chip[data-act="choose"]', c => c.dataset.u === rest);
  else if (kind === 'solo') { const k = r[r.length - 1], u = r.slice(0, -1).join(':'); light('.chip[data-act="solo"]', c => c.dataset.u === u && c.dataset.k === k); }
  else if (kind === 'part') light('.chip[data-act="part"]', c => c.dataset.p === rest);
  else if (kind === 'pair') light('.chip[data-act="pair"]', c => `${c.dataset.p}:${c.dataset.q}` === rest);
  else if (kind === 'point') light('.chip[data-act="point"]', c => M.scan && r[0] === M.scan.id && c.dataset.key === r[1]);
  else if (kind === 'combo' && combo && key === comboKey()) {
    for (const [l, u] of Object.entries(combo.picks))
      if (u && !combo.off[l]) light(`.chip[data-base="${CSS.escape(combo.base)}"][data-k="${l}"]`, c => c.dataset.u === u);
  }
}
const playSnap = (u, el) => D.clips[u] ? playLayers('clip:' + u, el, [{ url: D.clips[u] }], 0) : playLayers('whole:' + u, el, wholeLayers(u), D.bpm[u]);
function wholeLayers(uid, without) {
  // a snapshot as its parts: every lane, return and the bus it has, less the lanes named
  return (D.have[uid] || []).filter(k => !(without || []).includes(k)).map(k => ({ url: partUrl(uid, k), bpm: D.bpm[uid] }));
}

// ---------------------------------------------------------------- chips
function chip(uid, note, opts = {}) {
  const name = D.names[uid] || uid.slice(-6);
  return `<button class="chip${opts.big ? ' big' : ''}${opts.cls ? ' ' + opts.cls : ''}" data-act="${esc(opts.act || 'clip')}" data-u="${esc(uid)}" ${opts.data || ''}>${esc(opts.label || name)}${note !== undefined && note !== '' ? `<small>${esc(note)}</small>` : ''}${opts.tag ? `<small class="tag${opts.tag === 'CHOSEN' ? ' chosen' : ''}">${esc(opts.tag)}</small>` : ''}</button>`;
}
function rel(label, items, fmt, sub) {
  items = (items || []).filter(x => D.names[x[0]]);
  if (!items.length) return '';
  return `<b class="${sub ? 'sub' : ''}">${label}</b><div class="chips">${items.map(fmt).join('')}</div>`;
}
const pct = x => `${x > 0 ? '+' : ''}${Math.round(x * 100)}`;
const partName = pid => { const p = D.parts[pid]; if (!p) return pid; const more = p.in.length > 1 ? ` +${p.in.length - 1}` : ''; return `${LN[p.lane] || p.lane} of ${D.names[p.in[0]] || '?'}${more}`; };

// ---------------------------------------------------------------- rhythm, in words
const low = s => String(s || '').toLowerCase();
const stepsOf = n => n === 16 ? '' : ` (${n} steps)`;
function heardWords(h) {
  if (!h) return '';
  const w = [`${h.hits} a bar`];
  if (h.added) w.push(`${h.added} added by the sound`);
  if (h.ghosts) w.push(`${h.ghosts} ghost${h.ghosts > 1 ? 's' : ''}`);
  if (h.triplets) w.push(`${h.triplets} on triplets`);
  if (h.off) w.push(`${h.off} off the grid`);
  if (h.period && h.period !== 4 && h.period !== 8 && h.period !== 2 && h.period !== 1) w.push(`repeats every ${h.period} steps`);
  return w.join(' · ');
}
function rhythmRows(r) {
  if (!r) return '';
  let h = '';
  const txt = s => `<div class="none" style="color:var(--ink)">${s}</div>`;
  if (r.heard) h += `<b>RHYTHM</b>${txt('heard: ' + esc(heardWords(r.heard)) + (r.heard.kind ? ` · ${esc(low(r.heard.kind))}` : ''))}`;
  const lanes = ORDER.filter(l => r.lanes && r.lanes[l] && r.lanes[l].kind);
  if (lanes.length) h += `<b class="sub">WRITTEN</b>${txt(lanes.map(l => `${LN[l]} ${esc(low(r.lanes[l].kind))}${stepsOf(r.lanes[l].len)}`).join(' · '))}`;
  const made = ORDER.filter(l => r.lanes && r.lanes[l] && r.lanes[l].heard && (r.lanes[l].heard.added || r.lanes[l].heard.ghosts || r.lanes[l].heard.triplets));
  if (made.length) h += `<b class="sub">BY THE SOUND</b>${txt(made.map(l => { const x = r.lanes[l].heard; return `${LN[l]} ${[x.added ? '+' + x.added : '', x.ghosts ? x.ghosts + ' ghost' + (x.ghosts > 1 ? 's' : '') : '', x.triplets ? x.triplets + ' triplet' : ''].filter(Boolean).join(', ')}`; }).join(' · '))}`;
  if (r.against) h += `<b class="sub">AGAINST</b>${txt(`${esc(r.against.ratio)}: ` + r.against.lengths.map(n => `${(r.against.by[n] || []).map(l => LN[l]).join(', ')} in ${n}`).join(' over ') + `, meeting every ${r.against.bars} bars`)}`;
  if (r.dub && r.dub.hits) h += `<b class="sub">DUB</b>${txt(`${r.dub.hits} echoes a bar` + (r.dub.period ? `, every ${r.dub.period} steps` : '') + (r.dub.triplets ? `, ${r.dub.triplets} on triplets` : ''))}`;
  return h;
}

// ---------------------------------------------------------------- SNAPSHOTS
function drawSnaps(q) {
  const all = Object.entries(D.found).filter(([u, f]) => f.energy !== undefined).sort((a, b) => b[1].energy - a[1].energy);
  const shown = q ? all.filter(([u]) => (D.names[u] || '').toLowerCase().includes(q)) : all;
  return `<p class="note">The big chip plays the snapshot. SAME TEXTURE: not alike overall, but the same to the touch (grit, resonance, noise, how alive). SAME RHYTHM: the same rhythm as it is heard, in another sound. RHYTHM: what is heard in the bar, and how much of it the sound itself adds to the steps (echoes, density, ghosts). SWAP: for each of its lanes, the same lane from another snapshot that would fit the rest of it. ADD: a lane it has not got, from another snapshot, that would go over all of it. The number: how well it fits, of 100. Pressed, each goes into the combination at the foot of the page and it plays: press more to build it up (one of each lane). A chip is lit while its lane is sounding in what plays, and only then; press a lit one and it comes out. In the bar: a lane pressed is silenced (again, back); OWN beside a lane from another snapshot puts this snapshot's own back. MAKE CHIP makes it a snapshot. The key at the top of the page stops whatever is playing and starts it again (so does the space bar), and says what it is. A lane's name here plays this snapshot's own lane alone.</p>` +
  shown.map(([u, f]) => {
    const swaps = f.swap ? ORDER.filter(l => f.swap[l] && f.swap[l].length).map(l =>
      `<b class="sub"><span class="chip" data-act="solo" data-u="${esc(u)}" data-k="${l}" style="padding:2px 6px;font-size:11px">${LN[l]}</span></b><div class="chips">${
        f.swap[l].slice(0, 3).map(([pid, from, s]) => chip(from, Math.round(s * 100), { act: 'swap', data: `data-base="${esc(u)}" data-k="${l}"` })).join('')}</div>`).join('') : '';
    const adds = f.add ? ORDER.filter(l => f.add[l] && f.add[l].length).map(l =>
      `<b class="sub">${LN[l]}</b><div class="chips">${
        f.add[l].map(([pid, from, s]) => chip(from, Math.round(s * 100), { act: 'add', data: `data-base="${esc(u)}" data-k="${l}"` })).join('')}</div>`).join('') : '';
    return `<div class="row">
      <div class="me">${chip(u, undefined, { big: true })}
        <div class="bar"><i style="width:${Math.round(f.energy * 100)}%"></i></div>
        <div class="ab"><span data-end="a" data-u="${esc(u)}" title="morph from this">MORPH FROM</span><span data-end="b" data-u="${esc(u)}" title="morph to this">TO</span></div>
        <div class="num dim">energy ${Math.round(f.energy * 100)} · ${esc(f.measured.loudness)} LUFS · ${esc(f.measured.onsets)} hits/beat${f.measured.sounding != null ? ` · ${f.measured.sounding} lanes` : ''} · ${D.bpm[u] ? Math.round(D.bpm[u]) + ' BPM · ' : ''}from ${esc(f.measured.from)}</div>
      </div>
      <div class="rel">
        ${rel('ALIKE', f.alike, ([v, d]) => chip(v, d.toFixed(2)))}
        ${rel('SAME TEXTURE', f.texture, ([v, d]) => chip(v, d.toFixed(2)))}
        ${rel('SAME RHYTHM', f.rhythm_alike, ([v, d]) => chip(v, d.toFixed(2)))}
        ${rhythmRows(f.rhythm)}
        ${rel('UP', f.up, ([v, d, de]) => chip(v, pct(de)))}
        ${rel('DOWN', f.down, ([v, d, de]) => chip(v, pct(de)))}
        ${rel('FOLLOWED BY', f.follows, ([v, n]) => chip(v, `×${n}`))}
        ${f.grew_from && D.names[f.grew_from] ? `<b>TAKEN FROM</b><div class="chips">${chip(f.grew_from)}</div>` : ''}
        ${swaps ? `<b>SWAP</b><div></div>${swaps}` : ''}
        ${adds ? `<b>ADD</b><div></div>${adds}` : ''}
      </div>
    </div>`;
  }).join('') + (D.unmeasured.length ? `<p class="dim">Not measured (no sound here for them): ${D.unmeasured.map(esc).join(', ')}</p>` : '');
}

// ---------------------------------------------------------------- PARTS
let lane = 'bass';
function drawParts(q) {
  const ps = Object.entries(D.parts).filter(([pid, p]) => p.lane === lane)
    .filter(([pid, p]) => !q || p.in.some(u => (D.names[u] || '').toLowerCase().includes(q)))
    .sort((a, b) => (b[1].level ?? -99) - (a[1].level ?? -99));
  if (!ps.length) return `<p class="dim">No ${LN[lane]} here that sounds.</p>`;
  return `<p class="note">Every ${LN[lane]} in the library, once each (one in several snapshots says +N). Pressed, it plays alone. GOES WITH: the parts of other lanes that would fit it best; pressed, the two play together at this one's tempo (the number: how well, of 100). TOGETHER: already in a snapshot together; the rest have never been. CHOSEN: you put them together yourself.</p>` +
  ps.map(([pid, p]) => {
    const u = p.in[0];
    const goes = ORDER.filter(l => p.goes[l] && p.goes[l].length).map(l => {
      const items = p.goes[l].filter(([q]) => D.parts[q]);
      if (!items.length) return '';
      return `<b class="sub">${LN[l]}</b><div class="chips">${items.map(([q, s, t, c]) => {
        const qu = D.parts[q].in[0];
        return chip(qu, Math.round(s * 100), { act: 'pair', label: D.names[qu], tag: c ? 'CHOSEN' : (t ? 'TOGETHER' : ''), data: `data-p="${esc(pid)}" data-q="${esc(q)}"` });
      }).join('')}</div>`;
    }).join('');
    return `<div class="row">
      <div class="me">${chip(u, undefined, { big: true, act: 'part', label: partName(pid), data: `data-p="${esc(pid)}"` })}
        <div class="num dim">${p.notes ? esc(p.notes) + ' · ' : ''}sounds in ${Math.round((p.held || 0) * 100)}% of the bar · ${esc(p.level)} dB</div>
        ${p.rhythm ? `<div class="num" style="color:var(--ink)">${p.rhythm.kind ? esc(low(p.rhythm.kind)) + stepsOf(p.rhythm.len) : 'played from outside'}${p.rhythm.heard ? ' · heard ' + esc(heardWords(p.rhythm.heard)) : ''}</div>` : ''}
        <div class="num dim">in ${p.in.map(v => esc(D.names[v] || '?')).slice(0, 6).join(', ')}${p.in.length > 6 ? '…' : ''}</div>
      </div>
      <div class="rel">
        ${rel('ALIKE', (p.alike || []).filter(([q]) => D.parts[q]).map(([q, d]) => [D.parts[q].in[0], d, q]), ([v, d, q]) => chip(v, d.toFixed(2), { act: 'part', label: partName(q), data: `data-p="${esc(q)}"` }))}
        ${goes ? `<b>GOES WITH</b><div></div>${goes}` : ''}
      </div>
    </div>`;
  }).join('');
}

// ---------------------------------------------------------------- the combination: a snapshot, with lanes from others
// Each SWAP, ADD or GOES WITH pressed puts its lane into it (pressed again, takes it out) and plays all of it. A lane
// in the bar pressed is taken away (again, put back). MAKE CHIP makes it a new snapshot in the web library's MADE stack.
let combo = null;                    // { base, picks: { lane: uid }, off: { lane: true } }
let said = '';
function comboLayers() {
  const taken = [...new Set([...Object.keys(combo.picks), ...Object.keys(combo.off)])];
  const layers = wholeLayers(combo.base, taken);
  for (const [l, from] of Object.entries(combo.picks)) if (from && !combo.off[l] && hasPart(from, l)) layers.push({ url: partUrl(from, l), bpm: D.bpm[from] });
  return layers;
}
function comboKey() { return 'combo:' + combo.base + ':' + JSON.stringify(combo.picks) + JSON.stringify(Object.keys(combo.off).sort()); }
function playCombo() { if (!combo) return; playLayers(comboKey(), document.getElementById('cplay'), comboLayers(), D.bpm[combo.base]); }
function pick(base, l, from) {
  if (!combo || combo.base !== base) combo = { base, picks: {}, off: {} };
  if (combo.picks[l] === from) delete combo.picks[l]; else combo.picks[l] = from;
  delete combo.off[l];
  said = ''; drawCombo();
}
function drawCombo() {
  const el = document.getElementById('combo');
  if (!combo) { el.hidden = true; return; }
  el.hidden = false;
  const own = (D.have[combo.base] || []).filter(k => ORDER.includes(k));
  const lanes = ORDER.filter(l => own.includes(l) || combo.picks[l]);
  const named = Object.values(combo.picks).filter(Boolean).map(u => D.names[u]);
  // each lane: pressed, silenced (again, back). One from another snapshot has a key beside it: OWN puts this
  // snapshot's own lane back; OUT takes away a lane this snapshot never had.
  el.innerHTML = `<b style="font-size:12px;letter-spacing:.1em">${esc(D.names[combo.base])}</b>` +
    lanes.map(l => { const f = combo.picks[l], off = !!combo.off[l], mine = own.includes(l) && !(((D.found[combo.base] || {}).add || {})[l]);      // (a lane it has, and sounds)
      return `<span class="lgrp"><span class="lane${off ? ' off' : ''}${f ? ' from' : ''}" data-l="${l}" title="${off ? 'silenced: press to bring it back' : 'press to silence it'}">${LN[l]}${f ? `<small>${esc(D.names[f])}</small>` : ''}</span>${
        f ? `<span class="back" data-back="${l}" title="${mine ? `back to ${esc(D.names[combo.base])}'s own ${LN[l]}` : `take this ${LN[l]} out`}">${mine ? 'OWN' : 'OUT'}</span>` : ''}</span>`; }).join('') +
    `<button class="act" id="cplay">PLAY</button>` +
    `<input id="cname" placeholder="name" value="${esc(combo.name ?? ((D.names[combo.base] || '').slice(0, 10) + (named.length ? ' + ' + named[0].replace(/^SNAP /, '') : '')))}" maxlength="24">` +
    `<button class="act go" id="cmake">MAKE CHIP</button><button class="act" id="cclear">CLEAR</button><span class="said">${esc(said)}</span>`;
  markPlaying();
}
document.getElementById('combo').oninput = e => { if (e.target.id === 'cname') combo.name = e.target.value; };
document.getElementById('combo').onclick = async e => {
  const t = e.target.closest('.lane, .back, .act'); if (!t) return;
  if (t.classList.contains('back')) { const l = t.dataset.back; delete combo.picks[l]; delete combo.off[l]; said = ''; drawCombo(); return playCombo(); }
  if (t.classList.contains('lane')) {
    const l = t.dataset.l;
    if (combo.off[l]) delete combo.off[l]; else combo.off[l] = true;
    said = ''; drawCombo(); return playCombo();
  }
  if (t.id === 'cplay') return playCombo();
  if (t.id === 'cclear') { combo = null; stop(); drawCombo(); markPlaying(); return; }
  if (t.id === 'cmake') {
    if (!Object.keys(combo.picks).length && !Object.keys(combo.off).length) { said = 'Nothing changed in it yet.'; return drawCombo(); }
    const name = document.getElementById('cname').value;
    said = 'Making it...'; drawCombo();
    try {
      const r = await fetch('/make', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ base: combo.base, picks: { ...combo.picks, ...Object.fromEntries(Object.keys(combo.off).map(l => [l, null])) }, name }) });
      const a = await r.json();
      said = a.ok ? `${a.name}: made, in the web library's MADE stack. WEB LOAD in SF-000 brings it.` : (a.error || 'It was not made.');
    } catch (err) { said = 'It was not made: open this page from the analysis window (http://localhost:8765/report.html).'; }
    drawCombo();
  }
};

// ---------------------------------------------------------------- MORPH: the way between two snapshots
// SF-000 itself renders it, as a stack knob holding the two would play it. The scan is kept beside the page
// (morph/<id>/scan.json) and read from there while it is made.
let M = { a: '', b: '', notes: 'a', scan: null, sel: null, lead: [], count: 3, said: '', shape: null, timer: null };
try { Object.assign(M, JSON.parse(sessionStorage.getItem('sf000morph') || '{}'), { timer: null }); } catch (e) {}
const keepM = () => { try { sessionStorage.setItem('sf000morph', JSON.stringify({ ...M, timer: null })); } catch (e) {} };
async function post(path, body) {
  try { const r = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }); return await r.json(); }
  catch (e) { return { error: 'Open this page from the analysis window (http://localhost:8765/report.html).' }; }
}
async function readScan() {
  if (!M.scan) return;
  try { const r = await fetch(`morph/${M.scan.id}/scan.json?${Date.now()}`); if (r.ok) M.scan = await r.json(); } catch (e) {}
  const busy = M.scan.status === 'rendering' || M.scan.status === 'measuring' || Object.values(M.scan.shapes || {}).some(s => s.status === 'rendering');
  clearTimeout(M.timer);
  if (busy) M.timer = setTimeout(readScan, 1500);
  keepM();
  if (view === 'morph') draw();
}
const TAGS = { NEW: 'neither end sounds like this', FITS: 'its parts sit together best here', EDGE: 'the sound changes most just before here', HOLDS: 'the sound holds still around here', LIFTS: 'more energy than either end', DROPS: 'less energy than either end' };
function strip(sc) {
  // the turn of the knob left to right; the bars: how much the sound changes in each tenth; the line: energy; a ring: worth hearing
  const W = 1000, H = 170, x = f => 30 + f * (W - 60), top = Math.max(...sc.steps, 1e-6);
  const bars = sc.steps.map((s, i) => `<rect x="${x(sc.points[i].f) + 2}" y="${120 - 70 * s / top}" width="${x(sc.points[i + 1].f) - x(sc.points[i].f) - 4}" height="${70 * s / top}" fill="#2f2f2f"/>`).join('');
  const line = sc.points.map((p, i) => `${i ? 'L' : 'M'}${x(p.f)},${120 - 70 * p.energy}`).join(' ');
  const pts = sc.points.map(p => {
    const hot = p.tags.length > 0, sel = M.sel === p.key;
    return `<g class="pt" data-key="${p.key}"><circle cx="${x(p.f)}" cy="140" r="${hot ? 9 : 6}" fill="${sel ? '#f2c230' : '#1f1f1f'}" stroke="${hot ? '#f2c230' : '#666'}" stroke-width="${hot ? 2 : 1}"/>` +
      `<text x="${x(p.f)}" y="162" text-anchor="middle">${Math.round(p.f * 100)}</text>` +
      p.tags.map((t, j) => `<text x="${x(p.f)}" y="${40 - j * 11}" text-anchor="middle" style="fill:#f2c230">${esc(t)}</text>`).join('') + `</g>`;
  }).join('');
  return `<svg viewBox="0 0 ${W} ${H}" style="width:100%;max-width:1100px;display:block">${bars}<path d="${line}" fill="none" stroke="#e9e6df" stroke-width="1.5"/>${pts}` +
    `<text x="30" y="12">${esc(D.names[sc.a])}</text><text x="${W - 30}" y="12" text-anchor="end">${esc(D.names[sc.b])}${sc.notes === 'a' ? "'s sound" : ''}</text></svg>`;
}
function shapeView(sc, sh) {
  if (!sh) return '';
  if (sh.status === 'rendering') return `<p class="dim">SF-000 is rendering it...</p>`;
  if (sh.status === 'failed') return `<p class="dim">${esc(sh.error || 'It could not be rendered.')}</p>`;
  const top = Math.max(...sh.steps, 1e-6);
  const chips = [`<button class="chip" data-act="clip" data-u="${esc(sc.a)}">${esc(D.names[sc.a])}</button>`]
    .concat(sh.chips.map((c, i) => `<i style="display:inline-block;width:${10 + 50 * sh.steps[i] / top}px;height:2px;background:#666;vertical-align:middle"></i><button class="chip" data-act="point" data-key="${c.key}">${sh.kind === 'lead' ? esc(sh.lanes.map(l => LN[l]).join(' + ')) + ' FIRST' : Math.round(c.how.f * 100) + '%'}</button>`))
    .concat([`<i style="display:inline-block;width:${10 + 50 * sh.steps[sh.steps.length - 1] / top}px;height:2px;background:#666;vertical-align:middle"></i><button class="chip" data-act="clip" data-u="${esc(sc.b)}">${esc(D.names[sc.b])}</button>`]).join('');
  const was = sh.kind === 'even' ? ` Turned evenly through these, the largest step is ${sh.uneven}× the mean (the plain morph: ${sh.plain_uneven}×).${sh.uneven >= sh.plain_uneven - 0.05 ? ' That is no more even than it was: this morph was even already, and there is nothing to gain by it.' : ''}` : ` The first half of the turn changes ${esc(sh.lanes.map(l => LN[l]).join(', '))} alone; the second half, the rest.`;
  return `<div class="mrow">${chips}</div><p class="note">The gaps: how much the sound changes between each two.${was}</p>
    <div class="mrow"><input id="sname" placeholder="name of the stack" maxlength="20" value="${esc(M.sname ?? ((D.names[sc.a] || '').replace(/^SNAP /, '') + ' > ' + (D.names[sc.b] || '').replace(/^SNAP /, '')))}"><button class="act go" id="smake">MAKE STACK</button><span class="dim">${esc(M.ssaid || '')}</span></div>`;
}
function drawMorph(q) {
  const sc = M.scan && M.scan.a === M.a && M.scan.b === M.b && M.scan.notes === M.notes ? M.scan : null;
  const there = end => M[end] && D.found[M[end]] && D.found[M[end]].energy !== undefined;
  if (M.arm === undefined) M.arm = !there('a') ? 'a' : !there('b') ? 'b' : null;
  const slot = (end, word) => `<b>${word}</b>${there(end) ? chip(M[end], Math.round(D.found[M[end]].energy * 100), { big: true }) : '<span class="dim">none yet</span>'}<button class="act${M.arm === end ? ' on' : ''}" data-arm="${end}">${M.arm === end ? 'CHOOSING' : 'CHOOSE'}</button>`;
  let h = `<p class="note">The way between two snapshots, as a stack knob holding both plays it: the sound goes from one to the other, with the notes of the one it set out from. SF-000 itself renders it (close SF-000 first), eleven points along the turn, about a minute. Then: what is worth hearing between them, and the morph reshaped into a stack. The two chips here play the two ends.</p>
    <div class="mrow">${slot('a', 'FROM')}<span style="width:14px"></span>${slot('b', 'TO')}</div>`;
  if (M.arm) {
    const other = M.arm === 'a' ? M.b : M.a;
    const all = Object.entries(D.found).filter(([u, f]) => f.energy !== undefined && u !== other).sort((x, y) => y[1].energy - x[1].energy);
    const shown = q ? all.filter(([u]) => (D.names[u] || '').toLowerCase().includes(q)) : all;
    h += `<p class="note">Press one and it plays, and it is the ${M.arm === 'a' ? 'FROM' : 'TO'}: press through them until it is the one you want. Pressed again, it stops. Most energy first; the number is its energy, of 100.${q ? '' : ' The box at the top finds by name.'}</p>
      <div class="chips" style="padding:6px 0 4px">${shown.map(([u, f]) => chip(u, Math.round(f.energy * 100), { act: 'choose', cls: u === M[M.arm] ? 'picked' : '' })).join('') || '<span class="dim">None by that name.</span>'}</div>`;
  }
  h += `<div class="mrow"><span class="dim">notes of</span><button class="act${M.notes === 'a' ? ' on' : ''}" data-notes="a">FROM</button><button class="act${M.notes === 'b' ? ' on' : ''}" data-notes="b">TO</button>
      <button class="act go" id="mscan"${there('a') && there('b') && M.a !== M.b ? '' : ' disabled'}>SCAN</button><span class="dim">${esc(M.said)}</span></div>`;
  if (!sc) return h;
  if (sc.status === 'rendering' || sc.status === 'measuring') return h + `<p class="dim">${sc.status === 'measuring' ? 'Measuring...' : `SF-000 is rendering: ${sc.done} of ${sc.of}.`}</p>`;
  if (sc.status === 'failed') return h + `<p class="dim">${esc(sc.error || 'It could not be scanned.')}</p>`;
  const nEnd = sc.notes === 'b' ? 1 : 0, kk = sc.keys;
  if (kk) h += `<p class="note">Every point plays the notes of ${sc.notes === 'b' ? 'TO' : 'FROM'}, in its key (${esc(kk[nEnd])}${kk[0] !== kk[1] ? `; ${sc.notes === 'b' ? 'FROM' : 'TO'} is in ${esc(kk[1 - nEnd])}, so the far end is not that snapshot as you know it` : ''}). Everything else goes over as the knob takes it, TUNE with it: a lane tuned differently in the two bends from one to the other.</p>`;
  if ((sc.held || []).length) h += `<p class="note" style="color:var(--hot)">This was scanned with TUNE held still on ${sc.held.map(l => LN[l]).join(', ')}. That is undone: SCAN again and TUNE bends across the turn, as the stack knob plays it.</p>`;
  if ((sc.missing || []).length) {
    const fname = r => esc(String(r).split('/').pop()), files = sc.missing.map(r => fname(r) + (sc.why && sc.why[r] ? ` (${esc(sc.why[r])})` : '')).join(', ');
    h += `<p class="note" style="color:var(--hot)">` + (sc.left === undefined
      ? `This was scanned without ${sc.missing.length} sound file(s) these snapshots name (${files}). A lane without its file does not fall silent: the voice lane sings its notes with a choir of its own, the same in every morph. SCAN again: the files are now taken from SF-000's own folder on this machine.`
      : `Not the whole of these snapshots: ${sc.missing.length} sound file(s) could not be had (${files}).` +
        (sc.left.length ? ` ${sc.left.map(l => LN[l]).join(', ')} is left out of every point, since without its file it would play something that is in neither snapshot.` : '') +
        ((sc.named || []).length ? ` ${sc.named.map(l => LN[l]).join(', ')} names one too: if it plays that file, it is not as you know it here.` : '')) + `</p>`;
  }
  const found = sc.points.filter(p => p.tags.length);
  const sel = sc.points.find(p => p.key === M.sel);
  h += strip(sc) + `<p class="note">Left to right: the turn of the knob. Press a point to hear it. The bars: how much the sound changes in each tenth of the turn (an even morph would have them level; here the largest is ${sc.uneven}× the mean). The line: energy. Ringed: worth hearing.</p>`;
  h += `<h2>WORTH HEARING</h2>` + (found.length ? `<div class="rel" style="grid-template-columns:60px 1fr">` + found.map(p =>
      `<b>${Math.round(p.f * 100)}%</b><div class="chips"><button class="chip" data-act="point" data-key="${p.key}">PLAY</button>${p.tags.map(t => `<span class="chip" style="cursor:default;border-color:transparent" title="${esc(TAGS[t] || 'more of this than either end has')}">${esc(t)}</span>`).join('')}</div>`).join('') + `</div>`
      : `<p class="dim">Nothing between them stands clear of both ends: it is a plain way from one to the other.</p>`);
  h += `<div class="mrow"><span class="dim">${sel ? `The point at ${Math.round(sel.f * 100)}%:` : 'Press a point, then:'}</span><input id="pname" placeholder="name" maxlength="24" value="${esc(M.pname ?? '')}"><button class="act go" id="pmake"${sel ? '' : ' disabled'}>MAKE CHIP</button><span class="dim">${esc(M.psaid || '')}</span></div>`;
  h += `<h2>RESHAPE IT INTO A STACK</h2>
    <div class="mrow"><b>EVEN</b><span class="dim">snapshots between, placed where the sound has changed by equal amounts:</span>
      ${[1, 2, 3, 5].map(n => `<button class="act${M.count === n ? ' on' : ''}" data-count="${n}">${n}</button>`).join('')}<button class="act" id="reven">RENDER</button></div>
    <div class="mrow"><b>LEAD WITH</b><span class="dim">these go first, the rest follow (most changed first):</span>
      ${(sc.lanes || []).map(([l, d]) => `<button class="act${M.lead.includes(l) ? ' on' : ''}" data-lead="${l}" title="how much it changes: ${d}">${LN[l]}</button>`).join('')}<button class="act" id="rlead"${M.lead.length ? '' : ' disabled'}>RENDER</button></div>`;
  h += shapeView(sc, (sc.shapes || {})[M.shape]);
  return h;
}
document.getElementById('list').addEventListener('input', e => {
  if (e.target.id === 'pname') M.pname = e.target.value; else if (e.target.id === 'sname') M.sname = e.target.value;
});
document.getElementById('list').addEventListener('click', async e => {
  const end = e.target.closest('.ab span');
  if (end) { M[end.dataset.end] = end.dataset.u; if (M.arm === end.dataset.end) M.arm = null; M.said = ''; keepM(); if (M.a && M.b) { view = 'morph'; document.querySelectorAll('.tab').forEach(x => x.classList.toggle('on', x.dataset.v === 'morph')); } draw(); return; }
  if (view !== 'morph') return;
  const t = e.target.closest('.act, .pt, .chip[data-act="point"], .chip[data-act="choose"]'); if (!t) return;
  const sc = M.scan;
  if (t.dataset.arm) { M.arm = M.arm === t.dataset.arm ? null : t.dataset.arm; keepM(); return draw(); }
  if (t.dataset.act === 'choose') {
    // heard and chosen in one press: the end being chosen is always the one last heard
    const u = t.dataset.u; if (!M.arm) return;
    M[M.arm] = u; M.said = ''; keepM(); draw();
    return playSnap(u, [...document.querySelectorAll('.chip[data-act="choose"]')].find(c => c.dataset.u === u) || document.body);
  }
  if (t.dataset.notes) { M.notes = t.dataset.notes; keepM(); return draw(); }
  if (t.dataset.count) { M.count = Number(t.dataset.count); keepM(); return draw(); }
  if (t.dataset.lead) { const l = t.dataset.lead; M.lead = M.lead.includes(l) ? M.lead.filter(x => x !== l) : [...M.lead, l]; keepM(); return draw(); }
  if (t.dataset.key && sc) {
    const key = t.dataset.key;
    if (sc.points.some(p => p.key === key)) { M.sel = key; M.psaid = ''; keepM(); draw(); }
    return playLayers('point:' + sc.id + ':' + key, document.querySelector(`.chip[data-key="${key}"]`) || document.body, [{ url: `morph/${sc.id}/${key}.ogg` }], 0);
  }
  if (t.id === 'mscan') {
    stop(); M.said = 'Asking SF-000...'; M.sel = null; M.shape = null; M.arm = null; draw();
    const a = await post('/scan', { a: M.a, b: M.b, notes: M.notes });
    if (a.error) { M.said = a.error; return draw(); }
    M.said = ''; M.scan = a; keepM(); return readScan();
  }
  if ((t.id === 'reven' || t.id === 'rlead') && sc) {
    const kind = t.id === 'reven' ? 'even' : 'lead';
    const a = await post('/reshape', { scan: sc.id, kind, count: M.count, lanes: M.lead });
    if (a.error) { M.said = a.error; return draw(); }
    M.shape = a.id; M.ssaid = ''; keepM(); return readScan();
  }
  if (t.id === 'pmake' && sc && M.sel) {
    const p = sc.points.find(x => x.key === M.sel);
    const name = document.getElementById('pname').value || `${(D.names[sc.a] || '').slice(0, 9)} ${Math.round(p.f * 100)}% ${(D.names[sc.b] || '').replace(/^SNAP /, '')}`;
    M.psaid = 'Making it...'; draw();
    const a = await post('/makepoint', { scan: sc.id, key: M.sel, name });
    M.psaid = a.ok ? `${a.name}: made, in the web library's MADE stack. WEB LOAD in SF-000 brings it.` : (a.error || 'It was not made.'); return draw();
  }
  if (t.id === 'smake' && sc && M.shape) {
    const name = document.getElementById('sname').value;
    M.ssaid = 'Making it...'; draw();
    const a = await post('/makestack', { scan: sc.id, shape: M.shape, name });
    M.ssaid = a.ok ? `${a.name}: a stack of ${a.chips} in the web library, marked to load. WEB LOAD in SF-000 brings it; put it on a stack knob.` : (a.error || 'It was not made.'); return draw();
  }
});

// ---------------------------------------------------------------- the page
let view = 'snaps';
function draw() {
  const q = document.getElementById('q').value.trim().toLowerCase();
  document.getElementById('lanebar').hidden = view !== 'parts';
  document.getElementById('list').innerHTML = view === 'snaps' ? drawSnaps(q) : view === 'parts' ? drawParts(q) : drawMorph(q);
  markPlaying();
}
function drawLanes() {
  const there = new Set(Object.values(D.parts).map(p => p.lane));
  document.getElementById('lanebar').innerHTML = ORDER.filter(l => there.has(l)).map(l => `<button class="lanebtn${l === lane ? ' on' : ''}" data-l="${l}">${LN[l]}</button>`).join('');
}
document.querySelector('.tabs').onclick = e => { const t = e.target.closest('.tab'); if (!t) return; view = t.dataset.v; document.querySelectorAll('.tab').forEach(x => x.classList.toggle('on', x === t)); draw(); };
document.getElementById('lanebar').onclick = e => { const b = e.target.closest('.lanebtn'); if (!b) return; lane = b.dataset.l; drawLanes(); draw(); };
document.getElementById('q').oninput = draw;
document.getElementById('allplay').onclick = playStop;
document.addEventListener('keydown', e => {
  // the space bar: the same key, unless something is being typed
  if (e.code !== 'Space' || e.metaKey || e.ctrlKey || e.altKey || /^(INPUT|TEXTAREA|SELECT)$/.test((e.target || {}).tagName || '')) return;
  e.preventDefault(); playStop();
});
document.getElementById('list').onclick = e => {
  const b = e.target.closest('.chip'); if (!b) return;
  const u = b.dataset.u, act = b.dataset.act;
  if (act === 'clip') return playSnap(u, b);
  if (act === 'solo') return playLayers(`solo:${u}:${b.dataset.k}`, b, [{ url: partUrl(u, b.dataset.k), bpm: D.bpm[u] }], D.bpm[u]);
  if (act === 'swap' || act === 'add') {
    const base = b.dataset.base, k = b.dataset.k;
    if (!hasPart(u, k)) return;
    // not in the combination: in, and it plays. In it and sounding (lit): out. In it and not sounding: it plays.
    const inIt = combo && combo.base === base && combo.picks[k] === u;
    if (inIt && (combo.off[k] || playingKey !== comboKey())) { delete combo.off[k]; drawCombo(); return playCombo(); }
    pick(base, k, u);
    return playCombo();
  }
  if (act === 'part') {
    const p = D.parts[b.dataset.p]; if (!p) return;
    const pu = p.in.find(x => hasPart(x, p.lane)); if (!pu) return;
    return playLayers('part:' + b.dataset.p, b, [{ url: partUrl(pu, p.lane), bpm: D.bpm[pu] }], D.bpm[pu]);
  }
  if (act === 'pair') {
    const p = D.parts[b.dataset.p], q = D.parts[b.dataset.q]; if (!p || !q) return;
    const pu = p.in.find(x => hasPart(x, p.lane)), qu = q.in.find(x => hasPart(x, q.lane)); if (!pu || !qu) return;
    // (into the combination: the first part's snapshot, with the other's lane in it)
    combo = { base: pu, picks: { [q.lane]: qu }, off: {} }; said = 'The two alone are playing; PLAY plays them in the whole of ' + D.names[pu] + '.'; drawCombo();
    return playLayers(`pair:${b.dataset.p}:${b.dataset.q}`, b, [{ url: partUrl(pu, p.lane), bpm: D.bpm[pu] }, { url: partUrl(qu, q.lane), bpm: D.bpm[qu] }], D.bpm[pu]);
  }
};
const c = D.counts;
document.getElementById('sum').textContent = `${c.measured} of ${c.snapshots} snapshots measured · ${c.parts || 0} parts · ${c.log_lines} lines of the log · ${D.made}`;
drawLanes(); draw();
if (M.scan) readScan();
</script></body></html>
"""
