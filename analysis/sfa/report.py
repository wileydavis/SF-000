"""A page to listen to what was found: every snapshot by energy, and beside each what it is like, what is a step up
or down from it, and what has followed it. Pressing a chip plays its clip. It is written beside what is kept here,
and plays the clips from there, so it works with no line to the library."""
from __future__ import annotations

import html
import json
import os
import time
from pathlib import Path


def write_report(root: Path, rows: list[dict], measured: list[dict], found: dict):
    names = {r["uid"]: (r.get("name") or r["uid"][-6:]) for r in rows}
    clips = {r["uid"]: f"clips/{''.join(c if c.isalnum() or c in '-_' else '_' for c in r['uid'])}.ogg" for r in rows if (root / "clips" / f"{''.join(c if c.isalnum() or c in '-_' else '_' for c in r['uid'])}.ogg").exists()}
    snaps = found.get("snapshots", {})
    data = {
        "names": names,
        "clips": clips,
        "found": snaps,
        "counts": found.get("counts", {}),
        "made": time.strftime("%Y-%m-%d %H:%M", time.localtime(found.get("at", 0) / 1000)),
        "unmeasured": [names.get(m["uid"], m["uid"]) for m in measured if not m.get("mix") or m["mix"].get("silent")],
    }
    page = PAGE.replace("/*DATA*/null", json.dumps(data).replace("</", "<\\/"))
    tmp = root / "report.html.part"
    tmp.write_text(page)
    os.replace(tmp, root / "report.html")


PAGE = r"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>SF-000 Analysis</title>
<style>
  :root { color-scheme: dark; --bg:#141414; --card:#1f1f1f; --ink:#e9e6df; --dim:#8d8a84; --hot:#f2c230; --line:#2c2c2c; }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--ink); font:14px/1.4 -apple-system, "Helvetica Neue", Arial, sans-serif; }
  header { position:sticky; top:0; z-index:2; background:#101010ee; border-bottom:1px solid var(--line); padding:14px 22px; display:flex; gap:18px; align-items:baseline; flex-wrap:wrap; }
  h1 { font-size:15px; letter-spacing:.12em; margin:0; font-weight:700; }
  .dim { color:var(--dim); }
  input { background:#0c0c0c; border:1px solid var(--line); color:var(--ink); padding:6px 9px; border-radius:6px; width:220px; }
  main { padding:12px 22px 80px; }
  .row { display:grid; grid-template-columns: 230px 1fr; gap:14px; padding:10px 0; border-bottom:1px solid var(--line); }
  .me { display:flex; flex-direction:column; gap:6px; }
  .bar { height:6px; background:#2a2a2a; border-radius:3px; overflow:hidden; }
  .bar i { display:block; height:100%; background:var(--hot); }
  .rel { display:grid; grid-template-columns: 104px 1fr; gap:4px 10px; align-items:start; }
  .rel b { font-weight:600; font-size:11px; letter-spacing:.1em; color:var(--dim); padding-top:5px; }
  .chips { display:flex; flex-wrap:wrap; gap:5px; }
  .chip { border:1px solid #3a3a3a; background:var(--card); color:var(--ink); border-radius:5px; padding:4px 8px; font-size:12px; cursor:pointer; white-space:nowrap; }
  .chip small { color:var(--dim); margin-left:5px; }
  .chip:hover { border-color:#666; }
  .chip.playing { background:var(--hot); color:#111; border-color:var(--hot); }
  .chip.playing small { color:#333; }
  .chip.mute { cursor:default; opacity:.55; }
  .big { font-size:14px; font-weight:600; padding:7px 10px; text-align:left; }
  .num { font-variant-numeric: tabular-nums; font-size:11px; }
  .none { color:var(--dim); font-size:12px; padding-top:4px; }
</style></head><body>
<header>
  <h1>SF-000 ANALYSIS</h1>
  <span class="dim" id="sum"></span>
  <input id="q" placeholder="find a snapshot by name" autocomplete="off">
  <span class="dim">Press a chip to hear it; again to stop. Sorted by energy, most first.</span>
</header>
<main id="list"></main>
<script>
const D = /*DATA*/null;
const audio = new Audio(); let playingUid = null;
function play(uid) {
  document.querySelectorAll('.chip.playing').forEach(c => c.classList.remove('playing'));
  if (playingUid === uid && !audio.paused) { audio.pause(); playingUid = null; return; }
  if (!D.clips[uid]) return;
  audio.src = D.clips[uid]; audio.loop = true; audio.play(); playingUid = uid;
  document.querySelectorAll(`.chip[data-u="${CSS.escape(uid)}"]`).forEach(c => c.classList.add('playing'));
}
audio.onpause = () => { if (audio.ended || audio.paused) document.querySelectorAll('.chip.playing').forEach(c => c.classList.remove('playing')); };
const esc = s => String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function chip(uid, note, big) {
  const name = D.names[uid] || uid.slice(-6);
  const can = !!D.clips[uid];
  return `<button class="chip${big ? ' big' : ''}${can ? '' : ' mute'}" data-u="${esc(uid)}" title="${can ? 'play' : 'no clip here'}">${esc(name)}${note !== undefined ? `<small>${esc(note)}</small>` : ''}</button>`;
}
function rel(label, items, fmt) {
  items = (items || []).filter(x => D.names[x[0]]);          // (one no longer in the library is left out)
  if (!items.length) return `<b>${label}</b><div class="none">-</div>`;
  return `<b>${label}</b><div class="chips">${items.map(fmt).join('')}</div>`;
}
const pct = x => `${x > 0 ? '+' : ''}${Math.round(x * 100)}`;
function draw() {
  const q = document.getElementById('q').value.trim().toLowerCase();
  const all = Object.entries(D.found).filter(([u, f]) => f.energy !== undefined)
    .sort((a, b) => b[1].energy - a[1].energy);
  const shown = q ? all.filter(([u]) => (D.names[u] || '').toLowerCase().includes(q)) : all;
  document.getElementById('list').innerHTML = shown.map(([u, f]) => `
    <div class="row">
      <div class="me">${chip(u, undefined, true)}
        <div class="bar"><i style="width:${Math.round(f.energy * 100)}%"></i></div>
        <div class="num dim">energy ${Math.round(f.energy * 100)} · ${esc(f.measured.loudness)} LUFS · ${esc(f.measured.onsets)} hits/beat${f.measured.sounding != null ? ` · ${f.measured.sounding} lanes` : ''} · from ${esc(f.measured.from)}</div>
      </div>
      <div class="rel">
        ${rel('ALIKE', f.alike, ([v, d]) => chip(v, d.toFixed(2)))}
        ${rel('UP', f.up, ([v, d, de]) => chip(v, pct(de)))}
        ${rel('DOWN', f.down, ([v, d, de]) => chip(v, pct(de)))}
        ${rel('FOLLOWED BY', f.follows, ([v, n]) => chip(v, `×${n}`))}
        ${f.grew_from && D.names[f.grew_from] ? `<b>TAKEN FROM</b><div class="chips">${chip(f.grew_from)}</div>` : ''}
      </div>
    </div>`).join('') + (D.unmeasured.length ? `<p class="dim">Not measured (no sound here for them): ${D.unmeasured.map(esc).join(', ')}</p>` : '');
}
const c = D.counts;
document.getElementById('sum').textContent = `${c.measured} of ${c.snapshots} snapshots measured (${c.lossless} without loss) · ${c.log_lines} lines of the log · ${D.made}`;
document.getElementById('q').oninput = draw;
document.getElementById('list').onclick = e => { const b = e.target.closest('.chip'); if (b) play(b.dataset.u); };
draw();
</script></body></html>
"""
