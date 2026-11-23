// 교과서 데모 공통 도우미. 각 장의 스크립트가 SR.register(이름, 함수)로 데모를 등록하고,
// 본문의 <div class="demo" data-demo="이름">에 붙는다.
(function () {
  const registry = {};
  const SR = {
    register(name, fn) { registry[name] = fn; },
    css(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); },
    el(tag, attrs, ...kids) {
      const e = document.createElement(tag);
      for (const [k, v] of Object.entries(attrs || {})) {
        if (k === 'class') e.className = v;
        else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
        else e.setAttribute(k, v);
      }
      for (const kid of kids) e.append(kid);
      return e;
    },
    // CSS 크기(w×h)로 보이고 devicePixelRatio만큼 선명한 canvas
    canvas(w, h) {
      const c = document.createElement('canvas');
      const r = window.devicePixelRatio || 1;
      c.width = Math.round(w * r); c.height = Math.round(h * r);
      c.style.width = w + 'px'; c.style.aspectRatio = w + ' / ' + h;
      const ctx = c.getContext('2d'); ctx.scale(r, r);
      return { c, ctx, w, h };
    },
    // 이벤트 좌표를 canvas의 CSS 좌표로 (화면이 좁아 줄어든 경우 포함)
    pos(c, ev, w, h) {
      const b = c.getBoundingClientRect();
      return { x: (ev.clientX - b.left) * w / b.width, y: (ev.clientY - b.top) * h / b.height };
    },
    slider(label, min, max, step, value, oninput) {
      const out = SR.el('span', { class: 'val' });
      const inp = SR.el('input', { type: 'range', min, max, step, value });
      const sync = () => { out.textContent = oninput(Number(inp.value)) ?? inp.value; };
      inp.addEventListener('input', sync);
      const wrap = SR.el('label', {}, label + ' ', inp, ' ', out);
      wrap.sync = sync; wrap.input = inp;
      return wrap;
    },
    toggle(label, on, onchange) {
      const b = SR.el('button', { type: 'button' }, label);
      const set = v => { on = v; b.classList.toggle('on', v); onchange(v); };
      b.addEventListener('click', () => set(!on));
      b.set = set; queueMicrotask(() => set(on));
      return b;
    },
    hex(n, digits) { return '0x' + (n >>> 0).toString(16).toUpperCase().padStart(digits, '0'); },
  };
  window.SR = SR;
  window.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('.demo[data-demo]').forEach(host => {
      const fn = registry[host.dataset.demo];
      if (!fn) return;
      host.querySelectorAll('.demo-fallback').forEach(n => n.remove());
      try { fn(host); } catch (e) { host.append(SR.el('div', { class: 'readout' }, '데모 오류: ' + e.message)); }
    });
  });
})();
