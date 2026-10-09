// The browser plays the sketch's part for each board: buttons, steps, screen timeout, always-on view,
// storage and the radio. A second board can be added to try trading: it runs its own copy of the code,
// with its own hero, and the two talk through a pretend radio.
const W = 368, H = 448;
const $ = id => document.getElementById(id);
const store = {
  get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch (e) {} },
  del(k) { try { localStorage.removeItem(k); } catch (e) {} },
};
const toB64 = u8 => { let s = ""; for (const b of u8) s += String.fromCharCode(b); return btoa(s); };
const fromB64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));
const WASM_BIN = fromB64(WASM);
const now = () => (performance.now() | 0) >>> 0;
const boards = [];

class Board {
  constructor(index, el) {
    this.index = index; this.el = el;
    this.prefix = index ? `prpg.b${index + 1}.` : "prpg.";   // board 1 keeps the original keys
    this.keys = ["hero", "settings", "game"].map(k => this.prefix + k);
    this.mac = [0x50, 0x52, 0x47, 0, 0, index + 1];
    this.cv = el.querySelector("canvas"); this.ctx = this.cv.getContext("2d"); this.img = this.ctx.createImageData(W, H);
    this.state = "on"; this.lastActivity = 0; this.needDraw = true; this.ambientDrawnAt = 0; this.stepsAt = 0;
    this.brightness = 255; this.radioOn = false; this.down = null; this.dayOffset = 0; this.steps = 0;
  }
  out(name) { return this.el.querySelector(`[data-out="${name}"]`); }
  bytes() { return new Uint8Array(this.ex.memory.buffer); }
  async start() {
    const b = this;
    const env = {
      js_save(kind, p, len) { store.set(b.keys[kind], toB64(b.bytes().slice(p, p + len))); },
      js_erase() { b.keys.forEach(store.del); store.del(b.prefix + "steps"); b.steps = 0; },
      js_random(n) { return Math.floor(Math.random() * n) >>> 0; },
      js_brightness(level) { b.brightness = level; b.applyLook(); },
      js_radio(on) { b.radioOn = !!on; b.applyLook(); },
      js_radio_send(macPtr, p, len) {
        const to = macPtr ? Array.from(b.bytes().slice(macPtr, macPtr + 6)) : null;
        const data = b.bytes().slice(p, p + len);
        for (const o of boards) {
          if (o === b || !o.ex || !o.radioOn) continue;
          if (to && to.join() !== o.mac.join()) continue;
          if (Math.random() < (window.radioDrop || 0)) continue;   // testing: lose some messages (radioDrop 0..1)
          if (window.radioFilter && !window.radioFilter(b.index, data)) continue;   // testing: drop chosen messages
          setTimeout(() => o.receive(b.mac, data), 20 + Math.random() * 30);   // a little air time
        }
      },
    };
    const wasi = new Proxy({}, { get: () => () => 0 });   // the C library's file calls are never used
    const { instance } = await WebAssembly.instantiate(WASM_BIN, { env, wasi_snapshot_preview1: wasi });
    this.ex = instance.exports;
    this.ex._initialize();
    this.ex.web_splash(); this.blit();   // the start-up picture, like the board
    this.el.querySelector(".loading").hidden = true;
    const splashUntil = performance.now() + 1500;
    const load = (kind, ptr, size, ...older) => {   // older: shorter sizes from earlier versions (new fields stay 0)
      const s = store.get(this.keys[kind]); if (!s) return false;
      const d = fromB64(s); if (d.length !== size && !older.includes(d.length)) return false;
      this.bytes().fill(0, ptr, ptr + size);   // an older, shorter save leaves the new fields at 0
      this.bytes().set(d, ptr); return true;
    };
    load(1, this.ex.web_settings_buf(), this.ex.web_settings_size());
    const haveHero = load(0, this.ex.web_hero_buf(), this.ex.web_hero_size());
    const haveGame = load(2, this.ex.web_game_buf(), this.ex.web_game_size(), this.ex.web_game_old_size(), this.ex.web_game_v2_size());
    this.ex.web_begin(haveHero ? 1 : 0, haveGame ? 1 : 0);
    this.ex.web_set_battery(80, 0, 0);
    this.brightness = this.ex.web_brightness();
    this.loadSteps(); this.feedSteps();
    await new Promise(r => setTimeout(r, Math.max(0, splashUntil - performance.now())));
    this.wire();
    this.setState("on");
  }
  receive(mac, data) {
    if (!this.radioOn) return;
    const buf = this.ex.web_radio_buf();
    const m = this.bytes(); m.set(mac, buf); m.set(data, buf + 6);
    if (this.ex.web_radio_receive(data.length)) this.needDraw = true;
  }
  today() {
    const d = new Date(Date.now() + this.dayOffset * 86400000);
    return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
  }
  loadSteps() {
    try { const s = JSON.parse(store.get(this.prefix + "steps") || "{}"); this.dayOffset = s.off || 0; this.steps = s.day === this.today() ? s.steps || 0 : 0; } catch (e) { this.steps = 0; }
  }
  saveSteps() { store.set(this.prefix + "steps", JSON.stringify({ day: this.today(), steps: this.steps, off: this.dayOffset })); }
  feedSteps() { this.ex.web_set_steps(this.steps, this.today()); this.needDraw = true; this.applyLook(); }
  applyLook() {
    if (!this.ex) return;
    const level = this.state === "ambient" ? 40 : this.brightness;
    this.cv.style.filter = `brightness(${(0.35 + 0.65 * level / 255).toFixed(2)})`;
    this.cv.style.transform = this.ex.web_upside_down() ? "rotate(180deg)" : "";
    const s = this.state === "on" ? "Screen on" : this.state === "ambient" ? "Always-on view" : "Screen off";
    this.out("state").textContent = this.radioOn ? s + " · radio on" : s;
    this.out("steps").textContent = `${this.steps.toLocaleString("en-US")} steps today`;
  }
  draw() {
    if (this.state === "off") { this.ctx.fillStyle = "#000"; this.ctx.fillRect(0, 0, W, H); return; }
    this.ex.web_draw(now());
    this.blit();
  }
  blit() {
    const fb = new Uint16Array(this.ex.memory.buffer, this.ex.web_fb(), W * H), d = this.img.data;
    for (let i = 0, j = 0; i < W * H; i++, j += 4) {
      const c = fb[i];
      d[j] = (c >> 11) << 3 | (c >> 13); d[j + 1] = ((c >> 5) & 63) << 2 | ((c >> 9) & 3); d[j + 2] = (c & 31) << 3 | ((c >> 2) & 7); d[j + 3] = 255;
    }
    this.ctx.putImageData(this.img, 0, 0);
  }
  setState(s) {
    if (s === "off") this.ex.web_screen_off();
    this.state = s;
    this.ex.web_ambient(s === "ambient" ? 1 : 0);
    if (s === "ambient") this.ambientDrawnAt = now();
    this.lastActivity = now(); this.needDraw = true; this.applyLook();
  }
  toScreen(e) {
    const r = this.cv.getBoundingClientRect();
    let x = Math.round((e.clientX - r.left) / r.width * W), y = Math.round((e.clientY - r.top) / r.height * H);
    if (this.ex.web_upside_down()) { x = W - 1 - x; y = H - 1 - y; }   // the CSS turn already flips what you see
    return [x, y];
  }
  wire() {
    const b = this, act = a => this.el.querySelector(`[data-act="${a}"]`);
    this.cv.addEventListener("pointerdown", e => { if (b.state !== "on") return; b.down = b.toScreen(e); b.last = null; this.cv.setPointerCapture?.(e.pointerId); b.lastActivity = now(); if (b.ex.web_touch_raw(...b.down, 1)) b.needDraw = true; });
    this.cv.addEventListener("pointermove", e => { if (!b.down || b.state !== "on") return; const [x, y] = b.toScreen(e); b.last = [x, y]; if (b.ex.web_touch_raw(x, y, 1)) b.needDraw = true; });
    this.cv.addEventListener("pointerup", () => {
      if (!b.down || b.state !== "on") { b.down = null; return; }
      const [x, y] = b.down; b.down = null; b.lastActivity = now();
      const [lx, ly] = b.last || [x, y]; b.last = null;
      if (b.ex.web_touch_raw(x, y, 0)) b.needDraw = true;
      const dx = lx - x, dy = ly - y;
      if (Math.abs(dx) > 50 && Math.abs(dx) > 2 * Math.abs(dy)) { if (b.ex.web_swipe(dx < 0 ? 1 : -1)) b.needDraw = true; }   // sideways swipe
      else if (b.ex.web_tap(x, y)) b.needDraw = true;
      b.applyLook();
    });
    act("boot").addEventListener("click", () => {
      if (b.state !== "on") return b.setState("on");
      b.ex.web_settings_button(); b.lastActivity = now(); b.needDraw = true;
    });
    act("pwr").addEventListener("click", () => {
      if (b.state === "off" || b.state === "ambient") return b.setState("on");
      b.setState(b.ex.web_always_on() ? "ambient" : "off");
    });
    const walk = n => () => { b.steps += n; b.saveSteps(); b.feedSteps(); };
    act("walk200").addEventListener("click", walk(200));
    act("walk2000").addEventListener("click", walk(2000));
    act("nextDay").addEventListener("click", () => { b.dayOffset++; b.steps = 0; b.saveSteps(); b.feedSteps(); });
  }
  // the same order as the sketch's loop()
  frame(t) {
    if (!this.ex) return;
    if (t - this.stepsAt > 2000) { this.stepsAt = t; this.ex.web_set_steps(this.steps, this.today()); if (this.state !== "off") this.needDraw = true; }
    if (this.state === "on") {
      if (this.ex.web_tick(t)) this.needDraw = true;
      if (!this.down && !this.ex.web_keep_awake() && t - this.lastActivity > this.ex.web_timeout() * 1000) this.setState(this.ex.web_always_on() ? "ambient" : "off");
    } else {
      this.ex.web_tick(t);   // the radio keeps going even when nothing is drawn
      if (this.state === "ambient" && t - this.ambientDrawnAt > 30000) { this.ambientDrawnAt = t; this.needDraw = true; }
    }
    if (this.needDraw) { this.needDraw = false; this.draw(); }
  }
}

function loop() { const t = now(); for (const b of boards) b.frame(t); requestAnimationFrame(loop); }

async function addBoard() {
  const index = boards.length;
  let el = document.querySelector(`[data-board="${index}"]`);
  if (!el) {   // copy board 1's markup
    el = document.querySelector('[data-board="0"]').cloneNode(true);
    el.dataset.board = index;
    el.querySelector(".bname").textContent = `Board ${index + 1}`;
    el.querySelector("canvas").setAttribute("aria-label", `Board ${index + 1} screen. Tap to use.`);
    el.querySelector(".loading").hidden = false;
    $("boards").appendChild(el);
    $("layout").classList.add("two");
  }
  const b = new Board(index, el);
  boards.push(b);
  await b.start();
  return b;
}

$("second").addEventListener("click", async () => {
  if (boards.length >= 2) return;
  $("second").disabled = true;
  store.set("prpg.twoBoards", "1");
  await addBoard();
  $("second").textContent = "Second board added";
});
let resetArmed = false;
$("reset").addEventListener("click", () => {
  if (!resetArmed) { resetArmed = true; $("resetAsk").hidden = false; return; }
  resetArmed = false; $("resetAsk").hidden = true;
  for (const b of boards) { b.ex.web_screen_off(); b.keys.forEach(store.del); store.del(b.prefix + "steps"); b.steps = 0; b.dayOffset = 0; b.ex.web_begin(0, 0); b.setState("on"); b.feedSteps(); }
});

(async () => {
  await addBoard();
  if (store.get("prpg.twoBoards") === "1") { $("second").disabled = true; $("second").textContent = "Second board added"; await addBoard(); }
  requestAnimationFrame(loop);
})().catch(e => { document.querySelector(".loading").textContent = "The board code did not start: " + e.message; });
