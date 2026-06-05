const targetUrl = process.argv[2] ?? 'http://127.0.0.1:9223/json';

const targets = await (await fetch(targetUrl)).json();
const page = targets.find((target) => target.type === 'page');

if (!page?.webSocketDebuggerUrl) {
  throw new Error('No Chrome page target found. Start Chrome with --remote-debugging-port first.');
}

const ws = new WebSocket(page.webSocketDebuggerUrl);
const pending = new Map();
let nextId = 1;

ws.addEventListener('message', (event) => {
  const message = JSON.parse(event.data);
  if (!message.id || !pending.has(message.id)) return;

  const { resolve, reject } = pending.get(message.id);
  pending.delete(message.id);

  if (message.error) reject(new Error(JSON.stringify(message.error)));
  else resolve(message.result);
});

await new Promise((resolve, reject) => {
  ws.addEventListener('open', resolve, { once: true });
  ws.addEventListener('error', reject, { once: true });
});

function cdp(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
}

async function evaluate(expression, timeout = 60000) {
  const result = await cdp('Runtime.evaluate', {
    expression,
    awaitPromise: true,
    returnByValue: true,
    timeout,
  });

  if (result.exceptionDetails) {
    throw new Error(JSON.stringify(result.exceptionDetails));
  }

  return result.result.value;
}

await cdp('Runtime.enable');
await cdp('Page.enable');

await cdp('Page.addScriptToEvaluateOnNewDocument', {
  source: `
    (() => {
      const listeners = new Map();
      let nextListenerId = 1;
      window.__fpsBridgeSends = [];
      window.__fpsEmitEvent = (eventId, data) => {
        const callbacks = listeners.get(eventId);
        if (!callbacks) return;
        callbacks.forEach((callback) => callback(data));
      };
      window.__JUCE__ = {
        backend: {
          addEventListener(eventId, callback) {
            const id = nextListenerId++;
            if (!listeners.has(eventId)) listeners.set(eventId, new Map());
            listeners.get(eventId).set(id, callback);
            return [eventId, id];
          },
          removeEventListener(handle) {
            const [eventId, id] = handle;
            listeners.get(eventId)?.delete(id);
          },
          emitEvent(eventId, object) {
            window.__fpsBridgeSends.push({ eventId, object, t: performance.now() });
          },
        },
      };
    })();
  `,
});

await cdp('Page.navigate', { url: page.url || 'http://127.0.0.1:4174/' });
await new Promise((resolve) => setTimeout(resolve, 500));

const results = await evaluate(`
(async () => {
  function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  function rafSample(durationMs) {
    return new Promise(resolve => {
      const deltas = [];
      const start = performance.now();
      let last = start;

      function tick(now) {
        deltas.push(now - last);
        last = now;

        if (now - start >= durationMs) {
          const elapsed = now - start;
          const sorted = deltas.slice().sort((a, b) => a - b);
          resolve({
            frames: deltas.length,
            fps: Number((deltas.length * 1000 / elapsed).toFixed(1)),
            avgFrameMs: Number((deltas.reduce((a, b) => a + b, 0) / deltas.length).toFixed(2)),
            p95FrameMs: Number((sorted[Math.floor(sorted.length * 0.95)] || 0).toFixed(2)),
            maxFrameMs: Number((sorted[sorted.length - 1] || 0).toFixed(2)),
          });
          return;
        }

        requestAnimationFrame(tick);
      }

      requestAnimationFrame(tick);
    });
  }

  async function measure(label, action) {
    if (action) await action();
    await sleep(120);
    return { label, ...(await rafSample(1800)) };
  }

  function clickButton(text) {
    const button = [...document.querySelectorAll('button')]
      .find(candidate => candidate.textContent.trim() === text);
    button?.click();
  }

  const eventCounts = { raf: 0, interval: 0, timeout: 0, pointermove: 0, juceEmit: 0 };
  const originalRaf = window.requestAnimationFrame;
  const originalInterval = window.setInterval;
  const originalTimeout = window.setTimeout;
  const originalEmit = window.__JUCE__?.backend?.emitEvent;

  window.requestAnimationFrame = function wrappedRaf(callback) {
    eventCounts.raf += 1;
    return originalRaf.call(this, callback);
  };
  window.setInterval = function wrappedInterval(callback, delay, ...args) {
    eventCounts.interval += 1;
    return originalInterval.call(this, callback, delay, ...args);
  };
  window.setTimeout = function wrappedTimeout(callback, delay, ...args) {
    eventCounts.timeout += 1;
    return originalTimeout.call(this, callback, delay, ...args);
  };
  if (originalEmit) {
    window.__JUCE__.backend.emitEvent = function wrappedEmit(...args) {
      eventCounts.juceEmit += 1;
      return originalEmit.apply(this, args);
    };
  }
  window.addEventListener('pointermove', () => { eventCounts.pointermove += 1; }, true);

  let meterTick = 0;
  const levelInterval = setInterval(() => {
    meterTick += 1;
    const phase = meterTick / 4;
    const pads = Array.from({ length: 16 }, (_, i) => Math.max(0, Math.sin(phase + i * 0.37) * 0.75));
    window.__fpsEmitEvent?.('levelData', {
      start: 0,
      pads,
      masterL: Math.max(0, Math.sin(phase) * 0.9),
      masterR: Math.max(0, Math.cos(phase * 0.8) * 0.8),
    });
  }, 1000 / 30);

  let statsTick = 0;
  const statsInterval = setInterval(() => {
    statsTick += 1;
    window.__fpsEmitEvent?.('systemStats', {
      cpuPercent: 10 + Math.sin(statsTick / 2) * 8,
      sampleBytes: 128 * 1024 * 1024,
    });
  }, 300);

  const out = [];
  out.push(await measure('PADS idle'));

  clickButton('MIXER');
  out.push(await measure('MIXER idle'));

  clickButton('PADS');
  out.push(await measure('PADS after tab switch'));

  const knob = document.querySelector('[role="slider"]');
  if (knob) {
    const rect = knob.getBoundingClientRect();
    const x = rect.left + rect.width / 2;
    const y = rect.top + rect.height / 2;

    out.push(await measure('Knob drag', async () => {
      knob.dispatchEvent(new PointerEvent('pointerdown', {
        bubbles: true,
        pointerId: 7,
        clientX: x,
        clientY: y,
      }));

      for (let i = 0; i < 45; i += 1) {
        window.dispatchEvent(new PointerEvent('pointermove', {
          bubbles: true,
          pointerId: 7,
          clientX: x,
          clientY: y - i * 2,
        }));
        await sleep(8);
      }

      window.dispatchEvent(new PointerEvent('pointerup', {
        bubbles: true,
        pointerId: 7,
        clientX: x,
        clientY: y - 90,
      }));
    }));
  }

  const waveHandle = document.querySelector('svg [class*="trimMarker"], svg [class*="trimHandle"]');
  if (waveHandle) {
    const rect = waveHandle.getBoundingClientRect();
    const x = rect.left + rect.width / 2;
    const y = rect.top + rect.height / 2;

    out.push(await measure('Waveform handle drag', async () => {
      waveHandle.dispatchEvent(new PointerEvent('pointerdown', {
        bubbles: true,
        pointerId: 8,
        clientX: x,
        clientY: y,
      }));

      for (let i = 0; i < 45; i += 1) {
        window.dispatchEvent(new PointerEvent('pointermove', {
          bubbles: true,
          pointerId: 8,
          clientX: x + i * 3,
          clientY: y,
        }));
        await sleep(8);
      }

      window.dispatchEvent(new PointerEvent('pointerup', {
        bubbles: true,
        pointerId: 8,
        clientX: x + 135,
        clientY: y,
      }));
    }));
  }

  clearInterval(levelInterval);
  clearInterval(statsInterval);

  return {
    measurements: out,
    eventCounts,
    bridgeSends: window.__fpsBridgeSends?.length ?? 0,
    tabText: [...document.querySelectorAll('button')]
      .slice(0, 12)
      .map(button => button.textContent.trim())
      .filter(Boolean),
  };
})()
`);

console.log(JSON.stringify(results, null, 2));
ws.close();
