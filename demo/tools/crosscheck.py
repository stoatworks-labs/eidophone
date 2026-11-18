"""The browser demo against the plugin's harness: the same raster, the same
settings, the same audio, the same frames, two renderers.

    python3 demo/tools/crosscheck.py --edtest build/edtest [--keep DIR] [--only TEXT]

Needs a GPU, Google Chrome, numpy, Pillow, a built edtest, and
stoatworks-backend's `release/cdpshot.py` (CDPSHOT=path overrides
~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py). Not run by
tools/verify.sh. (woofer's crosscheck, which is mirrorball's, with eidophone's
plugins, track and fallback.)

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal) at ?size=WxH&clip=C[&plugin=over].
    The page is paused, the case's settings applied, and its audio chosen
    through the host row's own path (a new instance at time 0, which draws its
    first frame there). Then Step is pressed N times, one frame of 1/60 s each:
    N + 1 frames in all, the last at N/60 s, with (N + 1) x 800 samples of the
    page's track fed at 48 kHz (each frame is fed up to the end of its
    interval). For the Over, Mix is then set to 0 (the plugin hands the clip
    through untouched; a repeated host time runs no substep and feeds
    nothing), the canvas is read back as the input frame, Mix is put back and
    the page's output is read back. preserveDrawingBuffer is forced on from
    outside so the canvas can be read after its frame; nothing in the page is
    changed for this.
  - by the PLUGIN: `edtest --pipe --fps 60 --size WxH --wav TRACK --set ...`
    (the real EidophonePlugin in a GL 4.1 context, its clock preset to
    seconds), with the same host values (every control as the page's own
    hostValue() sends it, the float32 printed to 9 digits) and the page's own
    track samples, read out of its WebAssembly memory and written to a float
    WAV, for N + 1 frames; the Over is fed the page's input frame for every
    frame (only the last frame's clip reaches the picture: the grains never
    read it). On the "Resolume FFT" cases, which feed nothing, edtest's plain
    render is used instead (`--bins 1 --frames N+1 --out PNG`, with the clip as
    `--clip` for the Over), because --pipe has no --bins; its PNG has no
    alpha, so only colour is compared there.

The grain walk is chaotic: a grain's position after a second depends on the
last bit of every substep before it, so two renderers that differed in the
last bit anywhere (WebAssembly's libm against Apple's for the plate's doubles,
ANGLE's Metal shaders against Apple's GL for the GLSL) could give the same
figure with different grains in it. Two measures are printed:

  - the bytes: the largest difference in any colour value, and how many values
    differ at all (and the alpha's largest difference, where there is one);
  - the figure: both pictures averaged over 8 x 8 pixel blocks (where the sand
    lies, not which grain is where), the mean and largest block difference in
    codes.

The mark: under 1% of the colour values differ at all, and no 8 x 8 block
differs by more than 1 code on average. Two negative controls on the first
case must FAIL it, so the mark is shown to see a slip: edtest with Gain one
slider step (0.001) off the page's, the smallest real change, and edtest with
the other Shape, a wrong figure.

Exit 1 when a case is past the mark, a negative control passes it, or the page
logs an error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

SIZE = "640x360"
RATE = 48000
PER_FRAME = RATE // 60
BLOCK = 8
MARK_OFF, MARK_BLOCK = 0.01, 1.0

# name, plugin, clip, audio (the host row's id), steps N, settings by the
# plugin's own names (the page's kitValues(): {'units': x} is in the plugin's
# units and goes through its compiled ParamFrom*, an option is its element's
# name, a bare number the host's 0..1).
CASES = [
    ("source, defaults, 5.5 s into the track (D2)", "source", "scene", "track", 330, {}),
    ("source, defaults, the star note 1.5 s in", "source", "scene", "track", 600, {}),
    ("source, Circle pushed 0.4 out, Both", "source", "scene", "track", 300,
     {"Shape": "Circle", "Drive At": {"units": 0.4}, "Grain": "Both"}),
    ("source, 30 cm brass, Gain 30 g, Fill", "source", "scene", "track", 240,
     {"Size": {"units": 0.30}, "Material": "Brass", "Gain": {"units": 30}, "View": "Fill"}),
    ("Over, defaults, 5.5 s in", "over", "scene", "track", 330, {}),
    ("Over, Fit, Mix 50%, Powder", "over", "bars", "track", 240, {"View": "Fit", "Mix": 0.5, "Grain": "Powder"}),
    ("source, Resolume FFT (bins 1), defaults", "source", "scene", "fft", 240, {}),
    ("Over, Resolume FFT (bins 1), Bin Law Log", "over", "grid", "fft", 180, {"Bin Law": "Log"}),
]

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""

# Every control on the panel, as the page sends it to the instance shown.
HOST = """import('./plugin.js').then(m => {
  const variant = m.currentVariant();
  const out = [];
  for (const p of m.PARAMS) {
    const d = p.ff[variant];
    const h = m.hostValue(d, m.mounted.params.get(p.id));
    if (h !== null) out.push([d.name, h]);
  }
  return out;
})"""

# Restart with the audio chosen: the host row's own path (a new instance at time 0).
CHOOSE = """import('./plugin.js').then(m => { m.audio.id = %s; m.restartClock(); })"""

STEP = """(async () => {
  const step = [...document.querySelectorAll('.transport .btn')].find((b) => b.textContent === 'Step');
  for (let i = 0; i < %d; i += 1) {
    step.click();
    await new Promise((resolve) => requestAnimationFrame(() => resolve()));
  }
  const m = await import('./plugin.js');
  return { time: m.mounted.state.time, stats: m.renderer.stats() };
})()"""

# The page's own track, the first `frames` stereo frames, as base64 float32.
TRACK = """import('./plugin.js').then(m => {
  const frames = Math.min(%d, m.core._ed_track_frames());
  const at = m.core._ed_track_samples() >> 2;
  const f = m.core.HEAPF32.subarray(at, at + 2 * frames);
  const bytes = new Uint8Array(f.buffer, f.byteOffset, f.byteLength);
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return { frames, b64: btoa(s) };
})"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def mark(self):
        return self.run("(document.querySelector('canvas')?.dataset.frames || 0) + ':' + (document.querySelector('canvas')?.dataset.instances || 0)")

    def instances(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.instances || 0)"))

    def load(self, url):
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 40
        while time.time() < deadline:
            try:
                if int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)")) > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        before = self.mark()
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.mark() != before:
                return
            time.sleep(0.03)
        raise RuntimeError("no frame after " + expression[:80])

    def canvas(self):
        """The canvas's bytes, rows top first."""
        r = self.run("""(() => {
          const c = document.querySelector('canvas'); const gl = c.getContext('webgl2');
          const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const out = new Uint8Array(w * h * 4);
          for (let y = 0; y < h; y += 1) out.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          let s = '';
          for (let i = 0; i < out.length; i += 0x8000) s += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
          return { w, h, b64: btoa(s) };
        })()""")
        return r["w"], r["h"], base64.b64decode(r["b64"])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def write_wav(path, interleaved, frames):
    """A 32-bit float stereo WAV at 48 kHz (WAVE_FORMAT_IEEE_FLOAT)."""
    data = interleaved[: 2 * frames * 4]
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 3, 2, RATE, RATE * 8, 8, 32))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def settings_args(host, override=None):
    sets = []
    for name, value in host:
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    return sets


def edtest_pipe(binary, over, host, frame, count, wav, override=None):
    """The last of `count` frames, each fed the same input frame."""
    sets = settings_args(host, override)
    proc = subprocess.Popen([binary, "--pipe", "--fps", "60", "--size", SIZE, "--wav", wav, *(["--over"] if over else []), *sets],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def feed():
        try:
            for _ in range(count):
                proc.stdin.write(frame)
            proc.stdin.close()
        except BrokenPipeError:
            pass

    writer = threading.Thread(target=feed, daemon=True)
    writer.start()
    size = len(frame)
    last, got = None, 0
    while True:
        chunk = proc.stdout.read(size)
        if not chunk:
            break
        if len(chunk) < size:
            chunk += proc.stdout.read(size - len(chunk))
        if len(chunk) == size:
            last, got = chunk, got + 1
    writer.join()
    err = proc.stderr.read().decode()
    if proc.wait() != 0 or got != count:
        raise RuntimeError(f"edtest --pipe {' '.join(sets)}: {got} of {count} frames; {err[:400]}")
    return last, True


def edtest_plain(binary, over, host, frame, count, scratch, override=None):
    """edtest's plain render on --bins 1: the last of `count` frames, as RGBA (alpha 255)."""
    from PIL import Image
    sets = settings_args(host, override)
    out = os.path.join(scratch, "plain.png")
    extra = []
    if over:
        clip = os.path.join(scratch, "clip.rgba")
        with open(clip, "wb") as f:
            f.write(frame)
        extra = ["--over", "--clip", clip]
    proc = subprocess.run([binary, "--fps", "60", "--size", SIZE, "--frames", str(count), "--bins", "1", "--out", out, *extra, *sets],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"edtest --bins 1 {' '.join(sets)}: {proc.stderr[:400]}")
    return np.asarray(Image.open(out).convert("RGBA")).tobytes(), False


def blocks(rgb):
    h, w, _ = rgb.shape
    return rgb[: h - h % BLOCK, : w - w % BLOCK].reshape(h // BLOCK, BLOCK, w // BLOCK, BLOCK, 3).mean(axis=(1, 3))


def compare(ours, theirs, w, h, alpha):
    a = np.frombuffer(ours, np.uint8).reshape(h, w, 4).astype(np.int16)
    b = np.frombuffer(theirs, np.uint8).reshape(h, w, 4).astype(np.int16)
    rgb = np.abs(a - b)[..., :3]
    fig = np.abs(blocks(a[..., :3].astype(np.float64)) - blocks(b[..., :3].astype(np.float64)))
    return {
        "max": int(rgb.max()), "off": int((rgb > 0).sum()), "values": int(rgb.size), "mean": float(rgb.mean()),
        "alpha": int(np.abs(a - b)[..., 3].max()) if alpha else None,
        "fig_mean": float(fig.mean()), "fig_max": float(fig.max()),
        "a": a, "b": b, "rgb": rgb,
    }


def within(c):
    return c["off"] < MARK_OFF * c["values"] and c["fig_max"] <= MARK_BLOCK


def line(mark, name, c):
    alpha = f", alpha max {c['alpha']}" if c["alpha"] is not None else ", no alpha (PNG)"
    return (f"{mark}  {name:<48} bytes: max {c['max']} codes, {c['off']} of {c['values']} values off (mean {c['mean']:.2f}){alpha}"
            f" | figure ({BLOCK}x{BLOCK} blocks): mean {c['fig_mean']:.2f}, max {c['fig_max']:.1f} codes")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--edtest", required=True)
    ap.add_argument("--keep", help="write each case's two pictures and their difference (x10) here as PNG")
    ap.add_argument("--only", help="run only the cases whose name contains this")
    args = ap.parse_args()

    w, h = map(int, SIZE.split("x"))
    server, base = serve()
    page = Page()
    scratch = tempfile.mkdtemp(prefix="ed-cross-")
    failures = 0
    first = True
    try:
        for name, plugin, clip, audio, count, settings in CASES:
            if args.only and args.only not in name:
                continue
            over = plugin == "over"
            page.load(f"{base}?size={SIZE}&clip={clip}" + ("&plugin=over" if over else ""))
            page.after("document.querySelector('.btn--play').click()")
            if page.run("import('./plugin.js').then(m => m.mounted.state.playing)"):
                raise RuntimeError("the page did not pause")
            kit = page.run(f"import('./plugin.js').then(m => m.kitValues(m.currentVariant(), {json.dumps(settings)}))")
            if kit:
                page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            before = page.instances()
            page.after(CHOOSE % json.dumps(audio))
            if page.instances() != before + 1:
                raise RuntimeError("choosing the audio did not start a new instance")
            stepped = page.run(STEP % count)
            if abs(stepped["time"] - count / 60) > 1e-9:
                raise RuntimeError(f"the page's clock reads {stepped['time']} after {count} steps")
            frames = count + 1
            if over:
                mix = page.run("import('./plugin.js').then(m => m.mounted.params.get('mix'))")
                page.after("import('./plugin.js').then(m => m.mounted.params.set('mix', 0))")
                cw, ch, frame = page.canvas()
                page.after(f"import('./plugin.js').then(m => m.mounted.params.set('mix', {mix}))")
            else:
                frame = bytes(w * h * 4)
            cw, ch, ours = page.canvas()
            if (cw, ch) != (w, h):
                raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
            host = page.run(HOST)

            if audio == "fft":
                render = functools.partial(edtest_plain, args.edtest, over, host, frame, frames, scratch)
            else:
                t = page.run(TRACK % (frames * PER_FRAME + RATE))
                if t["frames"] < frames * PER_FRAME:
                    raise RuntimeError("the page's track is shorter than the case")
                wav = os.path.join(scratch, "track.wav")
                write_wav(wav, base64.b64decode(t["b64"]), t["frames"])
                render = functools.partial(edtest_pipe, args.edtest, over, host, frame, frames, wav)
            theirs, alpha = render()
            c = compare(ours, theirs, w, h, alpha)
            ok = within(c)
            failures += not ok
            s = stepped["stats"]
            print(line("ok  " if ok else "FAIL", name, c))
            print(f"      page: {s['hz']:.2f} Hz{' (a note this frame)' if s['note'] else ''}, {s['relative']:.3f} of the peak, F0 {s['F0']:.4f} N,"
                  f" {s['steps']:.0f} substeps run, earshot {s['state']}, {s['fed']:.0f} samples fed")
            if first:
                first = False
                gain = dict(host)["Gain"]
                slip, _ = render(override={"Gain": gain + 0.001})
                n = compare(ours, slip, w, h, alpha)
                caught = not within(n)
                failures += not caught
                print(line("ok  " if caught else "FAIL", "negative: Gain one step (0.001) off (must fail)", n))
                shape = dict(host)["Shape"]
                wrong, _ = render(override={"Shape": 1.0 - shape})
                n = compare(ours, wrong, w, h, alpha)
                caught = not within(n)
                failures += not caught
                print(line("ok  " if caught else "FAIL", "coarse negative: the other Shape (must fail)", n))
            if args.keep:
                from PIL import Image
                os.makedirs(args.keep, exist_ok=True)
                stem = os.path.join(args.keep, "".join(ch if ch.isalnum() else "-" for ch in name))
                Image.fromarray(c["a"][..., :3].astype(np.uint8)).save(f"{stem}-page.png")
                Image.fromarray(c["b"][..., :3].astype(np.uint8)).save(f"{stem}-edtest.png")
                Image.fromarray(np.clip(c["rgb"] * 10, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x10.png")
        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all cases within the mark' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
