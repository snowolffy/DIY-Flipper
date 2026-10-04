// app.js - the emulator window. Everything it changes goes through /api/cmd as the same command objects the
// terminal sends; everything it shows comes from /api/state and /api/frame.
(() => {
  const qs = new URLSearchParams(location.search);
  const TOKEN = qs.get("token") || "";
  const SHOT = qs.get("shot") === "1";  // sim shot-ui: render once, don't count as an open window
  const $ = (id) => document.getElementById(id);
  const el = (tag, attrs = {}, ...kids) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === "class") e.className = v; else if (k === "text") e.textContent = v; else e.setAttribute(k, v);
    }
    for (const k of kids) e.append(k);
    return e;
  };
  const headers = () => {
    const h = { "X-Sim-Token": TOKEN, "Content-Type": "application/json" };
    if (!SHOT) h["X-Sim-Window"] = "1";
    return h;
  };
  const api = async (path, body, method) => {
    const r = await fetch("/api/" + path, { method: method || (body === undefined ? "GET" : "POST"), headers: headers(),
      body: body === undefined ? undefined : (typeof body === "string" ? body : JSON.stringify(body)) });
    const ct = r.headers.get("content-type") || "";
    return ct.includes("json") ? r.json() : r.arrayBuffer();
  };
  const cmd = (c) => api("cmd", c).then((r) => { if (r && r.state) applyState(r.state, false); return r; });

  let S = null;  // last full state
  const BUTTONS = ["OK", "CANCEL", "LEFT", "RIGHT", "POWER"];
  const DEFAULT_KEYS = { OK: "z", CANCEL: "x", LEFT: "ArrowLeft", RIGHT: "ArrowRight", POWER: "p" };
  let keys = { ...DEFAULT_KEYS };
  const keyLabel = (k) => ({ ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓", " ": "Space" }[k] || k.toUpperCase());

  // ---------------- screen ----------------
  const canvas = $("screen"), ctx2d = canvas.getContext("2d"), img = ctx2d.createImageData(128, 160);
  let zoom = localStorage.getItem("zoom") || "3";
  const applyZoom = () => {
    let z = zoom === "fit" ? Math.max(1, Math.floor(Math.min(($("screenBox").parentElement.clientWidth - 80) / 128,
      (document.querySelector(".device").clientHeight - 220) / 160))) : +zoom;
    canvas.style.width = 128 * z + "px";
    canvas.style.height = 160 * z + "px";
  };
  $("zoomSel").value = zoom;
  $("zoomSel").onchange = (e) => { zoom = e.target.value; try { localStorage.setItem("zoom", zoom); } catch {} applyZoom(); };
  window.addEventListener("resize", applyZoom);
  applyZoom();

  async function drawFrame() {
    const buf = new Uint8Array(await api("frame"));
    if (buf.length < 128 * 160 * 2) return;
    const px = img.data;
    for (let i = 0; i < 128 * 160; i++) {
      const c = buf[2 * i] | (buf[2 * i + 1] << 8);
      const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
      px[4 * i] = (r << 3) | (r >> 2); px[4 * i + 1] = (g << 2) | (g >> 4); px[4 * i + 2] = (b << 3) | (b >> 2); px[4 * i + 3] = 255;
    }
    ctx2d.putImageData(img, 0, 0);
    const bl = buf[128 * 160 * 2];  // backlight 0-255: the GUI dims the panel like the real one
    canvas.style.filter = bl >= 255 ? "" : `brightness(${Math.max(0.04, bl / 255)})`;
  }

  // ---------------- buttons ----------------
  const locked = new Set(), down = new Set();
  const setBtn = (b, isDown) => {
    if (isDown === down.has(b)) return;
    if (isDown) down.add(b); else down.delete(b);
    cmd({ type: "button", button: b, action: isDown ? "down" : "up" });
    paintButtons();
  };
  const paintButtons = () => {
    document.querySelectorAll(".key").forEach((k) => {
      const b = k.dataset.btn;
      k.classList.toggle("down", down.has(b));
      k.classList.toggle("locked", locked.has(b));
    });
    const held = [...down];
    $("heldText").innerHTML = held.length
      ? "Holding " + held.map((b) => `<b>${b}</b>`).join(" + ") + (locked.size ? " (locked: right-click to release)" : "") +
        " — press another button for a combo."
      : "Keyboard: hold keys together for a combo. Mouse: right-click a button to lock it held.";
  };
  document.querySelectorAll(".key").forEach((k) => {
    const b = k.dataset.btn;
    k.addEventListener("mousedown", (e) => { if (e.button === 0 && !locked.has(b)) setBtn(b, true); });
    k.addEventListener("mouseup", (e) => { if (e.button === 0 && !locked.has(b)) setBtn(b, false); });
    k.addEventListener("mouseleave", () => { if (down.has(b) && !locked.has(b) && !keyDown.has(b)) setBtn(b, false); });
    k.addEventListener("contextmenu", (e) => {
      e.preventDefault();
      if (locked.has(b)) { locked.delete(b); setBtn(b, false); } else { locked.add(b); setBtn(b, true); }
      paintButtons();
    });
  });
  const keyDown = new Set();
  const typing = () => { const a = document.activeElement; return a && (a.tagName === "INPUT" || a.tagName === "SELECT" || a.tagName === "TEXTAREA"); };
  window.addEventListener("keydown", (e) => {
    if (typing() || e.repeat || $("keysDlg").open) return;
    const b = BUTTONS.find((x) => keys[x] && keys[x].toLowerCase() === e.key.toLowerCase());
    if (!b) return;
    e.preventDefault();
    keyDown.add(b);
    setBtn(b, true);
  });
  window.addEventListener("keyup", (e) => {
    const b = BUTTONS.find((x) => keys[x] && keys[x].toLowerCase() === e.key.toLowerCase());
    if (!b || !keyDown.has(b)) return;
    keyDown.delete(b);
    if (!locked.has(b)) setBtn(b, false);
  });
  window.addEventListener("blur", () => { for (const b of [...keyDown]) { keyDown.delete(b); if (!locked.has(b)) setBtn(b, false); } });
  const paintCaps = () => document.querySelectorAll(".cap").forEach((c) => (c.textContent = keyLabel(keys[c.dataset.cap] || "?")));

  // edit keys
  $("editKeysBtn").onclick = () => {
    const list = $("keysList");
    list.replaceChildren();
    for (const b of BUTTONS) {
      const btn = el("button", { class: "btn", type: "button", text: keyLabel(keys[b]) });
      btn.onclick = () => {
        btn.textContent = "press a key…";
        const h = (e) => { e.preventDefault(); keys[b] = e.key; btn.textContent = keyLabel(e.key); window.removeEventListener("keydown", h, true); save(); };
        window.addEventListener("keydown", h, true);
      };
      list.append(el("div", { class: "item" }, el("span", { text: b }), btn));
    }
    $("keysDlg").showModal();
  };
  const save = () => { api("keys", keys); paintCaps(); };
  $("keysReset").onclick = (e) => { e.preventDefault(); keys = { ...DEFAULT_KEYS }; save(); $("keysDlg").close(); };

  // ---------------- header ----------------
  $("pauseBtn").onclick = () => api("pause", {});
  $("restartBtn").onclick = () => cmd({ type: "restart", cold_boot: false });
  $("coldBtn").onclick = () => cmd({ type: "restart", cold_boot: true });
  $("projectBtn").onclick = () => alert("This window shows the session of\n" + (S ? S.project : "") +
    "\n\nTo work on another folder close this window and run:\n  sim gui --project <folder>");
  $("switchSel").onchange = (e) => cmd({ type: "power_switch", on: e.target.value === "on" });
  $("savePngBtn").onclick = () => {
    const a = el("a", { href: `/api/screen.png?scale=${zoom === "fit" ? 4 : zoom}&token=${TOKEN}`, download: "screen.png" });
    document.body.append(a); a.click(); a.remove();
  };

  // ---------------- tabs ----------------
  let tab = "hw";
  try { tab = localStorage.getItem("tab") || "hw"; } catch {}
  const showTab = (t) => {
    tab = t;
    try { localStorage.setItem("tab", t); } catch {}
    document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("on", b.dataset.tab === t));
    document.querySelectorAll(".tab").forEach((d) => d.classList.toggle("hidden", d.id !== "tab-" + t));
    if (t === "files") loadFiles();
  };
  document.querySelectorAll("#tabs button").forEach((b) => (b.onclick = () => showTab(b.dataset.tab)));

  // Hardware
  const pinOf = (sig) => { const p = S && S.board.pins.find((x) => x.signal.startsWith(sig)); return p ? (p.gpio < 0 ? "not fitted" : "GPIO " + p.gpio) : "?"; };
  $("batRange").oninput = (e) => ($("batVal").textContent = e.target.value + "%");
  $("batRange").onchange = (e) => { $("batNone").checked = false; cmd({ type: "battery", percent: +e.target.value }); };
  $("batNone").onchange = (e) => cmd({ type: "battery", percent: e.target.checked ? "unknown" : +$("batRange").value });
  $("usbChk").onchange = $("usbUnknown").onchange = () =>
    cmd({ type: "usb", plugged: $("usbUnknown").checked ? "unknown" : $("usbChk").checked });
  $("sdChk").onchange = (e) => cmd({ type: "sd", present: e.target.checked });
  $("sdFail").onchange = (e) => cmd({ type: "storage", fail_writes: e.target.checked });
  $("rtcMissing").onchange = (e) => e.target.checked ? cmd({ type: "rtc", missing: true }) : cmd({ type: "rtc", time: "pc" });
  $("rtcPc").onclick = () => cmd({ type: "rtc", time: "pc" });

  // IR
  $("irPress").onclick = () => cmd({ type: "ir_signal", protocol: $("irProto").value, address: $("irAddr").value, command: $("irCmd").value });
  $("irRaw").onclick = () => $("irRawFile").click();
  $("irRawFile").onchange = async (e) => {
    const f = e.target.files[0]; if (!f) return;
    const raw = (await f.text()).split(/[\s,]+/).filter((x) => /^\d+$/.test(x)).map(Number);
    cmd({ type: "ir_signal", protocol: "RAW", raw });
    e.target.value = "";
  };
  let irClearedAt = 0;
  $("irClear").onclick = () => { irClearedAt = S ? S.ir.sent_count : 0; render(); };

  // NFC
  const hex = (s) => s.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
  $("nfcPlace").onclick = async () => {
    const dump = $("nfcDump").value;
    if (dump) {
      cmd({ type: "nfc_card", action: "present", dump, magic: $("nfcMagic").checked });
      return;
    }
    const type = $("nfcType").value, n = Math.max(0, Math.min(256, +$("nfcBlocks").value || 0));
    const uid = hex($("nfcUid").value);
    const blocks = type.includes("Classic") ? Array.from({ length: n }, (_, i) => (i === 0 ? (uid + "0".repeat(32)).slice(0, 32) : "00".repeat(16))) : [];
    cmd({ type: "nfc_card", action: "present", uid, card_type: type, blocks, magic: $("nfcMagic").checked });
  };
  $("nfcRemove").onclick = () => cmd({ type: "nfc_card", action: "remove" });
  $("nfcTap").onclick = () => cmd({ type: "nfc_reader" });
  $("nfcModule").onchange = (e) => cmd({ type: "nfc_module", present: e.target.checked });

  // WiFi
  let nets = null;
  const sendNets = () => cmd({ type: "wifi", networks: nets });
  const paintNets = () => {
    const box = $("wifiNets");
    box.replaceChildren();
    nets.forEach((n, i) => {
      const name = el("input", { class: "n", type: "text", "aria-label": "Network name" }); name.value = n.ssid;
      const dbm = el("input", { class: "d mono", type: "text", "aria-label": "Signal dBm" }); dbm.value = n.rssi;
      const lock = el("input", { type: "checkbox" }); lock.checked = n.secured;
      const del = el("button", { class: "btn xs", type: "button", text: "Remove" });
      name.onchange = () => { n.ssid = name.value; sendNets(); };
      dbm.onchange = () => { n.rssi = +dbm.value || -60; sendNets(); };
      lock.onchange = () => { n.secured = lock.checked; sendNets(); };
      del.onclick = () => { nets.splice(i, 1); sendNets(); paintNets(); };
      box.append(el("div", { class: "netrow" }, name, dbm, el("label", { class: "row gap6" }, lock, "Locked"), del));
    });
  };
  $("wifiAdd").onclick = () => { nets.push({ ssid: "New network", rssi: -60, secured: true }); sendNets(); paintNets(); };
  $("wifiOk").onchange = (e) => cmd({ type: "wifi", next_connect_succeeds: e.target.checked });
  $("wifiNtp").onchange = (e) => cmd({ type: "wifi", ntp_responds: e.target.checked });
  $("wifiDelay").oninput = (e) => ($("wifiDelayText").textContent = `Delay ${e.target.value} ms`);
  $("wifiDelay").onchange = (e) => cmd({ type: "wifi", latency_ms: +e.target.value });
  $("wifiDrop").onclick = () => cmd({ type: "wifi", drop: true });

  // Bluetooth
  $("btConnect").onclick = () => cmd({ type: "ble_host", action: "connect", name: $("btHost").value || "Phone" });
  $("btDisconnect").onclick = () => cmd({ type: "ble_host", action: "disconnect" });
  $("btFull").onchange = (e) => cmd({ type: "ble_bonds", full: e.target.checked });

  // Files
  async function loadFiles() {
    const f = await api("files");
    $("filesRoot").textContent = f.root;
    const lines = [];
    const walk = (nodes, pre) => nodes.forEach((n) => {
      lines.push(pre + n.name + (n.children ? "/" : "") + (n.size !== undefined ? `  ${n.size} B` : ""));
      if (n.children) walk(n.children, pre + "  ");
    });
    walk(f.tree, "");
    $("filesTree").textContent = lines.join("\n") || "(empty)";
    // saved NFC dumps for the NFC tab
    const sd = f.tree.find((n) => n.name === "sd"), nfc = sd && sd.children && sd.children.find((n) => n.name === "nfc");
    const sel = $("nfcDump"), cur = sel.value;
    sel.replaceChildren(el("option", { value: "", text: "— none —" }));
    for (const d of (nfc && nfc.children) || []) sel.append(el("option", { value: "sd:/nfc/" + d.name, text: "sd:/nfc/" + d.name }));
    sel.value = cur;
  }
  $("openFolder").onclick = () => api("open-folder", {});
  $("resetSeed").onclick = async () => {
    const seed = $("seedSel").value;
    if (!confirm(`Empty the device storage${seed ? " and copy seeds/" + seed + " in" : ""}, then cold boot?`)) return;
    await api("reset-storage", { seed });
    loadFiles();
  };
  const doImport = async (file) => {
    const buf = await file.arrayBuffer();
    const r = await fetch(`/api/import?name=${encodeURIComponent(file.name)}`, { method: "POST", headers: { "X-Sim-Token": TOKEN, "X-Sim-Window": "1" }, body: buf });
    const j = await r.json();
    $("importResult").textContent = j.ok ? `Imported ${file.name}: ${j.info}` : `Not imported: ${j.error}`;
    loadFiles();
  };
  $("importBtn").onclick = () => $("importFile").click();
  $("importFile").onchange = (e) => { if (e.target.files[0]) doImport(e.target.files[0]); e.target.value = ""; };
  const dz = $("dropZone");
  dz.ondragover = (e) => { e.preventDefault(); dz.classList.add("over"); };
  dz.ondragleave = () => dz.classList.remove("over");
  dz.ondrop = (e) => { e.preventDefault(); dz.classList.remove("over"); if (e.dataTransfer.files[0]) doImport(e.dataTransfer.files[0]); };

  // Scripts
  let selScript = null;
  $("runHere").onclick = () => selScript && api("script/run", { name: selScript, restart: $("restartFirst").checked });
  $("runAll").onclick = async () => { $("scriptDetail").textContent = "running every script headless…"; await api("script/run-all", {}); $("scriptDetail").textContent = ""; };
  $("stopScript").onclick = () => api("script/stop", {});
  $("recordBtn").onclick = async () => {
    if (S && S.recording) {
      const name = prompt("Save the recording as script:", "recorded");
      if (!name) return;
      const r = await api("record", { start: false, name });
      $("scriptDetail").textContent = r.ok ? "saved " + r.path : r.error;
    } else {
      await api("record", { start: true });
    }
  };
  $("saveLog").onclick = async () => {
    const name = prompt("Save the command log as script:", "session");
    if (!name) return;
    const r = await api("save-log", { name });
    alert(r.ok ? "Saved " + r.path : "Not saved");
  };

  // ---------------- state -> page ----------------
  const fmtUp = (ms) => { const s = Math.floor(ms / 1000); return [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60].map((x) => String(x).padStart(2, "0")).join(":"); };
  const cell = (k, v) => el("div", { class: "cell" }, el("div", { class: "k", text: k }), el("div", { class: "mono", text: v }));
  let lastLogLen = -1;

  function applyState(s, full) {
    if (full) S = s; else if (S) Object.assign(S, s); else return;
    render();
  }

  function render() {
    const s = S;
    if (!s) return;
    $("projectPath").textContent = s.project;
    $("boardName").textContent = `${s.board.name} · ${s.board.display.replace("x", "×")}`;
    $("pathUptime").textContent = `${s.menu_path || s.screen || "—"} · up ${fmtUp(s.uptime_ms)}`;
    $("pauseBtn").textContent = s.paused ? "Resume" : "Pause";
    $("pauseBtn").classList.toggle("on", s.paused);
    $("hashText").textContent = `16-bit color · hash ${s.display.hash}`;
    $("switchSel").value = s.power.switch_on ? "on" : "off";
    // the firmware may hold or release buttons the window didn't (scripts): show the real levels
    for (const b of BUTTONS) {
      const really = s.held.includes(b);
      if (really !== down.has(b)) { if (really) down.add(b); else { down.delete(b); locked.delete(b); } }
    }
    paintButtons();

    // hardware
    $("pinBattery").textContent = `LiPo 1000 mAh · ADC ${pinOf("Battery ADC")}`;
    $("pinUsb").textContent = `TP4056 · sense ${pinOf("USB sense")}`;
    $("pinSd").textContent = `SPI module · CS ${pinOf("SD CS")}`;
    if (document.activeElement !== $("batRange")) {
      const p = s.battery.set_percent;
      $("batNone").checked = p < 0;
      if (p >= 0) { $("batRange").value = p; $("batVal").textContent = p + "%"; }
    }
    $("usbUnknown").checked = s.power.usb === "unknown";
    $("usbChk").checked = s.power.usb === true;
    $("sdChk").checked = s.sd.present;
    $("sdFail").checked = s.sd.fail_writes;
    $("rtcMissing").checked = s.rtc === null;
    $("rtcText").textContent = s.rtc ? s.rtc.replace("T", " ") : "no time";
    const g = $("drivenGrid");
    g.replaceChildren(
      cell(`Backlight · PWM ${pinOf("TFT BLK")}`, Math.round(s.backlight * 100 / 255) + "%"),
      cell(`Buzzer · KY-006 ${pinOf("Buzzer")}`, s.buzzer.sounding_hz ? s.buzzer.sounding_hz + " Hz" : "silent"),
      cell(`IR · TX ${pinOf("IR TX")} / RX ${pinOf("IR RX")}`, s.ir.listening ? "listening" : "idle"),
      cell("NFC · PN532 I2C 0x24", !s.nfc.module ? "not answering" : s.nfc.emulating ? "emulating" : s.nfc.polling ? "polling" : "not polling"),
      cell("WiFi", s.wifi.state + (s.wifi.ssid ? " · " + s.wifi.ssid : "")),
      cell("Bluetooth", s.ble.state + (s.ble.host ? " · " + s.ble.host : "")),
      cell("Display pushes", `${s.display.fps} fps · ${s.display.last_push_ms.toFixed(1)} ms/push`),
      cell("Power state", s.power.state + (s.power.firmware !== "awake" ? " · " + s.power.firmware.replace("_", " ") : "")));
    const pt = $("pinsTable");
    if (!pt.childElementCount) for (const p of s.board.pins) pt.append(el("tr", {}, el("td", { text: p.gpio < 0 ? "—" : p.gpio }), el("td", { text: p.signal }), el("td", { class: "dim", text: p.part })));

    // IR
    $("irListen").textContent = s.ir.listening ? "listening" : "not listening";
    const sent = s.ir.sent.slice(Math.max(0, s.ir.sent.length - (s.ir.sent_count - irClearedAt))).reverse();
    $("irSent").replaceChildren(...(sent.length ? sent.map((x) => el("div", { class: "item" },
      el("span", { class: "mono", text: x.protocol === "RAW" ? `RAW · ${x.raw} timings` : `${x.protocol} · addr 0x${x.address.toString(16).toUpperCase()} · cmd 0x${x.command.toString(16).toUpperCase()}` })))
      : [el("div", { class: "empty", text: "Nothing sent yet. Signals the firmware transmits appear here, newest first." })]));

    // NFC
    $("nfcStatus").textContent = !s.nfc.module ? "not answering" : s.nfc.polling ? "polling" + (s.nfc.card_present ? " · card " + s.nfc.card_uid : "") : "not polling";
    $("nfcModule").checked = s.nfc.module;
    $("nfcEmu").textContent = s.nfc.emulating ? `Emulating ${s.nfc.emulated_uid} · read ${s.nfc.reader_taps} time(s)` : "The device is not emulating a card.";
    $("nfcWritten").textContent = s.nfc.blocks_written ? `${s.nfc.blocks_written} block(s) written to the card in the field.` : "No writes yet.";

    // WiFi
    $("wifiStatus").textContent = s.wifi.state + (s.wifi.ssid ? " · " + s.wifi.ssid : "");
    if (!nets || !$("tab-wifi").contains(document.activeElement)) {
      const fresh = JSON.stringify(s.wifi.networks) !== JSON.stringify(nets);
      nets = JSON.parse(JSON.stringify(s.wifi.networks));
      if (fresh) paintNets();
    }
    $("wifiOk").checked = s.wifi.next_connect_succeeds;
    $("wifiNtp").checked = s.wifi.ntp_responds;
    if (document.activeElement !== $("wifiDelay")) { $("wifiDelay").value = s.wifi.latency_ms; $("wifiDelayText").textContent = `Delay ${s.wifi.latency_ms} ms`; }

    // Bluetooth
    $("btStatus").textContent = s.ble.state + (s.ble.host ? " · " + s.ble.host : "");
    $("btCode").textContent = s.ble.passkey !== null ? String(s.ble.passkey).padStart(6, "0") : "—";
    $("btBonds").replaceChildren(...(s.ble.bonds.length ? s.ble.bonds.map((b) => el("div", { class: "item" }, el("span", { text: b }),
      el("span", { class: "r", text: b === s.ble.host && s.ble.state === "connected" ? "connected" : "paired" })))
      : [el("div", { class: "empty", text: "No paired hosts." })]));
    $("btFull").checked = s.ble.bonds.some((b) => b.startsWith("Old device"));
    const names = { 205: "Play/Pause", 181: "Next", 182: "Prev", 233: "Vol +", 234: "Vol -", 226: "Mute", 79: "Right", 80: "Left", 81: "Down", 82: "Up", 75: "Page Up", 78: "Page Down", 41: "Esc", 40: "Enter" };
    $("btKeys").replaceChildren(...(s.ble.keys.length ? s.ble.keys.slice().reverse().map((k) => el("div", { class: "item" },
      el("span", { text: names[k.usage] || "0x" + k.usage.toString(16) }), el("span", { class: "r mono", text: `${k.page} 0x${k.usage.toString(16).toUpperCase()}` })))
      : [el("div", { class: "empty", text: "No keys yet. Media and keyboard keys the device sends appear here." })]));

    // Files: seeds
    const seedSel = $("seedSel");
    if (seedSel.childElementCount !== s.seeds.length + 1) {
      seedSel.replaceChildren(el("option", { value: "", text: "empty storage" }), ...s.seeds.map((x) => el("option", { value: x, text: "seeds/" + x })));
    }

    // Scripts
    const list = $("scriptList");
    list.replaceChildren(...(s.scripts.length ? s.scripts.map((x) => {
      const r = x.result;
      const status = s.playing === x.name ? "running…" : !r ? "not run" : r.passed ? `pass · ${r.checks} checks` : `FAIL · ${r.failed.length} of ${r.checks}`;
      const it = el("div", { class: "item" + (selScript === x.name ? " sel" : ""), tabindex: "0", role: "option" },
        el("span", { text: x.name }), el("span", { class: "r " + (!r ? "" : r.passed ? "pass" : "fail"), text: status }));
      it.onclick = () => { selScript = x.name; render(); };
      return it;
    }) : [el("div", { class: "empty", text: "No scripts in scripts/ yet. Record one below or save the command log." })]));
    const sr = selScript && s.scripts.find((x) => x.name === selScript);
    if (sr && sr.result && !sr.result.passed) $("scriptDetail").textContent = sr.result.failed.join("\n");
    $("recordBtn").textContent = s.recording ? "Stop and save" : "Start recording";

    // log
    if (s.log.length !== lastLogLen) {
      lastLogLen = s.log.length;
      const box = $("logLines");
      box.replaceChildren(...s.log.map((l) => el("div", {}, el("span", { class: "t", text: fmtUp(l.t) }), el("span", { text: l.text }),
        el("span", { class: "res", text: l.result || "" }))), el("div", {}, el("span", { class: "t", text: fmtUp(s.uptime_ms) }), el("span", { class: "prompt", text: "> _" })));
      box.scrollTop = box.scrollHeight;
    }
  }

  // ---------------- loops ----------------
  async function pollState() {
    try { applyState(await api("state?full=1"), true); } catch (e) { /* session closed */ }
  }
  async function start() {
    try { const k = await api("keys"); if (k && k.OK) keys = { ...DEFAULT_KEYS, ...k }; } catch {}
    paintCaps();
    await pollState();
    await drawFrame();
    showTab(tab);
    if (SHOT) { document.body.dataset.ready = "1"; return; }
    setInterval(pollState, 300);
    let busy = false;
    const frame = async () => {
      if (!busy) { busy = true; try { await drawFrame(); } catch {} busy = false; }
      setTimeout(frame, 33);
    };
    frame();
    setInterval(() => { if (tab === "files") loadFiles(); }, 2000);
  }
  start();
})();
