// 6장 데모: 구 하나를 면 단위 Lambert / 픽셀 단위 Lambert / Phong 반사로. 빛은 끌어서 이동.
(function () {
  SR.register('lighting', host => {
    const W = 300, H = 300, R = 120;
    const view = SR.canvas(W, H);
    const off = document.createElement('canvas'); off.width = W; off.height = H;
    const octx = off.getContext('2d');
    const readout = SR.el('div', { class: 'readout' });
    let mode = 'phong', shin = 32, L = norm([-0.5, -0.6, 0.65]), drag = false;
    function norm(v) { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }
    const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    // 면 단위: 위경도 격자(12×8)의 면 법선
    const SEG_U = 14, SEG_V = 9;
    function faceNormal(n) {
      const lon = Math.atan2(n[0], n[2]), lat = Math.asin(Math.max(-1, Math.min(1, -n[1])));
      const i = Math.floor((lon + Math.PI) / (2 * Math.PI) * SEG_U), j = Math.floor((lat + Math.PI / 2) / Math.PI * SEG_V);
      const clon = (i + .5) / SEG_U * 2 * Math.PI - Math.PI, clat = (j + .5) / SEG_V * Math.PI - Math.PI / 2;
      return [Math.cos(clat) * Math.sin(clon), -Math.sin(clat), Math.cos(clat) * Math.cos(clon)];
    }
    function render() {
      const img = octx.createImageData(W, H), d = img.data, V = [0, 0, 1];
      let lit = 0, powCalls = 0, skipped = 0;
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        const o = (y * W + x) * 4, nx = (x + .5 - W / 2) / R, ny = (y + .5 - H / 2) / R, r2 = nx * nx + ny * ny;
        if (r2 > 1) { d[o] = 26; d[o + 1] = 28; d[o + 2] = 34; d[o + 3] = 255; continue; }
        let n = [nx, ny, Math.sqrt(1 - r2)];
        if (mode === 'flat') n = faceNormal(n);
        const diff = Math.max(0, dot(n, L));
        let spec = 0;
        if (mode === 'phong') {
          lit++;
          const rd = [2 * dot(n, L) * n[0] - L[0], 2 * dot(n, L) * n[1] - L[1], 2 * dot(n, L) * n[2] - L[2]];
          const base = Math.max(0, dot(V, rd));
          if (diff > 0 && base !== 0) { spec = Math.pow(base, shin); powCalls++; } else skipped++;
        }
        const amb = 0.08, base = [0.85, 0.32, 0.12];
        for (let k = 0; k < 3; k++) d[o + k] = Math.min(255, 255 * (base[k] * (amb + diff) + 0.9 * spec));
        d[o + 3] = 255;
      }
      octx.putImageData(img, 0, 0);
      const g = view.ctx; g.drawImage(off, 0, 0, W, H);
      // 빛 방향 표시
      const lx = W / 2 + L[0] * R * 1.15, ly = H / 2 + L[1] * R * 1.15;
      g.fillStyle = L[2] > 0 ? '#ffd166' : 'rgba(255,209,102,.4)'; g.beginPath(); g.arc(lx, ly, 9, 0, 7); g.fill();
      g.strokeStyle = '#ffd166'; g.setLineDash([4, 4]); g.beginPath(); g.moveTo(W / 2, H / 2); g.lineTo(lx, ly); g.stroke(); g.setLineDash([]);
      const names = { flat: '면 단위 Lambert (e896b3b)', pixel: '픽셀 단위 Lambert (5751375)', phong: `Phong 반사 (shininess ${shin})` };
      readout.textContent = names[mode] + (mode === 'phong'
        ? `\n구 위 픽셀 ${lit}개 중 pow 호출 ${powCalls}개, 생략 ${skipped}개 (${Math.round(100 * skipped / Math.max(1, lit))}%) → PR #6이 건너뛰는 몫`
        : '\n노란 점(빛)을 끌어서 방향을 바꿔 보세요.');
    }
    function setLight(ev) {
      const p = SR.pos(view.c, ev, W, H); let x = (p.x - W / 2) / (R * 1.15), y = (p.y - H / 2) / (R * 1.15);
      const r = Math.hypot(x, y); if (r > 1) { x /= r; y /= r; }
      L = norm([x, y, Math.sqrt(Math.max(0.02, 1 - x * x - y * y))]); render();
    }
    view.c.addEventListener('pointerdown', ev => { drag = true; view.c.setPointerCapture(ev.pointerId); setLight(ev); });
    view.c.addEventListener('pointermove', ev => { if (drag) setLight(ev); });
    view.c.addEventListener('pointerup', () => drag = false);
    const btns = {};
    const pick = m => { mode = m; for (const k in btns) btns[k].classList.toggle('on', k === m); render(); };
    for (const [k, label] of [['flat', '면 단위'], ['pixel', '픽셀 단위'], ['phong', 'Phong 반사']]) btns[k] = SR.el('button', { type: 'button', onclick: () => pick(k) }, label);
    const s = SR.slider('광택', 2, 128, 1, shin, v => { shin = v; render(); return v; });
    host.append(SR.el('div', { class: 'demo-title' }, '구를 비추는 빛'), view.c, SR.el('div', { class: 'controls' }, btns.flat, btns.pixel, btns.phong, s), readout);
    s.sync(); pick('phong');
  });
})();
