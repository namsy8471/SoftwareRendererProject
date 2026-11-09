// 1장 데모: 픽셀 배열, BGR 순서, 더블 버퍼, fps와 ms
(function () {
  // ── 1. 화면은 1차원 배열이다 ────────────────────────────────
  SR.register('pixel-grid', host => {
    const W = 32, H = 18, S = 18;              // 32×18 "화면", 한 픽셀을 18px로 확대
    const pixels = new Uint32Array(W * H);     // 0x00RRGGBB (DIB와 같은 순서)
    const view = SR.canvas(W * S, H * S);
    const mem = SR.canvas(W * S, 64);
    const readout = SR.el('div', { class: 'readout' }, '격자 위에 마우스를 올리거나 칠해 보세요.');
    let hover = -1, timer = null, brush = 0xE04040;

    function draw() {
      const g = view.ctx;
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        const p = pixels[y * W + x];
        g.fillStyle = p ? '#' + p.toString(16).padStart(6, '0') : ((x + y) & 1 ? '#202227' : '#26292f');
        g.fillRect(x * S, y * S, S, S);
      }
      if (hover >= 0) {
        const hx = hover % W, hy = (hover / W) | 0;
        g.strokeStyle = '#ffd166'; g.lineWidth = 2;
        g.strokeRect(hx * S + 1, hy * S + 1, S - 2, S - 2);
        g.strokeStyle = 'rgba(255,209,102,.35)'; g.lineWidth = 1;
        g.strokeRect(0.5, hy * S + 0.5, W * S - 1, S - 1);
      }
      // 메모리 띠: 같은 픽셀들을 주소 순서로 한 줄에 펼친다(576칸을 2줄로 접어서)
      const m = mem.ctx, cell = (W * S) / (W * H / 2);
      m.clearRect(0, 0, mem.w, mem.h);
      for (let i = 0; i < W * H; i++) {
        const row = i < W * H / 2 ? 0 : 1, col = row ? i - W * H / 2 : i;
        const y = (i / W) | 0, p = pixels[i];
        m.fillStyle = p ? '#' + p.toString(16).padStart(6, '0') : (y & 1 ? '#3a3f48' : '#2b2f36');
        m.fillRect(col * cell, 6 + row * 30, Math.max(cell - 0.3, 0.6), 22);
        if (i === hover) { m.fillStyle = '#ffd166'; m.fillRect(col * cell - 1, 2 + row * 30, cell + 2, 30); }
      }
    }
    function at(ev) {
      const { x, y } = SR.pos(view.c, ev, view.w, view.h);
      const px = Math.floor(x / S), py = Math.floor(y / S);
      return px >= 0 && px < W && py >= 0 && py < H ? py * W + px : -1;
    }
    function show(i) {
      if (i < 0) return;
      const x = i % W, y = (i / W) | 0;
      readout.textContent = `(x=${x}, y=${y})  →  pixels[y * width + x] = pixels[${y} * ${W} + ${x}] = pixels[${i}]` +
        `   값 ${SR.hex(pixels[i], 8)}   주소 = base + ${i * 4} 바이트`;
    }
    let painting = false;
    view.c.addEventListener('pointerdown', ev => { painting = true; view.c.setPointerCapture(ev.pointerId); const i = at(ev); if (i >= 0) pixels[i] = brush; hover = i; show(i); draw(); });
    view.c.addEventListener('pointermove', ev => { const i = at(ev); hover = i; if (painting && i >= 0) pixels[i] = brush; show(i); draw(); });
    view.c.addEventListener('pointerup', () => painting = false);
    view.c.addEventListener('pointerleave', () => { hover = -1; draw(); });

    // 첫 Render()의 이중 for: 화면 가운데 절반 크기 사각형을 행 우선으로 칠한다
    const speed = SR.slider('속도', 1, 40, 1, 6, v => v + '픽셀/틱');
    function runRect() {
      clearInterval(timer);
      const hh = H / 2 | 0, hw = W / 2 | 0, list = [];
      for (let i = (hh / 2) | 0; i < hh + ((hh / 2) | 0); i++)
        for (let j = (hw / 2) | 0; j < hw + ((hw / 2) | 0); j++) list.push(i * W + j);
      let k = 0;
      timer = setInterval(() => {
        for (let n = 0; n < Number(speed.input.value) && k < list.length; n++, k++) { pixels[list[k]] = 0xFF0000; hover = list[k]; }
        show(hover); draw();
        if (k >= list.length) clearInterval(timer);
      }, 30);
    }
    const controls = SR.el('div', { class: 'controls' },
      SR.el('button', { type: 'button', onclick: runRect }, '첫 Render() 실행'),
      SR.el('button', { type: 'button', onclick: () => { clearInterval(timer); pixels.fill(0); hover = -1; draw(); readout.textContent = 'Clear(): 모든 칸을 0으로.'; } }, 'Clear()'),
      speed);
    speed.sync();
    host.append(SR.el('div', { class: 'demo-title' }, '화면 = width × height 칸짜리 배열'), view.c,
      SR.el('div', { class: 'readout' }, '↓ 같은 픽셀을 메모리 주소 순서로 펼친 모습 (행이 바뀔 때마다 바탕 줄무늬가 바뀜)'), mem.c, controls, readout);
    draw();
  });

  // ── 2. RGB 매크로와 DIB의 바이트 순서 ────────────────────────
  SR.register('bgr', host => {
    let r = 255, g = 64, b = 0, swap = true;
    const sw = SR.canvas(420, 110);
    const out = SR.el('div', { class: 'readout' });
    function hexByte(v) { return v.toString(16).toUpperCase().padStart(2, '0'); }
    function draw() {
      const rgbMacro = (r) | (g << 8) | (b << 16);                // Win32 RGB(): 0x00BBGGRR
      const written = swap ? ((r << 16) | (g << 8) | b) : rgbMacro; // DrawPixel이 메모리에 쓰는 값
      // 리틀 엔디안: 메모리 바이트 = [값 & 0xFF, (값>>8)&0xFF, (값>>16)&0xFF, 0]; DIB는 이를 B, G, R, X로 읽는다
      const B = written & 255, G = (written >> 8) & 255, R = (written >> 16) & 255;
      const x = sw.ctx;
      x.clearRect(0, 0, sw.w, sw.h);
      x.fillStyle = `rgb(${r},${g},${b})`; x.fillRect(0, 0, 190, 80);
      x.fillStyle = `rgb(${R},${G},${B})`; x.fillRect(230, 0, 190, 80);
      x.fillStyle = SR.css('--fg'); x.font = '13px sans-serif';
      x.fillText('내가 원한 색 RGB(r,g,b)', 0, 100); x.fillText('화면에 실제로 보이는 색', 230, 100);
      out.textContent =
        `RGB(${r}, ${g}, ${b}) 매크로 값       = ${SR.hex(rgbMacro, 8)}   (0x00BBGGRR)\n` +
        `메모리에 쓴 값 (${swap ? '변환함' : '변환 안 함'})      = ${SR.hex(written, 8)}\n` +
        `메모리 바이트 [0] [1] [2] [3]   = ${hexByte(B)} ${hexByte(G)} ${hexByte(R)} 00\n` +
        `DIB가 읽는 뜻                   =  B  G  R  -   →  화면색 (${R}, ${G}, ${B})`;
    }
    const sl = (n, v, set) => { const s = SR.slider(n, 0, 255, 1, v, val => { set(val); draw(); return val; }); s.sync(); return s; };
    host.append(SR.el('div', { class: 'demo-title' }, 'RGB()로 만든 색을 그대로 쓰면?'), sw.c,
      SR.el('div', { class: 'controls' }, sl('R', r, v => r = v), sl('G', g, v => g = v), sl('B', b, v => b = v),
        SR.toggle('BGR→RGB 변환', swap, v => { swap = v; draw(); })), out);
    draw();
  });

  // ── 3. 더블 버퍼 ──────────────────────────────────────────────
  SR.register('double-buffer', host => {
    const W = 360, H = 200;
    const screen = SR.canvas(W, H);
    const back = document.createElement('canvas'); back.width = W; back.height = H;
    const bctx = back.getContext('2d');
    let dbl = false, t = 0, row = 0, phase = 'clear';
    const readout = SR.el('div', { class: 'readout' });
    // 느린 렌더러: 한 틱에 몇 줄씩만 그린다. 단일 버퍼면 그리는 도중의 화면이 그대로 보인다.
    function drawScene(g, y0, y1) {
      g.save(); g.beginPath(); g.rect(0, y0, W, y1 - y0); g.clip();
      for (let i = 0; i < 5; i++) {
        const cx = W / 2 + Math.cos(t * 0.9 + i * 1.3) * 120, cy = H / 2 + Math.sin(t * 1.4 + i) * 60;
        g.fillStyle = `hsl(${i * 70 + 10},80%,60%)`; g.beginPath(); g.arc(cx, cy, 26, 0, 7); g.fill();
      }
      g.restore();
    }
    const target = () => dbl ? bctx : screen.ctx;
    function tick() {
      const g = target();
      if (phase === 'clear') {               // Clear(): 화면을 먼저 검게
        g.fillStyle = '#000'; g.fillRect(0, 0, W, H); phase = 'draw'; row = 0;
      } else {
        const next = Math.min(H, row + 22);
        drawScene(g, row, next); row = next;
        if (row >= H) {                       // 프레임 완성 → Present()
          if (dbl) screen.ctx.drawImage(back, 0, 0, W, H);
          phase = 'clear'; t += 0.12;
        }
      }
      readout.textContent = dbl
        ? '더블 버퍼: 뒤 버퍼에 다 그린 뒤 Present()에서 한 번에 복사 → 완성된 프레임만 보임'
        : '단일 버퍼: 지우고(검은 화면) 위에서부터 그리는 중간 과정이 그대로 보임 → 깜빡임';
      requestAnimationFrame(tick);
    }
    host.append(SR.el('div', { class: 'demo-title' }, '그리는 도중을 보여주면 생기는 일'), screen.c,
      SR.el('div', { class: 'controls' }, SR.toggle('더블 버퍼 켜기', dbl, v => { dbl = v; })), readout);
    requestAnimationFrame(tick);
  });

  // ── 4. fps 말고 ms로 ─────────────────────────────────────────
  SR.register('fps-ms', host => {
    const P = SR.canvas(520, 240);
    const readout = SR.el('div', { class: 'readout' });
    let saved = 1;
    const marks = [30, 100, 300];
    function draw() {
      const g = P.ctx, L = 46, R = 510, T = 10, B = 205, maxMs = 40, maxF = 400;
      const X = ms => L + (ms / maxMs) * (R - L), Y = f => B - (Math.min(f, maxF) / maxF) * (B - T);
      g.clearRect(0, 0, P.w, P.h);
      g.strokeStyle = SR.css('--line'); g.fillStyle = SR.css('--muted'); g.font = '11px sans-serif'; g.lineWidth = 1;
      for (let f = 0; f <= maxF; f += 100) { g.beginPath(); g.moveTo(L, Y(f)); g.lineTo(R, Y(f)); g.stroke(); g.fillText(f + ' fps', 2, Y(f) + 4); }
      for (let ms = 0; ms <= maxMs; ms += 5) g.fillText(ms + 'ms', Math.min(X(ms) - 8, R - 28), B + 16);
      g.strokeStyle = SR.css('--accent'); g.lineWidth = 2.5; g.beginPath();
      for (let ms = 1000 / maxF; ms <= maxMs; ms += 0.05) { const f = 1000 / ms; ms === 1000 / maxF ? g.moveTo(X(ms), Y(f)) : g.lineTo(X(ms), Y(f)); }
      g.stroke();
      const lines = [];
      marks.forEach((f0, i) => {
        const ms0 = 1000 / f0, ms1 = Math.max(ms0 - saved, 0.5), f1 = 1000 / ms1;
        g.fillStyle = ['#3b82f6', '#16a34a', '#a855f7'][i];
        g.beginPath(); g.arc(X(ms0), Y(f0), 5, 0, 7); g.fill();
        g.beginPath(); g.arc(X(ms1), Y(f1), 5, 0, 7); g.fill();
        g.strokeStyle = g.fillStyle; g.lineWidth = 1.5; g.beginPath(); g.moveTo(X(ms0), Y(f0)); g.lineTo(X(ms1), Y(f1)); g.stroke();
        lines.push(`${String(f0).padStart(3)} fps (${ms0.toFixed(2).padStart(5)} ms) → ${saved} ms 줄이면 ${f1.toFixed(0).padStart(4)} fps   (+${(f1 - f0).toFixed(0)} fps)`);
      });
      readout.textContent = lines.join('\n') + '\n같은 일을 줄였는데 fps 증가폭은 처음 속도에 따라 몇 배씩 다릅니다. 그래서 이 프로젝트는 ms로 비교합니다.';
    }
    const s = SR.slider('줄인 시간', 0.25, 3, 0.25, saved, v => { saved = v; draw(); return v + ' ms'; });
    host.append(SR.el('div', { class: 'demo-title' }, '같은 1ms, 다른 fps'), P.c, SR.el('div', { class: 'controls' }, s), readout);
    s.sync();
  });
})();
