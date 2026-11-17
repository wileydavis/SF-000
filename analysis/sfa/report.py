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
  .chip.playing { background:var(--hot); color:#111; border-color:var(--hot); }
  .chip.playing small { color:#333; }
  .big { font-size:14px; font-weight:600; padding:7px 10px; text-align:left; }
  .num { font-variant-numeric: tabular-nums; font-size:11px; }
  .note { color:var(--dim); font-size:12px; padding:8px 0 0; max-width:900px; }
</style></head><body>
<header>
  <h1>SF-000 ANALYSIS</h1>
  <div class="tabs"><button class="tab on" data-v="snaps">SNAPSHOTS</button><button class="tab" data-v="parts">PARTS</button></div>
  <input id="q" placeholder="find by name" autocomplete="off">
  <span class="dim" id="sum"></span>
</header>
<div class="lanebar" id="lanebar" hidden></div>
<main id="list"></main>
<script>
const D = /*DATA*/null;
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ORDER = ['bass', 'chord', 'pad', 'lead', 'rumble', 'voice', 'kick', 'closed_hat', 'open_hat', 'clap', 'perc'];
const LN = { kick:'KICK', closed_hat:'HAT', open_hat:'OPEN HAT', clap:'CLAP', perc:'PERC', bass:'BASS', rumble:'RUMBLE', lead:'LEAD', chord:'CHORD', pad:'PAD', voice:'VOICE' };

// ---------------------------------------------------------------- playing: parts together, in time
let ctx = null, playing = null, playingKey = null;
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
  document.querySelectorAll('.chip.playing').forEach(c => c.classList.remove('playing'));
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
  playingKey = key; el.classList.add('playing');
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

// ---------------------------------------------------------------- SNAPSHOTS
function drawSnaps(q) {
  const all = Object.entries(D.found).filter(([u, f]) => f.energy !== undefined).sort((a, b) => b[1].energy - a[1].energy);
  const shown = q ? all.filter(([u]) => (D.names[u] || '').toLowerCase().includes(q)) : all;
  return `<p class="note">The big chip plays the snapshot. SWAP: for each of its lanes, the same lane from another snapshot that would fit the rest of it; pressed, it plays with that lane taken from the other. ADD: a lane it has not got, from another snapshot, that would go over all of it. The number: how well it fits, of 100. A lane's name plays this snapshot's own lane alone.</p>` +
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
        <div class="num dim">energy ${Math.round(f.energy * 100)} · ${esc(f.measured.loudness)} LUFS · ${esc(f.measured.onsets)} hits/beat${f.measured.sounding != null ? ` · ${f.measured.sounding} lanes` : ''} · ${D.bpm[u] ? Math.round(D.bpm[u]) + ' BPM · ' : ''}from ${esc(f.measured.from)}</div>
      </div>
      <div class="rel">
        ${rel('ALIKE', f.alike, ([v, d]) => chip(v, d.toFixed(2)))}
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
        <div class="num dim">${p.notes ? esc(p.notes) + ' · ' : ''}${esc(p.hits)} hits a bar · sounds in ${Math.round((p.held || 0) * 100)}% of it · ${esc(p.level)} dB</div>
        <div class="num dim">in ${p.in.map(v => esc(D.names[v] || '?')).slice(0, 6).join(', ')}${p.in.length > 6 ? '…' : ''}</div>
      </div>
      <div class="rel">
        ${rel('ALIKE', (p.alike || []).filter(([q]) => D.parts[q]).map(([q, d]) => [D.parts[q].in[0], d, q]), ([v, d, q]) => chip(v, d.toFixed(2), { act: 'part', label: partName(q), data: `data-p="${esc(q)}"` }))}
        ${goes ? `<b>GOES WITH</b><div></div>${goes}` : ''}
      </div>
    </div>`;
  }).join('');
}

// ---------------------------------------------------------------- the page
let view = 'snaps';
function draw() {
  const q = document.getElementById('q').value.trim().toLowerCase();
  document.getElementById('lanebar').hidden = view !== 'parts';
  document.getElementById('list').innerHTML = view === 'snaps' ? drawSnaps(q) : drawParts(q);
}
function drawLanes() {
  const there = new Set(Object.values(D.parts).map(p => p.lane));
  document.getElementById('lanebar').innerHTML = ORDER.filter(l => there.has(l)).map(l => `<button class="lanebtn${l === lane ? ' on' : ''}" data-l="${l}">${LN[l]}</button>`).join('');
}
document.querySelector('.tabs').onclick = e => { const t = e.target.closest('.tab'); if (!t) return; view = t.dataset.v; document.querySelectorAll('.tab').forEach(x => x.classList.toggle('on', x === t)); draw(); };
document.getElementById('lanebar').onclick = e => { const b = e.target.closest('.lanebtn'); if (!b) return; lane = b.dataset.l; drawLanes(); draw(); };
document.getElementById('q').oninput = draw;
document.getElementById('list').onclick = e => {
  const b = e.target.closest('.chip'); if (!b) return;
  const u = b.dataset.u, act = b.dataset.act;
  if (act === 'clip') {
    if (D.clips[u]) return playLayers('clip:' + u, b, [{ url: D.clips[u] }], 0);
    return playLayers('whole:' + u, b, wholeLayers(u), D.bpm[u]);
  }
  if (act === 'solo') return playLayers(`solo:${u}:${b.dataset.k}`, b, [{ url: partUrl(u, b.dataset.k), bpm: D.bpm[u] }], D.bpm[u]);
  if (act === 'swap') {
    const base = b.dataset.base, k = b.dataset.k;
    if (!hasPart(u, k)) return;
    return playLayers(`swap:${base}:${k}:${u}`, b, [...wholeLayers(base, [k]), { url: partUrl(u, k), bpm: D.bpm[u] }], D.bpm[base]);
  }
  if (act === 'add') {
    const base = b.dataset.base, k = b.dataset.k;
    if (!hasPart(u, k)) return;
    return playLayers(`add:${base}:${k}:${u}`, b, [...wholeLayers(base), { url: partUrl(u, k), bpm: D.bpm[u] }], D.bpm[base]);
  }
  if (act === 'part') {
    const p = D.parts[b.dataset.p]; if (!p) return;
    const pu = p.in.find(x => hasPart(x, p.lane)); if (!pu) return;
    return playLayers('part:' + b.dataset.p, b, [{ url: partUrl(pu, p.lane), bpm: D.bpm[pu] }], D.bpm[pu]);
  }
  if (act === 'pair') {
    const p = D.parts[b.dataset.p], q = D.parts[b.dataset.q]; if (!p || !q) return;
    const pu = p.in.find(x => hasPart(x, p.lane)), qu = q.in.find(x => hasPart(x, q.lane)); if (!pu || !qu) return;
    return playLayers(`pair:${b.dataset.p}:${b.dataset.q}`, b, [{ url: partUrl(pu, p.lane), bpm: D.bpm[pu] }, { url: partUrl(qu, q.lane), bpm: D.bpm[qu] }], D.bpm[pu]);
  }
};
const c = D.counts;
document.getElementById('sum').textContent = `${c.measured} of ${c.snapshots} snapshots measured · ${c.parts || 0} parts · ${c.log_lines} lines of the log · ${D.made}`;
drawLanes(); draw();
</script></body></html>
"""
