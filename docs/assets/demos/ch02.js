// 2장 데모: DDA(첫 코드, 방향 버그 포함) vs Bresenham, 한 칸씩 따라가기
(function () {
  SR.register('line-step', host => {
    const W = 40, H = 24, S = 15;
    const view = SR.canvas(W * S, H * S);
    const readout = SR.el('div', { class: 'readout' });
    let a = { x: 6, y: 18 }, b = { x: 33, y: 6 }, mode = 'bres', drag = null, shown = Infinity, timer = null;

    function ddaFirst(x0, y0, x1, y1) {           // f673c9d 그대로: abs 때문에 항상 +방향
      const out = [], dy = Math.abs(y1 - y0), dx = Math.abs(x1 - x0);
      const steps = Math.max(dx, dy) || 1, xi = dx / steps, yi = dy / steps;
      let x = x0, y = y0;
      for (let i = 0; i <= steps; i++) { out.push({ x: Math.trunc(x), y: Math.round(y), info: `x=${x.toFixed(2)} y=${y.toFixed(2)}` }); x += xi; y += yi; }
      return out;
    }
    function bres(x0, y0, x1, y1) {
      const out = [], dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
      let err = dx + dy;
      for (let guard = 0; guard < 400; guard++) {
        const e2 = 2 * err; let move = [];
        const p = { x: x0, y: y0 };
        if (x0 === x1 && y0 === y1) { p.info = `err=${err}  끝`; out.push(p); break; }
        if (e2 >= dy) { err += dy; x0 += sx; move.push('x'); }
        if (e2 <= dx) { err += dx; y0 += sy; move.push('y'); }
        p.info = `err=${err - (move.includes('x') ? dy : 0) - (move.includes('y') ? dx : 0)}  e2=${e2}  →  ${move.join('+')}로 이동`;
        out.push(p);
      }
      return out;
    }
    function pixels() { return mode === 'dda' ? ddaFirst(a.x, a.y, b.x, b.y) : bres(a.x, a.y, b.x, b.y); }
    function draw() {
      const g = view.ctx, px = pixels(), n = Math.min(shown, px.length);
      g.fillStyle = '#1d2026'; g.fillRect(0, 0, view.w, view.h);
      g.strokeStyle = 'rgba(255,255,255,.06)';
      for (let x = 0; x <= W; x++) { g.beginPath(); g.moveTo(x * S + .5, 0); g.lineTo(x * S + .5, H * S); g.stroke(); }
      for (let y = 0; y <= H; y++) { g.beginPath(); g.moveTo(0, y * S + .5); g.lineTo(W * S, y * S + .5); g.stroke(); }
      for (let i = 0; i < n; i++) {
        const p = px[i]; if (p.x < 0 || p.x >= W || p.y < 0 || p.y >= H) continue;
        g.fillStyle = i === n - 1 && n < px.length ? '#ffd166' : '#f97316'; g.fillRect(p.x * S + 1, p.y * S + 1, S - 2, S - 2);
      }
      // 실제 선
      g.strokeStyle = '#7cb2ff'; g.lineWidth = 1.5; g.beginPath();
      g.moveTo(a.x * S + S / 2, a.y * S + S / 2); g.lineTo(b.x * S + S / 2, b.y * S + S / 2); g.stroke();
      for (const [p, c] of [[a, '#4ade80'], [b, '#f87171']]) { g.fillStyle = c; g.beginPath(); g.arc(p.x * S + S / 2, p.y * S + S / 2, 6, 0, 7); g.fill(); }
      const last = px[Math.max(0, n - 1)];
      const ends = px[px.length - 1];
      const miss = mode === 'dda' && (ends.x !== b.x || ends.y !== b.y);
      readout.textContent = `${mode === 'dda' ? '첫 DDA' : 'Bresenham'}: 픽셀 ${px.length}개` +
        (n < px.length ? `   (${n}/${px.length}단계)  ${last.info}` : '') +
        (miss ? `\n끝점(빨강) (${b.x}, ${b.y})에 도착하지 못하고 (${ends.x}, ${ends.y})에서 끝남: abs 때문에 방향이 항상 +x, +y` : '');
    }
    function cell(ev) { const p = SR.pos(view.c, ev, view.w, view.h); return { x: Math.max(0, Math.min(W - 1, Math.floor(p.x / S))), y: Math.max(0, Math.min(H - 1, Math.floor(p.y / S))) }; }
    view.c.addEventListener('pointerdown', ev => { const c = cell(ev); drag = Math.hypot(c.x - a.x, c.y - a.y) < Math.hypot(c.x - b.x, c.y - b.y) ? a : b; view.c.setPointerCapture(ev.pointerId); Object.assign(drag, c); shown = Infinity; draw(); });
    view.c.addEventListener('pointermove', ev => { if (!drag) return; Object.assign(drag, cell(ev)); draw(); });
    view.c.addEventListener('pointerup', () => drag = null);
    const bBres = SR.el('button', { type: 'button', class: 'on' }, 'Bresenham'), bDda = SR.el('button', { type: 'button' }, '첫 DDA (f673c9d)');
    const pick = m => { mode = m; bBres.classList.toggle('on', m === 'bres'); bDda.classList.toggle('on', m === 'dda'); shown = Infinity; draw(); };
    bBres.onclick = () => pick('bres'); bDda.onclick = () => pick('dda');
    const step = SR.el('button', { type: 'button', onclick: () => { clearInterval(timer); shown = shown === Infinity || shown >= pixels().length ? 1 : shown + 1; draw(); } }, '한 칸씩');
    const play = SR.el('button', { type: 'button', onclick: () => { clearInterval(timer); shown = 0; timer = setInterval(() => { shown++; draw(); if (shown >= pixels().length) { clearInterval(timer); shown = Infinity; draw(); } }, 70); } }, '처음부터 재생');
    host.append(SR.el('div', { class: 'demo-title' }, '초록·빨강 끝점을 끌어 보세요'), view.c,
      SR.el('div', { class: 'controls' }, bBres, bDda, step, play), readout);
    draw();
  });
})();
