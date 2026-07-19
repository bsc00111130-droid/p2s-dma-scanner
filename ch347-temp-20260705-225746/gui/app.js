(function () {
  "use strict";

  var MAX_REQUEST_SIZE = 0x1000;
  var USER_PROBE_LIMIT = BigInt("0x00007FFFFFFEFFFF");
  var RING_CAPACITY = 512;
  var SLOT_COUNT = 8;

  function byId(id) {
    return document.getElementById(id);
  }

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function clampInt16(value) {
    return clamp(Math.round(value), -32768, 32767);
  }

  function parseU32(text, fallback) {
    var value = Number.parseInt(String(text).trim(), 0);
    if (!Number.isFinite(value)) {
      return fallback;
    }
    return value >>> 0;
  }

  function parseAddress(text) {
    var trimmed = String(text).trim();
    if (trimmed.length === 0) {
      return null;
    }
    try {
      return BigInt(trimmed);
    } catch (error) {
      return null;
    }
  }

  function hexByte(value) {
    return (value & 0xff).toString(16).toUpperCase().padStart(2, "0");
  }

  function hexDump(bytes, maxBytes) {
    var limit = Math.min(bytes.length, maxBytes);
    var lines = [];
    for (var i = 0; i < limit; i += 16) {
      var row = [];
      for (var j = i; j < Math.min(i + 16, limit); j += 1) {
        row.push(hexByte(bytes[j]));
      }
      lines.push(i.toString(16).toUpperCase().padStart(4, "0") + "  " + row.join(" "));
    }
    if (bytes.length > limit) {
      lines.push("... " + (bytes.length - limit) + " more bytes");
    }
    return lines.join("\n");
  }

  function xorshift32(seed) {
    var x = seed >>> 0;
    x ^= (x << 13);
    x ^= (x >>> 17);
    x ^= (x << 5);
    return x >>> 0;
  }

  function makePayload(pid, address, size, sequence) {
    var length = clamp(size, 0, MAX_REQUEST_SIZE - 1);
    var bytes = new Uint8Array(length);
    var seed = (pid ^ Number(address & BigInt(0xffffffff)) ^ sequence) >>> 0;
    for (var i = 0; i < length; i += 1) {
      seed = xorshift32(seed + i + 0x9e3779b9);
      bytes[i] = seed & 0xff;
    }
    return bytes;
  }

  function checksum(bytes, mode) {
    var value = 0;
    for (var i = 0; i < 7; i += 1) {
      if (mode === "xor8") {
        value ^= bytes[i];
      } else {
        value = (value + bytes[i]) & 0xff;
      }
    }
    if (mode === "twos") {
      value = (-value) & 0xff;
    }
    return value & 0xff;
  }

  function packetize(dx, dy) {
    var header = parseU32(byId("headerInput").value, 0xa55a) & 0xffff;
    var command = parseU32(byId("commandInput").value, 1) & 0xff;
    var scale = Number.parseFloat(byId("scaleInput").value);
    if (!Number.isFinite(scale) || scale <= 0) {
      scale = 1;
    }

    var x = clampInt16(dx * scale);
    var y = clampInt16(dy * scale);
    var xb = x < 0 ? 0x10000 + x : x;
    var yb = y < 0 ? 0x10000 + y : y;
    var bytes = new Uint8Array(8);
    bytes[0] = header & 0xff;
    bytes[1] = (header >>> 8) & 0xff;
    bytes[2] = command;
    bytes[3] = xb & 0xff;
    bytes[4] = (xb >>> 8) & 0xff;
    bytes[5] = yb & 0xff;
    bytes[6] = (yb >>> 8) & 0xff;
    bytes[7] = checksum(bytes, byId("checksumInput").value);
    return { bytes: bytes, x: x, y: y };
  }

  function Kalman1D() {
    this.x = 0;
    this.v = 0;
    this.p00 = 1;
    this.p01 = 0;
    this.p10 = 0;
    this.p11 = 1;
    this.ready = false;
  }

  Kalman1D.prototype.update = function (measurement, dt, q, r) {
    if (!this.ready) {
      this.x = measurement;
      this.v = 0;
      this.ready = true;
    }

    this.x += this.v * dt;
    this.p00 += dt * (dt * this.p11 - this.p10 - this.p01 + q);
    this.p01 -= dt * this.p11;
    this.p10 -= dt * this.p11;
    this.p11 += q * dt;

    var innovation = measurement - this.x;
    var s = this.p00 + r;
    var k0 = this.p00 / s;
    var k1 = this.p10 / s;

    this.x += k0 * innovation;
    this.v += k1 * innovation;

    this.p00 -= k0 * this.p00;
    this.p01 -= k0 * this.p01;
    this.p10 -= k1 * this.p00;
    this.p11 -= k1 * this.p01;
    return this.x;
  };

  var state = {
    running: false,
    sequence: 0,
    produced: 0,
    sent: 0,
    dropped: 0,
    busyDropped: 0,
    queue: [],
    slots: [],
    history: [],
    motionHistory: [],
    lastTick: performance.now(),
    latencyMs: 0,
    phase: 0,
    filterX: new Kalman1D(),
    filterY: new Kalman1D(),
    lastFilteredX: 0,
    lastFilteredY: 0,
    lastStepX: 0,
    lastStepY: 0,
    lastPacket: new Uint8Array(8)
  };

  for (var s = 0; s < SLOT_COUNT; s += 1) {
    state.slots.push({ busyUntil: 0, packet: null });
  }

  function log(message) {
    var logEl = byId("eventLog");
    var row = document.createElement("div");
    var time = new Date().toLocaleTimeString();
    row.textContent = "[" + time + "] " + message;
    logEl.prepend(row);
    while (logEl.children.length > 80) {
      logEl.removeChild(logEl.lastChild);
    }
  }

  function setBadge(id, text, kind) {
    var el = byId(id);
    el.textContent = text;
    el.className = "badge" + (kind ? " " + kind : "");
  }

  function setCheck(id, ok, detail) {
    var row = byId(id);
    row.classList.toggle("ok", ok);
    row.classList.toggle("fail", !ok);
    row.querySelector("em").textContent = detail;
  }

  function setMiniStatus(id, text, kind) {
    var el = byId(id);
    el.textContent = text;
    el.className = "mini-status" + (kind ? " " + kind : "");
  }

  function setButtonBusy(button, text) {
    var oldText = button.textContent;
    button.disabled = true;
    button.textContent = text;
    window.setTimeout(function () {
      button.disabled = false;
      button.textContent = oldText;
    }, 900);
  }

  function copyText(text) {
    function fallbackCopy() {
      var textarea = document.createElement("textarea");
      textarea.value = text;
      textarea.setAttribute("readonly", "readonly");
      textarea.style.position = "fixed";
      textarea.style.left = "-9999px";
      document.body.appendChild(textarea);
      textarea.select();
      var copied = document.execCommand("copy");
      document.body.removeChild(textarea);
      return copied ? Promise.resolve() : Promise.reject(new Error("copy rejected"));
    }

    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text).catch(fallbackCopy);
    }

    return fallbackCopy();
  }

  function updateRangeOutputs() {
    byId("noiseValue").textContent = byId("noiseInput").value;
    byId("processValue").textContent = byId("processInput").value;
    byId("measureValue").textContent = byId("measureInput").value;
    byId("stepValue").textContent = byId("stepInput").value;
  }

  function selectElementText(element) {
    var range = document.createRange();
    var selection = window.getSelection();
    range.selectNodeContents(element);
    selection.removeAllRanges();
    selection.addRange(range);
  }

  function validateContract() {
    var pid = Number.parseInt(byId("pidInput").value, 10);
    var size = Number.parseInt(byId("sizeInput").value, 10);
    var address = parseAddress(byId("addressInput").value);
    var pidOk = Number.isFinite(pid) && pid > 0;
    var sizeOk = Number.isFinite(size) && size > 0 && size < MAX_REQUEST_SIZE;
    var addressOk = address !== null && address > 0n && address <= USER_PROBE_LIMIT;
    var overflowOk = false;

    if (addressOk && sizeOk) {
      var end = address + BigInt(size - 1);
      overflowOk = end >= address && end <= USER_PROBE_LIMIT;
    }

    setCheck("pidCheck", pidOk, pidOk ? String(pid) : "invalid");
    setCheck("sizeCheck", sizeOk, sizeOk ? size + " bytes" : "rejected");
    setCheck("addressCheck", addressOk, addressOk ? "user range" : "invalid");
    setCheck("overflowCheck", overflowOk, overflowOk ? "clear" : "blocked");
    setCheck("readmemCheck", true, "disabled");

    var allOk = pidOk && sizeOk && addressOk && overflowOk;
    setBadge("contractBadge", allOk ? "Accepted" : "Rejected", allOk ? "good" : "bad");
    setMiniStatus("contractMini", allOk ? "Contract accepted" : "Contract rejected", allOk ? "good" : "bad");
    return {
      ok: allOk,
      pid: pidOk ? pid : 0,
      size: sizeOk ? size : 0,
      address: addressOk ? address : 0n,
      reason: !pidOk ? "invalid PID" :
        !sizeOk ? "size must be 1..4095" :
        !addressOk ? "invalid user address" :
        !overflowOk ? "address overflow" : "accepted"
    };
  }

  function updateCommandLine() {
    var mode = byId("modeInput").value;
    var com = byId("comInput").value.trim() || "-";
    var suffix = mode === "shared" ? " shared" : "";
    byId("commandLine").textContent =
      "proc_ioctl_controller.exe " +
      (byId("pidInput").value || "0") + " " +
      (byId("addressInput").value || "0") + " " +
      (byId("sizeInput").value || "0") + " " +
      com + " " +
      (byId("rateInput").value || "1000") +
      suffix;
  }

  function produceSample(now) {
    var contract = validateContract();
    state.sequence += 1;

    if (!contract.ok) {
      state.dropped += 1;
      return;
    }

    var payload = makePayload(contract.pid, contract.address, contract.size, state.sequence);
    var packet = updateMotion(0.016);
    var sample = {
      sequence: state.sequence,
      payload: payload,
      packet: packet.bytes,
      createdAt: now
    };

    if (state.queue.length >= RING_CAPACITY) {
      state.dropped += 1;
    } else {
      state.queue.push(sample);
      state.produced += 1;
    }
  }

  function drainQueue(now) {
    for (var i = 0; i < state.slots.length; i += 1) {
      if (state.slots[i].busyUntil <= now) {
        state.slots[i].packet = null;
      }
    }

    while (state.queue.length > 0) {
      var freeSlot = state.slots.find(function (slot) { return slot.busyUntil <= now; });
      if (!freeSlot) {
        state.busyDropped += 1;
        return;
      }

      var sample = state.queue.shift();
      var simulatedWriteMs = 0.18 + (sample.packet.length / 8) * 0.05;
      freeSlot.busyUntil = now + simulatedWriteMs;
      freeSlot.packet = sample.packet;
      state.sent += 1;
      state.latencyMs = now - sample.createdAt;
      state.lastPacket = sample.packet;
    }
  }

  function updateMotion(dt) {
    state.phase += dt;
    var noiseLevel = Number.parseFloat(byId("noiseInput").value) || 0;
    var q = (Number.parseFloat(byId("processInput").value) || 1) / 1000;
    var r = (Number.parseFloat(byId("measureInput").value) || 1) / 10;
    var maxStep = Number.parseFloat(byId("stepInput").value) || 1;

    var baseX = Math.cos(state.phase * 1.2) * 90;
    var baseY = Math.sin(state.phase * 0.9) * 58;
    var noiseX = (Math.sin(state.phase * 31.7) + Math.cos(state.phase * 17.3)) * noiseLevel * 0.5;
    var noiseY = (Math.cos(state.phase * 27.1) - Math.sin(state.phase * 13.9)) * noiseLevel * 0.5;
    var rawX = baseX + noiseX;
    var rawY = baseY + noiseY;
    var filteredX = state.filterX.update(rawX, dt, q, r);
    var filteredY = state.filterY.update(rawY, dt, q, r);
    var deltaX = filteredX - state.lastFilteredX;
    var deltaY = filteredY - state.lastFilteredY;
    var distance = Math.hypot(deltaX, deltaY);
    var limiter = distance > maxStep ? maxStep / distance : 1;
    var stepX = deltaX * limiter;
    var stepY = deltaY * limiter;
    var packet = packetize(stepX, stepY);

    state.lastFilteredX += stepX;
    state.lastFilteredY += stepY;
    state.lastStepX = packet.x;
    state.lastStepY = packet.y;
    state.motionHistory.push({
      rawX: rawX,
      rawY: rawY,
      filteredX: state.lastFilteredX,
      filteredY: state.lastFilteredY
    });
    if (state.motionHistory.length > 160) {
      state.motionHistory.shift();
    }

    byId("rawXMetric").textContent = rawX.toFixed(2);
    byId("rawYMetric").textContent = rawY.toFixed(2);
    byId("filteredXMetric").textContent = state.lastFilteredX.toFixed(2);
    byId("filteredYMetric").textContent = state.lastFilteredY.toFixed(2);
    byId("stepXMetric").textContent = String(packet.x);
    byId("stepYMetric").textContent = String(packet.y);
    byId("vectorBadge").textContent = packet.x + ", " + packet.y;
    byId("packetHex").textContent = Array.from(packet.bytes).map(hexByte).join(" ");
    return packet;
  }

  function drawTimeline() {
    var canvas = byId("timelineCanvas");
    var ctx = canvas.getContext("2d");
    var width = canvas.width;
    var height = canvas.height;
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#fbfcfd";
    ctx.fillRect(0, 0, width, height);

    ctx.strokeStyle = "#d8e0e6";
    ctx.lineWidth = 1;
    for (var y = 30; y < height; y += 40) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
      ctx.stroke();
    }

    var history = state.history;
    if (history.length < 2) {
      return;
    }

    ctx.strokeStyle = "#127a7a";
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (var i = 0; i < history.length; i += 1) {
      var x = (i / Math.max(1, history.length - 1)) * width;
      var level = history[i].queue / RING_CAPACITY;
      var py = height - 18 - level * (height - 36);
      if (i === 0) {
        ctx.moveTo(x, py);
      } else {
        ctx.lineTo(x, py);
      }
    }
    ctx.stroke();

    ctx.fillStyle = "#205b8f";
    var last = history[history.length - 1];
    ctx.fillRect(0, height - 12, width * clamp(last.queue / RING_CAPACITY, 0, 1), 6);
  }

  function drawMotion() {
    var canvas = byId("motionCanvas");
    var ctx = canvas.getContext("2d");
    var width = canvas.width;
    var height = canvas.height;
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#fbfcfd";
    ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = "#d8e0e6";
    ctx.beginPath();
    ctx.moveTo(width / 2, 0);
    ctx.lineTo(width / 2, height);
    ctx.moveTo(0, height / 2);
    ctx.lineTo(width, height / 2);
    ctx.stroke();

    function plot(keyX, keyY, color, widthLine) {
      ctx.strokeStyle = color;
      ctx.lineWidth = widthLine;
      ctx.beginPath();
      state.motionHistory.forEach(function (point, idx) {
        var x = width / 2 + point[keyX] * 1.8;
        var y = height / 2 + point[keyY] * 1.8;
        if (idx === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });
      ctx.stroke();
    }

    plot("rawX", "rawY", "rgba(180, 106, 22, 0.55)", 1);
    plot("filteredX", "filteredY", "#127a7a", 2);
  }

  function updateSlots() {
    var grid = byId("slotGrid");
    grid.innerHTML = "";
    var now = performance.now();
    state.slots.forEach(function (slot, index) {
      var cell = document.createElement("div");
      cell.className = "slot" + (slot.busyUntil > now ? " busy" : "");
      cell.textContent = String(index);
      grid.appendChild(cell);
    });
  }

  function render() {
    var queuePercent = (state.queue.length / RING_CAPACITY) * 100;
    byId("producedMetric").textContent = String(state.produced);
    byId("sentMetric").textContent = String(state.sent);
    byId("droppedMetric").textContent = String(state.dropped);
    byId("busyMetric").textContent = String(state.busyDropped);
    byId("queueMetric").textContent = queuePercent.toFixed(0) + "%";
    byId("latencyMetric").textContent = state.latencyMs.toFixed(2) + " ms";
    byId("tickLabel").textContent = "seq " + state.sequence;
    byId("baudMetric").textContent = byId("baudInput").value || "0";
    byId("payloadSizeBadge").textContent = (Number.parseInt(byId("sizeInput").value, 10) || 0) + " bytes";
    byId("payloadPreview").textContent = hexDump(makePayload(
      Number.parseInt(byId("pidInput").value, 10) || 0,
      parseAddress(byId("addressInput").value) || 0n,
      Number.parseInt(byId("sizeInput").value, 10) || 0,
      state.sequence
    ), 256);

    state.history.push({ queue: state.queue.length });
    if (state.history.length > 180) {
      state.history.shift();
    }

    drawTimeline();
    drawMotion();
    updateSlots();
    updateCommandLine();

    var queueKind = queuePercent >= 80 ? "bad" : queuePercent >= 40 ? "warn" : "good";
    setMiniStatus("queueMini", "Queue " + queuePercent.toFixed(0) + "%", queueKind);
    setMiniStatus("serialMini", "Serial dry view", "neutral");
    setMiniStatus("safetyMini", "Safety locked", "good");
    updateRangeOutputs();
  }

  function tick() {
    var now = performance.now();
    var rate = clamp(Number.parseInt(byId("rateInput").value, 10) || 1000, 1, 5000);
    var elapsed = now - state.lastTick;
    state.lastTick = now;
    var samples = state.running ? Math.max(1, Math.min(80, Math.round(elapsed * rate / 1000))) : 0;

    var start = now - elapsed;
    for (var i = 0; i < samples; i += 1) {
      var sampleNow = start + (elapsed * (i + 1) / samples);
      produceSample(sampleNow);
      drainQueue(sampleNow);
    }
    drainQueue(now);

    if (!state.running) {
      updateMotion(0.016);
    }
    render();
  }

  function resetState() {
    state.running = false;
    state.sequence = 0;
    state.produced = 0;
    state.sent = 0;
    state.dropped = 0;
    state.busyDropped = 0;
    state.queue = [];
    state.history = [];
    state.motionHistory = [];
    state.latencyMs = 0;
    state.phase = 0;
    state.filterX = new Kalman1D();
    state.filterY = new Kalman1D();
    state.lastFilteredX = 0;
    state.lastFilteredY = 0;
    state.lastStepX = 0;
    state.lastStepY = 0;
    state.slots.forEach(function (slot) {
      slot.busyUntil = 0;
      slot.packet = null;
    });
    setBadge("runtimeStatus", "Stopped", "idle");
    log("state reset");
  }

  function wireEvents() {
    byId("startBtn").addEventListener("click", function () {
      var contract = validateContract();
      if (!contract.ok) {
        setBadge("runtimeStatus", "Blocked", "bad");
        log("start blocked: " + contract.reason);
        return;
      }
      state.running = true;
      state.lastTick = performance.now();
      setBadge("runtimeStatus", "Running", "good");
      log("runtime started");
    });
    byId("stopBtn").addEventListener("click", function () {
      state.running = false;
      setBadge("runtimeStatus", "Stopped", "idle");
      log("runtime stopped");
    });
    byId("stepBtn").addEventListener("click", function () {
      produceSample(performance.now());
      drainQueue(performance.now());
      render();
      log("single sample processed");
    });
    byId("resetBtn").addEventListener("click", function () {
      resetState();
      render();
    });
    byId("clearLogBtn").addEventListener("click", function () {
      byId("eventLog").innerHTML = "";
    });
    byId("copyCommandBtn").addEventListener("click", function () {
      var button = byId("copyCommandBtn");
      copyText(byId("commandLine").textContent).then(function () {
        setButtonBusy(button, "Copied");
        log("command copied");
      }).catch(function () {
        selectElementText(byId("commandLine"));
        setButtonBusy(button, "Selected");
        log("command selected for manual copy");
      });
    });
    byId("copyPacketBtn").addEventListener("click", function () {
      var button = byId("copyPacketBtn");
      copyText(byId("packetHex").textContent).then(function () {
        setButtonBusy(button, "Copied");
        log("packet copied");
      }).catch(function () {
        selectElementText(byId("packetHex"));
        setButtonBusy(button, "Selected");
        log("packet selected for manual copy");
      });
    });

    Array.prototype.forEach.call(document.querySelectorAll("[data-rate]"), function (button) {
      button.addEventListener("click", function () {
        byId("rateInput").value = button.getAttribute("data-rate");
        updateCommandLine();
        render();
        log("rate preset " + byId("rateInput").value + " Hz");
      });
    });

    Array.prototype.forEach.call(document.querySelectorAll(".nav-item"), function (item) {
      item.addEventListener("click", function () {
        Array.prototype.forEach.call(document.querySelectorAll(".nav-item"), function (nav) {
          nav.classList.remove("active");
        });
        item.classList.add("active");
      });
    });

    [
      "pidInput", "addressInput", "sizeInput", "modeInput", "comInput",
      "baudInput", "rateInput", "noiseInput", "processInput", "measureInput",
      "stepInput", "headerInput", "commandInput", "checksumInput", "scaleInput"
    ].forEach(function (id) {
      byId(id).addEventListener("input", function () {
        var contract = validateContract();
        if (!state.running && contract.ok && byId("runtimeStatus").textContent === "Blocked") {
          setBadge("runtimeStatus", "Idle", "idle");
        }
        updateCommandLine();
        updateRangeOutputs();
        render();
      });
    });
  }

  function init() {
    wireEvents();
    validateContract();
    setBadge("runtimeStatus", "Idle", "idle");
    byId("slotCountMetric").textContent = String(SLOT_COUNT);
    updateRangeOutputs();
    log("dashboard ready");
    render();
    window.setInterval(tick, 50);
  }

  document.addEventListener("DOMContentLoaded", init);
}());
