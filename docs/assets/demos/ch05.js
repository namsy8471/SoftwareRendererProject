// 5장 데모: 기울어진 사각형에 체크무늬. 원근 보정 vs 화면 선형 보간, nearest vs bilinear, 밉맵
(function () {
  SR.register('persp-texture', host => {
    const W = 360, H = 240;
    const view = SR.canvas(W, H);
    const off = document.createElement('canvas'); off.width = W; off.height = H;
    const octx = off.getContext('2d');
    const readout = SR.el('div', { class: 'readout' });
    let tilt = 25, persp = true, bilinear = false, mip = false;
    // 체크무늬 텍스처 64x64, 8x8 칸 + 밉맵
    const T = 64, levels = [];
    { const base = new Float32Array(T * T * 3);
      for (let y = 0; y < T; y++) for (let x = 0; x < T; x++) {
        const c = ((x >> 3) + (y >> 3)) & 1, i = (y * T + x) * 3;
        base[i] = c ? 0.95 : 0.12; base[i + 1] = c ? 0.55 : 0.12; base[i + 2] = c ? 0.2 : 0.22;
        if (x < 2 || y < 2) { base[i] = 0.2; base[i + 1] = 0.6; base[i + 2] = 1; }
      }
      levels.push({ s: T, d: base });
      while (levels[levels.length - 1].s > 1) {
        const p = levels[levels.length - 1], s = p.s >> 1, d = new Float32Array(s * s * 3);
        for (let y = 0; y < s; y++) for (let x = 0; x < s; x++) for (let k = 0; k < 3; k++)
          d[(y * s + x) * 3 + k] = (p.d[((2 * y) * p.s + 2 * x) * 3 + k] + p.d[((2 * y) * p.s + 2 * x + 1) * 3 + k] + p.d[((2 * y + 1) * p.s + 2 * x) * 3 + k] + p.d[((2 * y + 1) * p.s + 2 * x + 1) * 3 + k]) / 4;
        levels.push({ s, d });
      }
    }
    const REP = 4;   // uv 0..1 동안 텍스처 4번 반복
    function texel(L, x, y, k) { const s = L.s; x = ((x % s) + s) % s; y = ((y % s) + s) % s; return L.d[(y * s + x) * 3 + k]; }
    function sample(u, v, lod, out) {
      const li = mip ? Math.max(0, Math.min(levels.length - 1, Math.round(lod))) : 0, L = levels[li];
      const fx = u * REP * L.s, fy = v * REP * L.s;
      for (let k = 0; k < 3; k++) {
        if (!bilinear) out[k] = texel(L, Math.floor(fx), Math.floor(fy), k);
        else {
          const x0 = Math.floor(fx - .5), y0 = Math.floor(fy - .5), tx = fx - .5 - x0, ty = fy - .5 - y0;
          out[k] = (texel(L, x0, y0, k) * (1 - tx) + texel(L, x0 + 1, y0, k) * tx) * (1 - ty) + (texel(L, x0, y0 + 1, k) * (1 - tx) + texel(L, x0 + 1, y0 + 1, k) * tx) * ty;
        }
      }
    }
    function render() {
      // 바닥 y = -1, x ∈ [-1.5, 1.5], z ∈ [1, 7]. 카메라는 원점에서 tilt만큼 아래를 봄.
      const th = tilt * Math.PI / 180, F = W * 0.62;
      const corners = [[-1.5, 1], [1.5, 1], [1.5, 7], [-1.5, 7]].map(([x, z]) => {
        const y = -1, yc = y * Math.cos(th) + z * Math.sin(th), zc = -y * Math.sin(th) + z * Math.cos(th);
        return { sx: W / 2 + x / zc * F, sy: H * 0.35 - yc / zc * F, w: zc, u: (x + 1.5) / 3, v: (z - 1) / 6, };
      });
      const img = octx.createImageData(W, H), d = img.data, col = [0, 0, 0];
      const bg = [0.10, 0.11, 0.13];
      for (let i = 0; i < W * H; i++) { d[i * 4] = bg[0] * 255; d[i * 4 + 1] = bg[1] * 255; d[i * 4 + 2] = bg[2] * 255; d[i * 4 + 3] = 255; }
      const tris = [[0, 1, 2], [0, 2, 3]];
      for (const [i0, i1, i2] of tris) {
        const A = corners[i0], B = corners[i1], C = corners[i2];
        const area = (B.sx - A.sx) * (C.sy - A.sy) - (B.sy - A.sy) * (C.sx - A.sx);
        if (Math.abs(area) < 1e-6) continue;
        const minX = Math.max(0, Math.floor(Math.min(A.sx, B.sx, C.sx))), maxX = Math.min(W - 1, Math.ceil(Math.max(A.sx, B.sx, C.sx)));
        const minY = Math.max(0, Math.floor(Math.min(A.sy, B.sy, C.sy))), maxY = Math.min(H - 1, Math.ceil(Math.max(A.sy, B.sy, C.sy)));
        for (let y = minY; y <= maxY; y++) for (let x = minX; x <= maxX; x++) {
          const px = x + .5, py = y + .5;
          const b0 = ((B.sx - px) * (C.sy - py) - (B.sy - py) * (C.sx - px)) / area;
          const b1 = ((C.sx - px) * (A.sy - py) - (C.sy - py) * (A.sx - px)) / area;
          const b2 = 1 - b0 - b1;
          if (b0 < 0 || b1 < 0 || b2 < 0) continue;
          let u, v, q = b0 / A.w + b1 / B.w + b2 / C.w;
          if (persp) { u = (b0 * A.u / A.w + b1 * B.u / B.w + b2 * C.u / C.w) / q; v = (b0 * A.v / A.w + b1 * B.v / B.w + b2 * C.v / C.w) / q; }
          else { u = b0 * A.u + b1 * B.u + b2 * C.u; v = b0 * A.v + b1 * B.v + b2 * C.v; }
          // LOD: 픽셀 하나가 덮는 텍셀 수(대략) = 1/q에 비례
          const zc = 1 / q, texPerPix = (REP * T / 3) * zc / F;   // 가로 방향 텍셀/픽셀
          sample(u, v, Math.log2(Math.max(1, texPerPix)) + 0.5 * Math.log2(1 + zc), col);
          const o = (y * W + x) * 4; d[o] = col[0] * 255; d[o + 1] = col[1] * 255; d[o + 2] = col[2] * 255;
        }
      }
      octx.putImageData(img, 0, 0);
      view.ctx.drawImage(off, 0, 0, W, H);
      readout.textContent = (persp ? '원근 보정: u/w, v/w, 1/w를 보간한 뒤 나눔 → 칸이 멀어질수록 고르게 작아짐' : '화면 선형 보간: uv를 그대로 보간 → 두 삼각형의 대각선을 따라 무늬가 꺾임') +
        `\n필터: ${bilinear ? 'bilinear (4텍셀 평균)' : 'nearest (가장 가까운 텍셀)'}   밉맵: ${mip ? '켬' : '끔 (먼 곳이 지글거림)'}`;
    }
    const s = SR.slider('내려다보는 각', 5, 55, 1, tilt, v => { tilt = v; render(); return v + '°'; });
    host.append(SR.el('div', { class: 'demo-title' }, '바닥에 깐 체크무늬'), view.c,
      SR.el('div', { class: 'controls' }, s,
        SR.toggle('원근 보정', persp, v => { persp = v; render(); }),
        SR.toggle('bilinear', bilinear, v => { bilinear = v; render(); }),
        SR.toggle('밉맵', mip, v => { mip = v; render(); })), readout);
    s.sync();
  });
})();
