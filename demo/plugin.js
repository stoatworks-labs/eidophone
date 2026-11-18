/**
 * Eidophone — browser demo.
 *
 * A Chladni plate with sand on it, sounded by the music, as two FFGL plugins:
 * SW Eidophone (ED01), a source that is a metal plate seen from above with
 * sand on it, and SW Eido Over (ED02), an effect that prints the clip on the
 * plate under the sand. Nothing is drawn: a free plate is pushed at a point by
 * a force that follows the music's pitch and loudness and answers in its own
 * modes, and a grain can leave the plate only where the plate's acceleration
 * beats gravity, so the sand gathers on the nodal lines.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `eidophone-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Eidophone.cpp` — the plugin class, with
 * its constructor and its clock, every pass it draws, every uniform it sets —
 * and everything it calls: the plate's modes (Rayleigh–Ritz for the square,
 * Bessel roots for the circle, in double: Modes.cpp, Eigen.cpp), its
 * oscillators, exact per frame (Plate.cpp), the audio law (Audio.cpp) and,
 * under it, earshot's snapshot, built with no device backend at all
 * (EARSHOT_CAPTURE=OFF), with the FFGL SDK's CFFGLPlugin, its parameter
 * bookkeeping, FFGLShader and FFGLFBO. Its GL calls go to this page's WebGL2
 * context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin, the source or the
 *     Over (what SourcePlugin.cpp and EffectPlugin.cpp do in a bundle), reads
 *     its parameter declarations back through the SDK's host getters — the
 *     panel below is built from them, not from a list typed here — and
 *     forwards SetFloatParameter, SetTime, the Audio buffer's elements and
 *     ProcessOpenGL, as tools/edtest's Rig does.
 *   - **The clock preset and the audio, as the plugin's harness has them.**
 *     After InitGL each instance is told its host clock is in seconds
 *     (SetClockScaleForTest( 1.0 ), what edtest's Rig::Init calls) instead of
 *     voting on it. No device: the page never asks for a microphone and never
 *     lists an input. It feeds the plugin as edtest does, through earshot's
 *     feed() (AudioInput::Feed, the path a device's samples take inside
 *     earshot): the page's track, held notes generated in the wasm by edtest's
 *     own sine() on the default plate's modes, or a file the visitor chooses,
 *     decoded here. Or nothing fed, and edtest --bins 1's made-up 64-bin
 *     buffer in the Audio parameter: the plugin's own fallback, the one behind
 *     its default Device, "Resolume FFT (no pitch)", on a synthetic FFT.
 *   - **Five GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()`. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE, which the plugin sets around its splat and saves
 *     and restores every frame, does not exist in WebGL2 (where a point is
 *     always the vertex shader's gl_PointSize anyway). glTexImage2D: the FFGL
 *     SDK allocates the RG32F field and the RG16F density with a client format
 *     of GL_RGBA, which WebGL2 refuses for those formats; with no data, they
 *     are given GL_RG.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's ed_convert); the units, the tooltips
 *     and the presets are this page's. The presets are written in centimetres,
 *     g, Q and so on and go to the host's 0..1 through the plugin's own inverse
 *     functions (ed_param_from).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Every audio choice, plugin choice and Restart is a new instance, from time
 * 0.** earshot's feed, once begun, is what its engine reads until a
 * selectDevice, and any select ends it; so the page never switches a running
 * instance between sources. Changing the audio restarts the clock, and the new
 * instance's feed starts at the track's beginning.
 *
 * **The clock preset rather than the vote.** In Resolume the plugin votes for
 * its first four frames on whether SetTime is in seconds or milliseconds,
 * against the wall clock, running on the wall clock until then. Here its first
 * frame builds the mode table in WebAssembly, which takes a wall-clock
 * fraction of a second; under the vote that would be up to 32 substeps of
 * grain walk (and a different random stream) the harness never takes. So the
 * page presets it, as edtest does, and the same frames, fps and feed reproduce
 * edtest's picture (AGENTS.md, "Browser demo").
 *
 * **The feed leads by one frame**, as edtest's: before the frame at page time
 * t, every sample up to t + 1/60 s has been fed.
 *
 * **Pour is a button.** It is FF_TYPE_EVENT; a press sends 1 then 0, and the
 * plugin pours on the rising edge at its next frame.
 *
 * **Left off the panel**: Device and Rescan (no device exists here), the Audio
 * buffer (Resolume's FFT, which the page writes only in the "Resolume FFT"
 * choice) and the About block, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createEidophone from './eidophone-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_BUFFER = 12;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of Assemble(), SplatVertex() and InitGL by
// check_shaders.py), one text per stage.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.filter((s) => s.stage === stage && s.text === text);
  if (!ours.length) {
    shaderCheck.refused += 1;
    return '';
  }
  for (const s of ours) shaderCheck.matched.add(s.name);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createEidophone({
    eidophoneShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/eidophone.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

// Before any instance: the plugin's log in the page's memory, and verify.sh's
// switch that keeps AudioInput::Refresh from asking for a device list.
core._ed_prepare();

//===========================================================================
// The declarations, read from each plugin's constructor through the SDK.
//===========================================================================

function declarations(effect) {
  const h = core._ed_new(effect ? 1 : 0);
  const list = [];
  const n = core._ed_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._ed_param_type(h, i);
    const d = { index: i, name: text(core._ed_param_name(h, i)), type, group: text(core._ed_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._ed_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._ed_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._ed_param_element_name(h, i, e)));
        d.values.push(core._ed_param_element_value(h, i, e));
      }
    }
    list.push(d);
  }
  const inputs = [core._ed_min_inputs(h), core._ed_max_inputs(h)];
  // Kept, never given GL: the read-outs and the presets ask it for
  // Controls.cpp's conversions.
  return { list, inputs, converter: h };
}

const DECLARED = { source: declarations(false), over: declarations(true) };
if (DECLARED.source.inputs.join() !== '0,0' || DECLARED.over.inputs.join() !== '1,1') {
  throw new Error(`the plugins declare ${DECLARED.source.inputs.join('–')} and ${DECLARED.over.inputs.join('–')} inputs; this page hands the source none and the Over exactly one`);
}

/** Controls.cpp's conversion of a host value, for a declaration of a plugin. */
const convert = (variant, d, v) => core._ed_convert(DECLARED[variant].converter, d.index, Math.fround(v));
/** And its inverse: the host's 0..1 for a value in the plugin's units. */
const paramFrom = (variant, d, units) => core._ed_param_from(DECLARED[variant].converter, d.index, units);

//===========================================================================
// What the panel leaves out, by the plugin's own names: the device menu and
// its rescan (no device exists here), Resolume's FFT buffer (written only by
// the "Resolume FFT" audio choice) and the About block.
//===========================================================================

const LEFT_OUT = ['Device', 'Rescan', 'Audio'];
const onPanel = (d) => d.group !== 'About' && !LEFT_OUT.includes(d.name);
for (const variant of ['source', 'over']) {
  for (const name of LEFT_OUT) {
    if (!DECLARED[variant].list.some((d) => d.name === name)) throw new Error(`the plugin no longer declares ${name}; this page's notes say it is left out`);
  }
}
/** The plugin's default Device, element 0, as its constructor declares it. */
const DEVICE_DEFAULT = (() => {
  const d = DECLARED.source.list.find((x) => x.name === 'Device');
  const i = d.values.findIndex((v) => v === d.default);
  return d.elements[i >= 0 ? i : 0];
})();

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
// Short: the read-out column is about ten characters wide. The tooltips say
// what each is.
const UNITS = {
  Size: (x) => `${fixed(x * 100, 1)} cm`,
  Thickness: (x) => `${fixed(x * 1000, 2)} mm`,
  'Drive At': (x) => (x === 0 ? 'centre' : fixed(x, 2)),
  Damping: (x) => `Q ${fixed(x, x < 10 ? 1 : 0)}`,
  Gain: (x) => `${fixed(x, x < 10 ? 1 : 0)} g`,
  Clarity: (x) => fixed(x, 3),
  Amount: (x) => Math.round(x).toLocaleString('en-GB'),
  Hop: (x) => `×${fixed(x, 2)}`,
  Air: (x) => fixed(x, 0),
  'Grain Size': (x) => `${fixed(x, 2)} px`,
};

function readout(d, variant) {
  return (v) => {
    const x = convert(variant, d, v);
    if (Number.isNaN(x)) return d.name === 'Mix' ? `${fixed(v * 100, 0)}%` : fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Shape: 'Square or Circle. Pushed at its centre, a round plate can ring only in its axisymmetric modes: rings. Move Drive At off the centre and the diameters appear. A change of Shape pours the sand again.',
  Size: 'The side of the square or the diameter of the circle, 10–60 cm (geometric). A bigger plate has lower modes: every mode’s frequency goes as 1/size².',
  Thickness: '0.3–3 mm of plate (geometric). Every mode’s frequency goes as the thickness.',
  Material: 'Aluminium 6061-T6, brass C26000 or stainless 304, each from a datasheet: Young’s modulus, density and Poisson’s ratio. A different Poisson’s ratio is a different set of mode shapes.',
  'Drive At': 'Where the plate is pushed: 0–0.8 of the way from the centre to the edge, along +x (the read-out is that fraction). At the centre only the modes symmetric about it ring.',
  Damping: 'The plate’s Q, 5–500 (geometric): how sharply each mode answers only its own note, and how fast it rings down. An assumption: nothing published was found for a sand-loaded plate on a driver.',
  Gain: 'How hard the loudest recent passage shakes the plate: its peak acceleration in g, 0.5–40 (geometric), at any note. Louder draws thinner lines; a passage more than 20·log₁₀(Gain) dB under the recent peak leaves the plate under g and the figure frozen.',
  Clarity: 'How sure earshot’s pitch tracker must be to call it a note, 0.5–0.99. Acts on the fed audio (the device path); the FFT fallback has no pitch to be sure of.',
  'Bin Law': 'How the fallback reads Resolume’s 64 FFT bins as frequencies: Linear or Log, at an assumed 44.1 kHz. FFT only: it acts on the “Resolume FFT” audio choice in the host row, where Linear puts the guessed note at 516.8 Hz and Log at 23.6 Hz.',
  Grain: 'Sand (hops by Newton’s threshold and walks toward the nodal lines), Powder (carried by the air currents toward the antinodes, the negative figure) or Both. A change pours again.',
  Amount: '1,024–262,144 grains (geometric). The update runs over every grain in use, 480 times a second.',
  Hop: '×0.25–×4 how far a thrown grain goes (geometric). An assumption, chosen, not derived.',
  Air: '0–100: how strongly powder is carried by the air over the plate. An assumption, chosen, not derived.',
  Pour: 'Strew the grains again, evenly. FF_TYPE_EVENT: a press sends 1 then 0, and the plugin pours on the rising edge at its next frame.',
  View: 'Fit: the whole plate in the frame. Fill: the plate covers the frame (the Over’s default, so the clip fills the frame too).',
  'Grain Size': '0.5–4 px a grain at 1080 lines (geometric), scaled with the raster.',
  Mix: 'SW Eido Over only. 0 hands the clip straight back, untouched.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed). Shared names whose declarations
// agree but for the default are one row (View's default differs); a name
// only one plugin declares carries `only`.
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  // Pour: a kit toggle, replaced by a button below.
  [FF_TYPE_EVENT]: 'boolean',
};

/** The kit reads these from the query string itself, and this page reads `plugin` and `audio`. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'plugin', 'audio']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

/** The same control in both plugins: everything but its host index and default. */
const sameControl = (a, b) => JSON.stringify({ ...a, index: 0, default: 0 }) === JSON.stringify({ ...b, index: 0, default: 0 });

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';
function currentVariant() { return mounted?.state.variant ?? initialVariant; }

const PARAMS = [];
{
  const over = DECLARED.over.list.filter(onPanel);
  const source = DECLARED.source.list.filter(onPanel);
  for (const d of [...source, ...over]) {
    if (KIT_TYPES[d.type] === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  }
  // The source's order, then anything only the Over declares, after the last
  // row of its own group (Mix, in View, last).
  for (const s of source) {
    const d = over.find((x) => x.name === s.name);
    if (d && !sameControl(s, d)) throw new Error(`${s.name}: the two plugins declare it differently, and this page has no row for that`);
    const p = { id: addressOf(s.name), name: s.name, type: KIT_TYPES[s.type], group: s.group, hint: HINTS[s.name], ff: { source: s, over: d ?? null } };
    if (!d) p.only = 'source';
    PARAMS.push(p);
  }
  for (const d of over) {
    if (source.some((x) => x.name === d.name)) continue;
    const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: { source: null, over: d }, only: 'over' };
    let at = PARAMS.length;
    for (let i = PARAMS.length - 1; i >= 0; i -= 1) if (PARAMS[i].group === d.group) { at = i + 1; break; }
    PARAMS.splice(at, 0, p);
  }
  for (const p of PARAMS) {
    const d = p.ff.source ?? p.ff.over;
    if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
    if (d.type === FF_TYPE_STANDARD) p.display = (v) => readout(p.ff[currentVariant()] ?? d, p.only ?? currentVariant())(v);
  }
}

/** Each constructor's defaults, by kit id; a row the plugin lacks keeps the other's. */
const DEFAULTS = { source: {}, over: {} };
for (const p of PARAMS) {
  for (const variant of ['source', 'over']) {
    DEFAULTS[variant][p.id] = kitDefault(p.ff[variant] ?? p.ff.source ?? p.ff.over);
  }
}
for (const p of PARAMS) p.default = DEFAULTS[initialVariant][p.id];

//===========================================================================
// The audio: the stand-in for what reaches the plugin in Resolume.
//===========================================================================

const FEED_RATE = 48000;
/** The feed's lead: edtest feeds frame f up to the end of its interval, (f + 1) / fps. */
const FEED_LEAD = 1 / 60;
const TRACK = (() => {
  const frames = core._ed_track_frames();
  const notes = [];
  for (let i = 0; i < core._ed_track_note_count(); i += 1) {
    notes.push({ hz: core._ed_track_note(i, 0), seconds: core._ed_track_note(i, 1), start: core._ed_track_note(i, 2), frames: core._ed_track_note(i, 3) });
  }
  const centre = [];
  for (let i = 0; Number.isFinite(core._ed_centre_mode_hz(i)); i += 1) centre.push(core._ed_centre_mode_hz(i));
  return { frames, seconds: frames / FEED_RATE, notes, centre };
})();

/** What a note of the track is, in words: the page's reading of its own plan. */
function noteWords(note) {
  const k = TRACK.centre.findIndex((hz) => Math.abs(hz - note.hz) < 1e-9);
  const hz = `${fixed(note.hz, 2)} Hz`;
  if (k === 1) return `${hz}, the second centre mode (the four-pointed star)`;
  if (k >= 0) return `${hz}, centre mode ${k + 1}`;
  return `${hz} (D2), between centre modes 1 and 2`;
}

/** edtest --bins J's bin: the one at 0.5. */
const HOST_BIN = 1;
const AUDIO = [
  { id: 'track', name: 'The page’s track: five held notes on the plate',
    hint: `Held sines at −12 dBFS (edtest --tone’s level), each made by edtest’s own sine() in the WebAssembly with a 50 ms fade at each end and 1.5 s of silence after it, looped every ${fixed(TRACK.seconds, 1)} s: ${TRACK.notes.map((n) => `${noteWords(n)} for ${n.seconds} s`).join('; ')}. The frequencies come from the plugin’s own mode table for the default plate; the last is edtest --tone 0. Fed through earshot’s feed() as a device’s samples would arrive, on both channels.` },
  { id: 'fft', name: `${DEVICE_DEFAULT}: edtest --bins ${HOST_BIN}’s made-up FFT`,
    hint: `The plugin’s own default Device, “${DEVICE_DEFAULT}”: nothing is fed, and the plugin’s fallback guesses the note from the strongest of the 64 bins of Resolume’s FFT buffer. Here that buffer is edtest --bins ${HOST_BIN}’s synthetic one, written once: bin ${HOST_BIN} at 0.5, bin 0 at 0.9 (the fallback never takes bin 0), the rest at 0.02. Not an analysis of any sound. Bin Law decides the note: Linear puts bin ${HOST_BIN} at 516.8 Hz, Log at 23.6 Hz.` },
  { id: 'file', name: 'Your audio file',
    hint: 'A file you choose, decoded in this page by the browser and fed through feed(), looped. It is not uploaded and not played aloud.' },
];
const audio = {
  id: AUDIO.some((a) => a.id === query.get('audio') && a.id !== 'file') ? query.get('audio') : 'track',
  file: null, // { name, left: Float32Array, right: Float32Array, seconds, channels }
  epoch: 0, // bumped on every change of audio: a new instance
};

/**
 * Decode a file the visitor chose. An OfflineAudioContext renders to memory,
 * not to an output device; decodeAudioData resamples to its 48 kHz. Nothing
 * here opens a device or asks for one, and nothing is played.
 */
async function decodeFile(file) {
  const bytes = await file.arrayBuffer();
  const context = new OfflineAudioContext(2, 1, FEED_RATE);
  const buffer = await context.decodeAudioData(bytes);
  const left = buffer.getChannelData(0);
  const right = buffer.numberOfChannels > 1 ? buffer.getChannelData(1) : left;
  if (!left.length) throw new Error('it holds no samples');
  return { name: file.name, left: new Float32Array(left), right: new Float32Array(right), seconds: left.length / FEED_RATE, channels: buffer.numberOfChannels };
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (metres,
// g, Q, grains, a factor) and is converted by the plugin's own inverse
// (ed_param_from); an option is its element's name; a bare number is the
// host's 0..1. They are the page's choices, and none touches Mix.
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: a 40 cm square of 1 mm aluminium, pushed at its centre', {}],
  ['A circle pushed at its centre: rings only', { Shape: 'Circle' }],
  ['A circle pushed 0.4 of the way out: the diameters appear', { Shape: 'Circle', 'Drive At': u(0.4) }],
  ['The square pushed half way out: the odd modes join in', { 'Drive At': u(0.5) }],
  ['Powder: the negative figure, heaped at the antinodes', { Grain: 'Powder' }],
  ['Sand and powder together', { Grain: 'Both' }],
  ['Louder: Gain 30 g, thinner lines', { Gain: u(30) }],
  ['Quieter: Gain 2 g, only the strongest antinodes clear', { Gain: u(2) }],
  ['Lightly damped: Q 200, each mode answers only its own note', { Damping: u(200) }],
  ['A 30 cm brass plate: other modes, so the track’s notes miss them', { Size: u(0.30), Material: 'Brass' }],
  ['Long hops: Hop ×3, figures form faster and coarser', { Hop: u(3) }],
  ['262,144 grains, the most Amount allows', { Amount: u(262144) }],
];

/**
 * Settings by the plugin's own names as the kit's values: {units} goes
 * through the plugin's ParamFrom*, an option is its element's name, a bare
 * number the host's 0..1. (demo/tools/crosscheck.py uses it too.)
 */
function kitValues(variant, spec, what = 'settings') {
  const values = {};
  for (const [name, value] of Object.entries(spec)) {
    const p = PARAMS.find((x) => x.name === name);
    if (!p) throw new Error(`${what}: no control called ${name}`);
    const d = p.ff[variant] ?? p.ff.source ?? p.ff.over;
    if (p.type === 'option') {
      const slot = p.elements.indexOf(value);
      if (slot < 0) throw new Error(`${what}: ${name} has no element ${value}`);
      values[p.id] = slot;
    } else {
      const host = typeof value === 'object' ? paramFrom(p.ff[variant] ? variant : (p.only ?? variant), d, value.units) : value;
      if (!Number.isFinite(host) || host < -1e-6 || host > 1 + 1e-6) throw new Error(`${what}: ${name} ${JSON.stringify(value)} is outside the control`);
      values[p.id] = Math.min(1, Math.max(0, host));
    }
  }
  return values;
}

function kitPresets(variant) {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) out[title] = kitValues(variant, spec, `preset “${title}”`);
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;
let mounted = null;

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  if (!d) return null;
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null; // sent by its button, as a press
    case FF_TYPE_TEXT:
    case FF_TYPE_BUFFER: return null;
    default: return Math.fround(v);
  }
}

const PLUGIN_NAME = { source: 'SW Eidophone', over: 'SW Eido Over' };

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. The kit has
  // already enabled EXT_color_buffer_float on it (needFloat), which the
  // plugin's RG32F, RGBA32F and RG16F targets need; nothing else is turned on
  // on the plugin's behalf.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  let instance = 0;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let instances = 0;
  let lastTime = null;
  let epoch = -1;
  let variant = null;
  let source = null;
  // The visitor's file, fed as edtest's Feeder feeds a WAV: the absolute
  // sample clock at 48 kHz, samples [0, fileSent) gone to feed(), looped.
  let fileSent = 0;
  let scratch = 0;
  let scratchFrames = 0;
  // The page's own frame rate, for the line under the picture.
  const stamps = [];

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(which, width, height) {
    if (instance) core._ed_delete(instance);
    variant = which;
    instance = core._ed_new(which === 'over' ? 1 : 0);
    pushed = new Map();
    instances += 1;
    fileSent = 0;
    shaderCheck.matched.clear();
    source = audio.file || audio.id !== 'file' ? audio.id : 'track';
    if (!core._ed_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`${PLUGIN_NAME[which]} could not start: ${why}.`);
    }
    // Every stage the page has a copy of must have been handed over: a stage
    // the plugin stopped compiling is a copy nothing checks any more.
    const missing = PAGE_SHADERS.filter((s) => !shaderCheck.matched.has(s.name)).map((s) => s.name);
    if (missing.length) throw new GLError(`${PLUGIN_NAME[which]} compiled without the page’s ${missing.join(', ')}: the page’s copy no longer describes it.`);
    // The fed choices go through feed() from here on: earshot's
    // feedBegin(48000, 2), its state Running, as edtest's Feeder does it. The
    // FFT choice feeds nothing and writes edtest --bins's buffer once.
    if (source === 'fft') core._ed_host_bins(instance, HOST_BIN);
    else core._ed_feed_begin(instance);
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p.ff[variant], params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._ed_set_float(instance, p.ff[variant].index, value);
    }
  }

  function feedFile(end) {
    const f = audio.file;
    const n = Math.min(Math.max(0, Math.round(end) - fileSent), 4 * FEED_RATE);
    if (n <= 0) return;
    if (n > scratchFrames) {
      if (scratch) core._free(scratch);
      scratchFrames = Math.max(n, FEED_RATE);
      scratch = core._malloc(scratchFrames * 2 * 4);
    }
    const out = core.HEAPF32.subarray(scratch >> 2, (scratch >> 2) + 2 * n);
    const length = f.left.length;
    for (let k = 0; k < n; k += 1) {
      const at = (fileSent + k) % length;
      out[2 * k] = f.left[at];
      out[2 * k + 1] = f.right[at];
    }
    core._ed_feed(instance, scratch, n, FEED_RATE, 2);
    fileSent += n;
  }

  /** The audio for the frame at page time `time`, before the plugin renders it. */
  function feed(time) {
    const end = (time + FEED_LEAD) * FEED_RATE;
    if (source === 'file') feedFile(end);
    else if (source === 'track') core._ed_feed_track(instance, end);
  }

  return {
    render({ input, params, width, height, time, variant: wanted }) {
      core.GL.makeContextCurrent(handle);
      // A new instance for a change of plugin or audio, and for Restart (the
      // page's clock going back).
      const fresh = !instance || wanted !== variant || epoch !== audio.epoch || (lastTime !== null && time < lastTime);
      lastTime = time;
      epoch = audio.epoch;
      if (fresh) start(wanted, width, height);
      push(params);
      feed(time);
      const clip = variant === 'over' ? glName(input.texture) : 0;
      if (!core._ed_process(instance, time, clip, input.width, input.height)) {
        const log = this.log().trim().split('\n').filter(Boolean).slice(-1)[0] ?? '';
        throw new GLError(`The plugin’s ProcessOpenGL failed.${log ? ` Its log: ${log.replace(/^\S+ /, '')}` : ''}`);
      }

      // A setter WebGL2 refuses (a float sent to an int, say) fails silently
      // but for this: asked on an instance's first frames only, as a sync
      // point is not free. demo/tools/check_shaders.py checks the setters
      // statically.
      frames += 1;
      if (fresh || frames % 600 === 0) {
        const error = gl.getError();
        if (error !== gl.NO_ERROR) {
          gl.canvas.dataset.glError = `0x${error.toString(16)}`;
          console.error(`WebGL reported error 0x${error.toString(16)} on frame ${frames} of ${PLUGIN_NAME[variant]}`);
        }
      }
      const now = performance.now();
      stamps.push(now);
      while (stamps.length && stamps[0] < now - 2000) stamps.shift();
      // For anything checking the page from outside.
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.instances = String(instances);
      gl.canvas.dataset.variant = variant;
      gl.canvas.dataset.audio = source;
      gl.canvas.dataset.hz = String(core._ed_frame(instance, 0));
      gl.canvas.dataset.steps = String(core._ed_step_counter(instance));
    },
    /** An FF_TYPE_EVENT press: 1 then 0, the plugin latching the rising edge. */
    press(index) {
      if (!instance) return false;
      core._ed_set_float(instance, index, 1);
      core._ed_set_float(instance, index, 0);
      return true;
    },
    stats() {
      if (!instance || !frames) return null;
      const f = (i) => core._ed_frame(instance, i);
      const hz = f(0);
      const span = stamps.length > 1 ? (stamps[stamps.length - 1] - stamps[0]) / 1000 : 0;
      return {
        variant,
        source,
        hz,
        note: f(1) === 1,
        seeded: f(2) === 1,
        fromDevice: f(3) === 1,
        bin: f(4),
        loudness: f(5),
        relative: f(6),
        F0: f(7),
        substeps: f(8),
        grains: f(9),
        halfSize: f(11),
        nearest: core._ed_nearest_mode_hz(instance, hz),
        modes: core._ed_mode_count(instance),
        state: text(core._ed_capture_state_name(instance)),
        now: core._ed_host_now(instance),
        steps: core._ed_step_counter(instance),
        fed: source === 'file' ? fileSent : core._ed_fed(instance),
        fps: span > 0.5 ? (stamps.length - 1) / span : null,
        frames,
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._ed_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const notesList = TRACK.notes.map((n) => noteWords(n)).join('; ');
const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. eidophone-core.wasm is source/Eidophone.cpp — the plugin class: its constructor, its clock, every pass it draws, every uniform and the GL state it saves and puts back — and everything it calls (Modes, Eigen, Plate, Audio, Controls, PassBuffer, Shaders, Diag), with earshot’s snapshot (8124191, unpatched) and the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLFBO, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The plate’s modes (Rayleigh–Ritz for the square, Bessel roots for the circle) and its oscillators are that C++, in double; the grains are its GLSL. Only the two registrations (SourcePlugin.cpp, EffectPlugin.cpp) are left out; the page constructs the plugin itself, as its harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  `No audio device, and no microphone: the page never asks for one, never lists one and makes no sound. earshot is compiled here with no capture backend at all (EARSHOT_CAPTURE=OFF: its NoCapture.cpp, and miniaudio is not compiled), and the plugin is told not to ask for a device list (EIDOPHONE_NO_DEVICES=1, its verification switch). So three of the plugin’s controls are not on this panel: Device (the menu: “${DEVICE_DEFAULT}”, “System default”, then each capture device, remembered by name), Rescan, and the Audio buffer (Resolume’s FFT, which a host writes and nobody sets). The live-device path — a device opened on earshot’s worker thread, Missing and Failed, the amber and the pulsing red rim on the plate, the remembered device name — is not in the browser.`,
  `The page’s audio differs from the plugin’s default. The plugin’s default Device is “${DEVICE_DEFAULT}”: in Resolume, with no capture device chosen, there is no pitch at all, and the note is a guess from the loudest of Resolume’s 64 FFT bins (under Linear they are 345 Hz wide, and every bass note lands in the first two). This page’s default is its track, handed over as the plugin’s harness hands audio over: through earshot’s feed() (AudioInput::Feed), the path a device’s samples take inside earshot, so earshot finds a real pitch, as it would from a capture device. The track is held sines at −12 dBFS made by edtest’s own sine() (copied character for character and held to tools/edtest by the repository’s verify script), with a 50 ms fade at each end and 1.5 s of silence after each, looped every ${fixed(TRACK.seconds, 1)} s: ${notesList}. Their frequencies come from the plugin’s own mode table, the last being edtest --tone 0, the centre mode nearest 400 Hz. Before the frame at page time t, every sample up to t + 1/60 s has been fed at 48 kHz, as edtest feeds frame f up to the end of its interval. Your own audio file is decoded by this browser (Web Audio’s decodeAudioData, in an OfflineAudioContext, resampled to 48 kHz; a mono file goes to both channels), never uploaded, and fed the same way, looped; edtest --wav plays a file once. Nothing is played aloud.`,
  `“${DEVICE_DEFAULT}”, the plugin’s own default, is offered in the host row on a made-up FFT: nothing is fed, and the plugin’s own fallback code (Audio.cpp’s DriveFrom) takes the strongest of the 64 bins but bin 0 as its guess at the note, at that bin’s centre under Bin Law and an assumed 44.1 kHz, and the bin’s value as the loudness. The buffer is edtest --bins ${HOST_BIN}’s, written once: bin ${HOST_BIN} at 0.5, bin 0 at 0.9, the rest at 0.02 — not an analysis of any sound. So the plate holds one note, 516.8 Hz under Linear or 23.6 Hz under Log, and Clarity does nothing. In Resolume the buffer is Resolume’s own FFT, which nobody has measured.`,
  'Known limits a visitor will meet, the plugin’s, not the page’s. High notes draw slowly: at the same Gain a grain’s hop shrinks as 1/f², so the track’s 428.69 Hz note takes far longer to draw than its bass notes: a note an octave up takes four times as long. The default plate’s lowest mode, near 37.5 Hz, is under earshot’s 40 Hz pitch floor, so no fed note can ring it (only the FFT fallback or a note’s overtones could); the track does not try. One note at a time: earshot gives one pitch, and a chord is not separated. How far a grain hops (Hop), how strongly powder is carried (Air, the powder current) and the plate’s damping (Q) are assumptions, chosen, not measured or derived. No sand falls off the edge, no heap has an angle of repose, and grains do not collide.',
  'Every change of audio or plugin, and Restart, is a new instance from time 0: a feed, once begun, is what earshot reads until a device is selected, and the page never selects one. After InitGL each instance is told its host clock is in seconds, as edtest’s Rig::Init tells it (SetClockScaleForTest). In Resolume it votes for its first four frames on whether SetTime is in seconds or milliseconds, against the wall clock, and runs on the wall clock until then; here its first frame builds the mode table in WebAssembly, and under the vote that wall-clock time would become grain walk the harness never takes. So the page’s picture is edtest’s for the same frames, fps and feed (AGENTS.md, “Browser demo”, gives the measured difference). The first frame after a change of Shape or Material builds that plate’s mode table, once for each shape and material per visit, and uploads its mode shapes again on every change; the page holds still meanwhile (measured in Chrome on an M4 Max: half a second for the circle’s first table, a tenth of a second for a change to a table already built).',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime. A frame here is one of the display’s refreshes, not Resolume’s output frame. The plate’s oscillators are exact for any step and the grains walk in 1/480 s substeps whatever the frame rate, at most 32 a frame: below 15 frames a second the plugin drops the excess, as it would in Resolume, and the sand slows. Paused, a moved control redraws the frame with no time passing (no substep runs, no audio is fed). Step advances the clock 1/60 s, eight substeps.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order Assemble(), SplatVertex() and InitGL join the pieces in for its five programs, and the repository’s verify script fails if a character differs, or if a uniform is set by a call its GLSL type does not take. At start-up the page also requires every stage the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults (highp, which the grain walk’s PCG hash needs for its 32-bit integers). GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and five entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port; glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE, which the plugin sets around its splat of points and saves and restores each frame, does not exist in WebGL2, where a point is always the size its vertex shader writes (the plugin’s own gl_PointSize, so the grains are the plugin’s size); and glTexImage2D, because the FFGL SDK allocates the RG32F field and the RG16F density with a client format of GL_RGBA, which WebGL2 refuses for those two formats (it takes GL_RG). With no data uploaded nothing about what is allocated changes. The page needs EXT_color_buffer_float to render into RG32F, RGBA32F and RG16F, and stops without it; the plugin reads none of them back.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its sin, cos, exp, pow and the rest are musl’s, not Apple’s, so the plate’s doubles can differ from the plugin built for a Mac in the last digits; and this browser’s GPU, through its own GL layer, walks and draws the grains, not Apple’s OpenGL. Measured on one Mac (an M4 Max, Chrome, 640×360, eight cases across both plugins and both audio paths, 3 to 10 seconds in), the page’s frame agreed with the plugin harness’s for the same settings, audio and frames in all but 0.3% of its colour values or fewer, and to within a third of a code averaged over every 8×8 block; Gain one slider step off changed 2.7%. The grain walk is chaotic, so on another GPU a last-bit difference can send a grain elsewhere a second later: the sand gathers on the same lines, but not grain for grain.',
  'The clip of SW Eido Over is the kit’s generated one, or your own image or video. The kit hands over premultiplied colour, and the plugin treats its input as straight colour with alpha; the page’s own clips are all opaque, which is why the kit’s shape-on-transparency clip is not among them. The Over’s output is straight colour with alpha and the page’s canvas takes premultiplied colour, so where the Over’s output is partly transparent (the plate’s anti-aliased rim in Fit) it shows a little brighter than it would in Resolume. The page shows it over black.',
  'Flashing: the plugin’s release survey (xenon’s flashcheck rule, the whole frame and each third of it, 462 runs of SW Eidophone across 14 settings and 198 of SW Eido Over across 6, on Resolume’s demo music and clips) found no setting over three flashes in any second, so this page carries no warning and keeps the plugin’s defaults.',
  'The plugin itself has never been loaded into Resolume, and has never opened a live audio device; its bundles are read and rendered through by a host-shaped tool (its README’s Status says what was seen). Its harness, edtest, drives the same class headlessly on a synthetic clock and reads the plate and the grains back: the square’s modes match a published series solution to 3.4e-5, the circle’s Bessel roots an independent solver to 6.6e-10, each mode’s response its closed form to 9.5e-16; no grain under g moved in any bit over 757,980 grain-substeps; driven in one mode, all the sand settles on that mode’s nodal lines; and 21 wrong models and 26 one-character mutants are caught. That harness, not this page, is the reason to believe the plate. The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'Eidophone',
  pluginId: 'ED01 · ED02',
  kind: ['source', 'effect'],
  tagline:
    'A Chladni plate with sand on it, sounded by the music, for Resolume. Nothing is drawn. A free metal plate, square or round, is pushed at its centre by a force that follows the music’s pitch and loudness and answers in its own modes, each a damped resonator; a grain can leave the plate only where the plate’s acceleration beats gravity, so the sand is thrown off everywhere but the nodal lines and gathers there into Chladni’s figures. A new note half-redraws the old figure, a quiet passage freezes it exactly, and fine powder heaps up the other way. SW Eidophone is the plate on black; SW Eido Over prints your clip on the plate under the sand.',
  repo: 'https://github.com/stoatworks-labs/eidophone',

  blurb:
    `It is the plugin’s own C++ — the plugin class, the plate’s modes and oscillators in double, its audio law and earshot under it — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page, and it is not Resolume: no audio device and no microphone (the Device menu, Rescan and the live-device path are not in the browser), and not the plugin’s default “${DEVICE_DEFAULT}” either, where the note is a guess from the loudest of Resolume’s 64 FFT bins. The audio is a track of held notes on the plate’s modes, or a file of yours, fed through earshot’s feed() as the plugin’s harness feeds it, so the plate gets a real pitch; the FFT fallback runs only on a made-up 64-bin buffer. SW Eido Over runs on a generated clip. The plugin’s own limits hold here too: high notes draw slowly, the plate’s lowest mode is under earshot’s pitch floor, one note at a time, and how far a grain hops, the powder’s air current and the damping are assumptions. The notes at the foot say the rest.`,

  // The field RG32F, the grain state RGBA32F, the density RG16F, as in the plugin.
  needFloat: true,
  // Shown over black: the source is opaque; the Over is opaque in Fill, and
  // transparent outside the plate in Fit, as over an empty composition.
  showBackdrop: false,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Eidophone (source)', hint: 'ED01, FF_SOURCE: the plate on black, the whole plate in the frame (View Fit). Reads no clip.' },
      { id: 'over', name: 'SW Eido Over (effect)', hint: 'ED02, FF_EFFECT: the clip printed on the plate under the sand, the plate covering the frame (View Fill).' },
    ],
  },

  // For SW Eido Over only, and all opaque: the kit's 'alpha' clip is left
  // out because the kit's premultiplied colour is not the straight colour the
  // plugin expects (see the notes at the foot).
  sources: ['scene', 'bars', 'grid', 'detail', 'spot', 'ramp'],

  params: PARAMS,
  presets: kitPresets(initialVariant),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// Which controls belong to which plugin, Pour, the host row and the lines
// under the picture.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  let shownVariant = mounted.state.variant;
  // The constructor's defaults of the plugin shown, for Defaults too.
  params.defaults = { ...DEFAULTS[shownVariant] };

  // Copy link carries the plugin and the audio choice (a file cannot travel in a link).
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    if (audio.id !== 'track' && audio.id !== 'file') q.set('audio', audio.id);
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(which) {
    for (const p of PARAMS) if (p.only) show(rowOf(p), p.only === which);
    const effect = which === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport:not(#eidophone-host) .transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const which = mounted.state.variant;
    if (which !== shownVariant) {
      shownVariant = which;
      // A new instance: that constructor's defaults, for Defaults too.
      params.defaults = { ...DEFAULTS[which] };
      params.reset();
    }
    if (!embed) showForVariant(which);
  });

  if (!embed) {
    showForVariant(shownVariant);
    // The kit's note says every declared parameter is here; four are not.
    const note = document.querySelector('.inspector__note');
    if (note) {
      note.textContent += ' Left out here: Device and Rescan (no audio device exists in the browser), the Audio buffer (Resolume’s FFT; the host row under the picture stands in for all three) and the About block.';
    }
    buildEventButtons();
    buildHostRow();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// Pour: FF_TYPE_EVENT, a press and a release.
//---------------------------------------------------------------------------
function buildEventButtons() {
  const rowOf = (p) => [...document.querySelectorAll('.prow')]
    .find((row) => row.querySelector('.prow__name')?.textContent === p.name);
  for (const p of PARAMS) {
    if ((p.ff.source ?? p.ff.over).type !== FF_TYPE_EVENT) continue;
    const toggle = rowOf(p)?.querySelector('.prow__toggle');
    if (!toggle) continue;
    const press = document.createElement('button');
    press.type = 'button';
    press.className = 'prow__toggle ed-press';
    press.id = `p-${p.id}`;
    press.textContent = p.name;
    if (p.hint) press.title = p.hint;
    press.addEventListener('click', () => {
      const d = p.ff[currentVariant()];
      if (d && renderer?.press(d.index)) mounted.redraw();
    });
    toggle.replaceWith(press);
  }
}

//---------------------------------------------------------------------------
// The host row: the stand-in for what Resolume would hand the plugin.
//---------------------------------------------------------------------------
function field(label, control, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  // Narrow screens: the field may shrink, and the control with it.
  wrap.style.maxWidth = '100%';
  wrap.style.minWidth = '0';
  control.style.maxWidth = '100%';
  control.style.minWidth = '0';
  wrap.append(span, control);
  return wrap;
}

/** Restart the page's clock: a new instance from time 0, as Restart does. */
function restartClock() {
  audio.epoch += 1;
  mounted.state.time = 0;
  mounted.state.frameIndex = 0;
  mounted.redraw();
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport';
  row.id = 'eidophone-host';
  const tag = document.createElement('span');
  tag.className = 'transport__label';
  tag.textContent = 'The host:';
  tag.title = 'The stand-in for what Resolume would hand the plugin. Not a plugin control.';

  const select = document.createElement('select');
  select.className = 'transport__select';
  select.id = 'eidophone-audio';
  for (const a of AUDIO) {
    const option = new Option(a.name, a.id);
    option.title = a.hint;
    if (a.id === 'file') option.disabled = true;
    select.append(option);
  }
  select.value = audio.id;
  select.addEventListener('change', () => {
    audio.id = select.value;
    restartClock();
  });

  const pick = document.createElement('label');
  pick.className = 'btn transport__file';
  pick.id = 'eidophone-audio-file-label';
  pick.textContent = 'Audio file…';
  pick.title = 'Choose a sound file on this machine. The browser decodes it here and the page feeds it to the plugin through feed(); it is not uploaded and not played aloud.';
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = 'audio/*';
  input.id = 'eidophone-audio-file';
  input.hidden = true;
  pick.append(input);
  const note = document.createElement('span');
  note.className = 'transport__label';
  note.id = 'eidophone-audio-note';
  note.setAttribute('role', 'status');
  input.addEventListener('change', async () => {
    const file = input.files?.[0];
    if (!file) return;
    note.textContent = `Decoding ${file.name}…`;
    try {
      audio.file = await decodeFile(file);
      const fileOption = [...select.options].find((o) => o.value === 'file');
      fileOption.disabled = false;
      fileOption.textContent = `Your file: ${file.name}`;
      audio.id = 'file';
      select.value = 'file';
      note.textContent = `${file.name}: ${audio.file.seconds.toFixed(1)} s, ${audio.file.channels === 1 ? 'mono, on both channels' : `${audio.file.channels} channels, the first two`}, at 48 kHz, looped. It stays in this page.`;
      restartClock();
    } catch (error) {
      note.textContent = `That file could not be decoded: ${error.message ?? error}`;
    }
  });

  row.append(
    tag,
    field('Audio', select, `What reaches the plugin in place of an audio input. The plugin’s Device menu (its default “${DEVICE_DEFAULT}”), Rescan and the live-device path are not in the browser: the page feeds earshot’s feed() as the plugin’s harness does, or writes a made-up FFT. Changing it restarts the clock (a new instance).`),
    pick,
    note,
  );
  transport.after(row);
}

//---------------------------------------------------------------------------
// The lines under the picture: the plugin's own state, through the accessors
// its harness reads. They report; they measure nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status ed-status';
  line.id = 'eidophone-status';
  line.setAttribute('aria-live', 'off');
  const slow = document.createElement('p');
  slow.className = 'stage__status ed-caution';
  slow.id = 'eidophone-slow';
  slow.setAttribute('aria-live', 'polite');
  slow.hidden = true;
  const log = document.createElement('p');
  log.className = 'stage__status ed-log';
  log.id = 'eidophone-log';
  log.setAttribute('aria-live', 'off');
  stage.append(line, slow, log);

  /** Where the track's feed is, in the page's words. */
  function trackAt(fed) {
    const at = fed % TRACK.frames;
    for (let i = 0; i < TRACK.notes.length; i += 1) {
      const n = TRACK.notes[i];
      if (at >= n.start && at < n.start + n.frames) return `note ${i + 1} of ${TRACK.notes.length}, ${noteWords(n)}, ${fixed((at - n.start) / FEED_RATE, 1)} s in`;
      const next = TRACK.notes[i + 1]?.start ?? TRACK.frames;
      if (at >= n.start + n.frames && at < next) return `the silence after note ${i + 1}`;
    }
    return 'the track';
  }

  let lastLine = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    let feedText;
    if (s.source === 'fft') {
      feedText = s.bin >= 0
        ? `nothing fed; the fallback took bin ${s.bin} of edtest --bins ${HOST_BIN}’s made-up FFT as the note, ${fixed(s.hz, 1)} Hz (no pitch)`
        : 'nothing fed; the fallback found no bin';
    } else {
      const what = s.source === 'file' ? 'your file' : `the track (${trackAt(s.fed)})`;
      const pitch = s.note ? `earshot’s pitch ${fixed(s.hz, 2)} Hz` : s.seeded ? `no pitch yet, a partial at ${fixed(s.hz, 2)} Hz` : s.hz > 0 ? `no note this frame, ${fixed(s.hz, 2)} Hz held` : 'no note yet';
      feedText = `${what} through feed() (earshot ${s.state}): ${pitch}`;
    }
    const level = s.relative > 0 ? `at ${fixed(s.relative, 2)} of its recent peak, a force of ${fixed(s.F0, 3)} N` : 'under the gate: no force, the figure frozen';
    const mode = Number.isFinite(s.nearest) ? ` · the nearest mode the drive point rings: ${fixed(s.nearest, 2)} Hz` : '';
    const rate = s.fps ? ` · ${fixed(s.fps, 0)} frames a second here` : '';
    const next = `${PLUGIN_NAME[s.variant]}: ${feedText}, ${level}${mode}`
      + ` · ${s.grains.toLocaleString('en-GB')} grains, ${s.substeps} substep${s.substeps === 1 ? '' : 's'} of 1/480 s this frame${rate}`
      + ` · the plugin’s clock at ${s.now.toFixed(2)} s`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }

    // Under 15 frames a second the plugin drops the substeps past 32.
    const behind = s.fps !== null && s.fps < 15;
    slow.hidden = !behind;
    slow.textContent = behind
      ? `This browser is drawing ${fixed(s.fps, 0)} frames a second, under 15: the plugin runs at most 32 substeps of 1/480 s a frame and drops the rest, as it would in Resolume, so the sand moves slower than real time. A lower Amount or a smaller Composition helps.`
      : '';

    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) eidophone: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against edtest reads the
// declarations, the track and the inverse conversions through these).
export { renderer, mounted, PARAMS, DECLARED, DEFAULTS, core, hostValue, kitValues, audio, restartClock, TRACK, currentVariant };
