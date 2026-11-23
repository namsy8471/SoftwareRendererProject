// 3장 데모: edge function 래스터화. 첫 코드(정수 좌표, 모서리 판정) vs 지금(1/256 고정소수점, 픽셀 중심)
(function () {
  SR.register('tri-raster', host => {
    const W = 32, H = 20, S = 18;
    const view = SR.canvas(W * S, H * S);
    const readout = SR.el('div', { class: 'readout' });
    let V = [{ x: 4.3, y: 3.6 }, { x: 27.6, y: 7.2 }, { x: 10.8, y: 17.4 }];
    const extra = { x: 24.5, y: 17.8 };   // 옆 삼각형: V1, extra, V2 (같은 변 V1-V2 공유)
    let mode = 'now', showNeighbor = false, drag = null, hover = null;

    function raster(a, b, c) {
      // 반환: 칠한 픽셀 map(key → [w0,w1,w2]), 검사 수, bbox
      const q = mode === 'first' ? (v => ({ x: Math.trunc(v.x), y: Math.trunc(v.y) })) : (v => v);
      let p0 = q(a), p1 = q(b), p2 = q(c);
      // 방향을 반시계로 맞춤(면적 부호)
      const area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
      if (area < 0) { const t = p1; p1 = p2; p2 = t; }
      const minX = Math.max(0, Math.trunc(Math.min(p0.x, p1.x, p2.x))), maxX = Math.min(W - 1, Math.trunc(Math.max(p0.x, p1.x, p2.x)));
      const minY = Math.max(0, Math.trunc(Math.min(p0.y, p1.y, p2.y))), maxY = Math.min(H - 1, Math.trunc(Math.max(p0.y, p1.y, p2.y)));
      const off = mode === 'first' ? 0 : 0.5, fx = mode === 'first' ? (v => v) : (v => Math.trunc(v * 256));
      const E = (A, B, x, y) => fx((B.y - A.y) * (x + off - A.x) - (B.x - A.x) * (y + off - A.y));
      const cov = new Map(); let tested = 0;
      for (let y = minY; y <= maxY; y++) for (let x = minX; x <= maxX; x++) {
        tested++;
        // 부호 규칙을 원 코드와 같게: w0는 변 1→2, w1은 2→0, w2는 0→1 (y축이 아래로 → 반시계 기준 부호 반전)
        const w0 = -E(p1, p2, x, y), w1 = -E(p2, p0, x, y), w2 = -E(p0, p1, x, y);
        if (w0 >= 0 && w1 >= 0 && w2 >= 0) cov.set(y * W + x, [w0, w1, w2]);
      }
      return { cov, tested, bbox: [minX, minY, maxX, maxY], pts: [p0, p1, p2] };
    }
    function draw() {
      const g = view.ctx;
      g.fillStyle = '#1d2026'; g.fillRect(0, 0, view.w, view.h);
      const r = raster(V[0], V[1], V[2]);
      const n = showNeighbor ? raster(V[1], extra, V[2]) : null;
      const [bx0, by0, bx1, by1] = r.bbox;
      g.fillStyle = 'rgba(124,178,255,.10)'; g.fillRect(bx0 * S, by0 * S, (bx1 - bx0 + 1) * S, (by1 - by0 + 1) * S);
      let both = 0;
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        const k = y * W + x, a = r.cov.get(k), b = n && n.cov.get(k);
        if (a) {
          const s = a[0] + a[1] + a[2] || 1;
          g.fillStyle = `rgb(${Math.round(255 * a[0] / s)},${Math.round(255 * a[1] / s)},${Math.round(255 * a[2] / s)})`;
          g.fillRect(x * S + 1, y * S + 1, S - 2, S - 2);
        }
        if (b) { g.fillStyle = a ? '#ffffff' : 'rgba(160,160,170,.55)'; if (a) both++; g.fillRect(x * S + (a ? 5 : 1), y * S + (a ? 5 : 1), a ? S - 10 : S - 2, a ? S - 10 : S - 2); }
      }
      g.strokeStyle = 'rgba(255,255,255,.07)';
      for (let x = 0; x <= W; x++) { g.beginPath(); g.moveTo(x * S + .5, 0); g.lineTo(x * S + .5, H * S); g.stroke(); }
      for (let y = 0; y <= H; y++) { g.beginPath(); g.moveTo(0, y * S + .5); g.lineTo(W * S, y * S + .5); g.stroke(); }
      // 판정점 표시
      if (mode === 'now') { g.fillStyle = 'rgba(255,255,255,.18)'; for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) g.fillRect(x * S + S / 2 - 1, y * S + S / 2 - 1, 2, 2); }
      else { g.fillStyle = 'rgba(255,255,255,.25)'; for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) g.fillRect(x * S, y * S, 2, 2); }
      const tri = (pts, color) => { g.strokeStyle = color; g.lineWidth = 1.5; g.beginPath(); pts.forEach((p, i) => i ? g.lineTo(p.x * S, p.y * S) : g.moveTo(p.x * S, p.y * S)); g.closePath(); g.stroke(); };
      tri(V, '#ffd166'); if (mode === 'first') tri(r.pts, 'rgba(249,115,22,.9)');
      if (showNeighbor) tri([V[1], extra, V[2]], 'rgba(200,200,210,.8)');
      const handles = showNeighbor ? [...V, extra] : V;
      handles.forEach((p, i) => { g.fillStyle = ['#f87171', '#4ade80', '#60a5fa', '#d4d4d8'][i]; g.beginPath(); g.arc(p.x * S, p.y * S, 6, 0, 7); g.fill(); });
      let txt = `경계 상자 ${(bx1 - bx0 + 1)}×${(by1 - by0 + 1)} = 검사 ${r.tested}픽셀, 칠함 ${r.cov.size}픽셀 (헛검사 ${Math.round(100 * (1 - r.cov.size / Math.max(1, r.tested)))}%)`;
      if (showNeighbor) txt += `\n두 삼각형이 모두 칠한 픽셀(흰 점): ${both}개 → 지금 렌더러는 깊이가 같으면 작은 ID가 이김`;
      if (hover) { const v = r.cov.get(hover.y * W + hover.x); txt += `\n픽셀 (${hover.x}, ${hover.y}) ${v ? `w0=${v[0]} w1=${v[1]} w2=${v[2]}  → 안쪽` : '→ 바깥 (음수인 edge가 있음)'}${mode === 'now' ? '  (단위: 1/256 픽셀)' : ''}`; }
      readout.textContent = txt;
    }
    const toCell = ev => { const p = SR.pos(view.c, ev, view.w, view.h); return { x: p.x / S, y: p.y / S }; };
    view.c.addEventListener('pointerdown', ev => {
      const p = toCell(ev), handles = showNeighbor ? [...V, extra] : V;
      let best = null, bd = 1.2;
      handles.forEach(h => { const d = Math.hypot(h.x - p.x, h.y - p.y); if (d < bd) { bd = d; best = h; } });
      drag = best; if (drag) view.c.setPointerCapture(ev.pointerId);
    });
    view.c.addEventListener('pointermove', ev => {
      const p = toCell(ev);
      hover = { x: Math.floor(p.x), y: Math.floor(p.y) };
      if (drag) { drag.x = Math.max(0, Math.min(W - 0.01, p.x)); drag.y = Math.max(0, Math.min(H - 0.01, p.y)); }
      draw();
    });
    view.c.addEventListener('pointerup', () => drag = null);
    view.c.addEventListener('pointerleave', () => { hover = null; draw(); });
    const bNow = SR.el('button', { type: 'button', class: 'on' }, '지금 (1/256, 픽셀 중심)'), bFirst = SR.el('button', { type: 'button' }, '첫 코드 (정수 좌표)');
    const pick = m => { mode = m; bNow.classList.toggle('on', m === 'now'); bFirst.classList.toggle('on', m === 'first'); draw(); };
    bNow.onclick = () => pick('now'); bFirst.onclick = () => pick('first');
    host.append(SR.el('div', { class: 'demo-title' }, '꼭짓점을 끌어 보세요 (색 = 무게중심 좌표)'), view.c,
      SR.el('div', { class: 'controls' }, bNow, bFirst, SR.toggle('옆 삼각형 보기', false, v => { showNeighbor = v; draw(); })), readout);
    draw();
  });
})();
