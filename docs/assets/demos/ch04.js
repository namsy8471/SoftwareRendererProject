// 4장 데모: 한 평면으로 삼각형 자르기(Sutherland–Hodgman) + 띠 vs 부채꼴 분할
(function () {
  SR.register('clip-fan', host => {
    const W = 560, H = 320;
    const view = SR.canvas(W, H);
    const readout = SR.el('div', { class: 'readout' });
    let V = [{ x: 120, y: 260 }, { x: 470, y: 230 }, { x: 260, y: 40 }];
    let planeX = 330, mode = 'fan', drag = null;
    // 평면: x <= planeX 쪽이 "안" (카메라 앞쪽이라고 생각)
    function clip(poly) {
      const out = [], inside = p => p.x <= planeX;
      for (let i = 0; i < poly.length; i++) {
        const cur = poly[i], prev = poly[(i + poly.length - 1) % poly.length];
        const ci = inside(cur), pi = inside(prev);
        if (ci !== pi) { const t = (planeX - prev.x) / (cur.x - prev.x); out.push({ x: planeX, y: prev.y + (cur.y - prev.y) * t, cut: true }); }
        if (ci) out.push(cur);
      }
      return out;
    }
    function tris(poly) {
      const t = [];
      if (poly.length < 3) return t;
      if (mode === 'fan') for (let i = 1; i + 1 < poly.length; i++) t.push([poly[0], poly[i], poly[i + 1]]);
      else for (let j = 0; j + 2 < poly.length; j++) t.push([poly[j], poly[j + 1], poly[j + 2]]);
      return t;
    }
    // 픽셀 커버리지를 세어 겹침/빈틈 표시 (4px 격자)
    // tol > 0: 변에서 tol픽셀 안쪽까지만 안으로 침(경계 샘플 오차 제외), tol < 0: 변 바깥 |tol|픽셀까지 안으로 침
    function inTri(p, a, b, c, tol = 0) {
      const e = (u, v) => ((v.x - u.x) * (p.y - u.y) - (v.y - u.y) * (p.x - u.x)) / (Math.hypot(v.x - u.x, v.y - u.y) || 1);
      const d0 = e(a, b), d1 = e(b, c), d2 = e(c, a);
      return (d0 >= tol && d1 >= tol && d2 >= tol) || (d0 <= -tol && d1 <= -tol && d2 <= -tol);
    }
    function draw() {
      const g = view.ctx, poly = clip(V), T = tris(poly);
      g.fillStyle = SR.css('--bg'); g.fillRect(0, 0, W, H);
      // 밖 영역
      g.fillStyle = 'rgba(185,28,28,.08)'; g.fillRect(planeX, 0, W - planeX, H);
      let over = 0, hole = 0;
      const step = 4;
      for (let y = step / 2; y < H; y += step) for (let x = step / 2; x < W; x += step) {
        const p = { x, y };
        const need = poly.length >= 3 && inTri(p, V[0], V[1], V[2], 2) && x <= planeX - 2;
        let c = 0, cs = 0; for (const t of T) { if (inTri(p, ...t, -1)) c++; if (inTri(p, ...t, 2)) cs++; }
        if (cs > 1) { g.fillStyle = 'rgba(234,88,12,.85)'; over++; }
        else if (c >= 1) g.fillStyle = 'rgba(37,99,235,.45)';
        else if (need) { g.fillStyle = 'rgba(220,38,38,.9)'; hole++; }
        else continue;
        g.fillRect(x - step / 2, y - step / 2, step - 0.6, step - 0.6);
      }
      // 원래 삼각형
      g.setLineDash([5, 4]); g.strokeStyle = SR.css('--muted'); g.lineWidth = 1.2; g.beginPath();
      V.forEach((p, i) => i ? g.lineTo(p.x, p.y) : g.moveTo(p.x, p.y)); g.closePath(); g.stroke(); g.setLineDash([]);
      // 분할 삼각형 테두리
      g.strokeStyle = SR.css('--fg'); g.lineWidth = 1;
      T.forEach(t => { g.beginPath(); t.forEach((p, i) => i ? g.lineTo(p.x, p.y) : g.moveTo(p.x, p.y)); g.closePath(); g.stroke(); });
      // 평면
      g.strokeStyle = '#dc2626'; g.lineWidth = 2.5; g.beginPath(); g.moveTo(planeX, 0); g.lineTo(planeX, H); g.stroke();
      g.fillStyle = '#dc2626'; g.font = '12px sans-serif'; g.fillText('클립 평면 (끌어서 이동)', Math.min(planeX + 6, W - 140), 16);
      g.fillText('밖', Math.min(planeX + 10, W - 20), H - 10); g.fillStyle = SR.css('--fg'); g.fillText('안', Math.max(planeX - 24, 4), H - 10);
      // 정점 번호
      poly.forEach((p, i) => { g.fillStyle = p.cut ? '#dc2626' : SR.css('--fg'); g.beginPath(); g.arc(p.x, p.y, 4, 0, 7); g.fill(); g.fillText(String(i), p.x + 7, p.y - 6); });
      V.forEach(p => { g.strokeStyle = '#f59e0b'; g.lineWidth = 2; g.beginPath(); g.arc(p.x, p.y, 8, 0, 7); g.stroke(); });
      readout.textContent = `잘린 다각형 정점 ${poly.length}개 → 삼각형 ${T.length}개 (${mode === 'fan' ? '부채꼴 (0,i,i+1)' : '첫 코드 띠 (j,j+1,j+2)'})\n` +
        (T.length ? T.map(t => '(' + t.map(p => poly.indexOf(p)).join(',') + ')').join(' ') : '') +
        `\n겹쳐 두 번 칠한 곳 ${over}칸(주황), 빠진 곳 ${hole}칸(빨강)`;
    }
    view.c.addEventListener('pointerdown', ev => {
      const p = SR.pos(view.c, ev, W, H);
      let best = null, bd = 18; V.forEach(v => { const d = Math.hypot(v.x - p.x, v.y - p.y); if (d < bd) { bd = d; best = v; } });
      drag = best || (Math.abs(p.x - planeX) < 16 ? 'plane' : null);
      if (drag) view.c.setPointerCapture(ev.pointerId);
    });
    view.c.addEventListener('pointermove', ev => {
      if (!drag) return; const p = SR.pos(view.c, ev, W, H);
      if (drag === 'plane') planeX = Math.max(10, Math.min(W - 10, p.x)); else { drag.x = Math.max(4, Math.min(W - 4, p.x)); drag.y = Math.max(4, Math.min(H - 4, p.y)); }
      draw();
    });
    view.c.addEventListener('pointerup', () => drag = null);
    const bFan = SR.el('button', { type: 'button', class: 'on' }, '부채꼴 (지금)'), bStrip = SR.el('button', { type: 'button' }, '첫 코드 (띠)');
    const pick = m => { mode = m; bFan.classList.toggle('on', m === 'fan'); bStrip.classList.toggle('on', m === 'strip'); draw(); };
    bFan.onclick = () => pick('fan'); bStrip.onclick = () => pick('strip');
    host.append(SR.el('div', { class: 'demo-title' }, '꼭짓점과 빨간 평면을 끌어 보세요'), view.c, SR.el('div', { class: 'controls' }, bFan, bStrip), readout);
    draw();
  });
})();
