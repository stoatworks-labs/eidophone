"""The demo's copies of the plugin must be the plugin, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.
(woofer's script, which is mirrorball's, pinhole's and fountain's, with
eidophone's nine pieces, its five programs, its `uniform( shader, "Name", v )`
wrappers beside its raw `glUniform*( where( shader, "Name" ), ... )` calls,
edtest's sine and --bins buffer, and earshot's sources.)

------------------------------------------------------------------- why

The page at eidophone-demo.stoatworks-labs.com carries copies of this
repository, and copies drift quietly: sand that settles on the antinodes, or a
track a semitone off every mode, still looks like a Chladni plate.

1. **The GLSL.** `demo/shaders.js` holds the nine GLSL pieces of
   `source/Shaders.cpp` plus kVersion, AND the order the plugin joins them in:
   Shaders.cpp's Assemble() (the five fragment stages) and SplatVertex(), and
   InitGL's quad vertex (kVersion + kQuadVertex), with InitGL's table of which
   vertex stage each pass is compiled with. This script reads that order out of
   the C++ and writes it into shaders.js as `ASSEMBLY` and `PROGRAMS`. The page
   builds its expected text for every stage from that table, and at run time
   REQUIRES the text the compiled plugin hands to glShaderSource to equal one of
   them before the kit's port() is applied (demo/wasm/gl_shim.cpp). The pieces
   are compared exactly -- no whitespace normalisation, no comment stripping: a
   comment updated on one side only is drift worth catching. Every
   `const char* const k... = R"(` piece in Shaders.cpp must be one this script
   knows, and shaders.js must be byte for byte what `--write` produces, so
   nothing else can be typed into it.

   The one transformation is a decode: a backtick cannot sit raw in a template
   literal, so shaders.js would escape it as \\`. Any other backslash, or a
   `${`, on the JS side is rejected; the C++ bodies hold neither.

2. **Every uniform's setter.** The page runs the plugin's own glUniform calls,
   and WebGL2 is stricter about them than a desktop driver is allowed to be
   lenient: a float sent to an `int`, a `uint` or a sampler is
   INVALID_OPERATION and the uniform stays 0, silently, and a sampler nobody
   sets sits on unit 0. Eidophone.cpp sets most uniforms through four wrappers,
   `uniform( shader, "Name", v )`, whose overload (int, unsigned int, float,
   float-and-float) picks glUniform1i, 1ui, 1f or 2f (read out of their
   definitions here), and four raw, `glUniformNx( where( shader, "Name" ), ... )`.
   This script tells each wrapper call's overload from its argument's spelled
   type (a static_cast, a literal, or a variable whose declaration it finds) and
   refuses to guess at anything else. Each uniform a program declares must be
   set by the call its type takes, for that program's shader, and nothing set
   that it does not declare.

3. **edtest's audio.** The page feeds the plugin through earshot's feed() as
   edtest does. demo/wasm/glue.cpp carries tools/edtest/main.cpp's `sine` and
   `defaultToneHz` (the track's every sample and its last note), and the
   --bins buffer's lines (with `instance->plugin` for edtest's `rig.plugin`);
   each must be edtest's text character for character. Three more of edtest's
   lines the page's rules rest on must still be there: Rig::Init's clock preset
   (which the glue's ed_init_gl repeats), the Feeder's one-frame lead (which the
   page's feed repeats) and --tone's -12 dBFS (the track's level).

4. **No device, and no sound.** No file the page serves may name
   getUserMedia, enumerateDevices or mediaDevices: the page never asks for a
   microphone or lists an audio device, and this keeps it so. Nor an
   AudioContext other than the OfflineAudioContext that decodes a visitor's
   file to memory: nothing is played aloud.

5. **The WebAssembly.** `demo/eidophone-core.wasm` is the plugin's C++ and
   earshot's compiled by `demo/tools/build-wasm.sh`, and committed, because the
   deploy has no build step. Building it needs emscripten, which verify.sh
   cannot assume, so this checks the next best thing: `demo/wasm/inputs.sha256`,
   written by the build, records the hash of every file that went in (every
   source/*.h, the nine plugin .cpp files, earshot's sources and headers, the
   SDK files, the glue, the shim and the build script), of both outputs, and the
   SDK commit. A source file changed since the last build -- a regenerated
   StoatworksAbout.h or a new earshot snapshot included -- or an output that is
   not the one the build wrote fails here, and the fix is to rerun
   build-wasm.sh.

------------------------------------------------------------------- what it cannot

It cannot tell whether the .wasm really is what those inputs compile to (only
rebuilding can), and it says nothing about demo/plugin.js beyond rule 4: the
panel is built from the plugin's own declarations at run time, but its read-out
units, its tooltips, its presets and its layout are the page's.
"""
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion is a plain string and is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("FIELD", "kField"),
    ("FIELD_FRAGMENT", "kFieldFragment"),
    ("POUR_FRAGMENT", "kPourFragment"),
    ("UPDATE_FRAGMENT", "kUpdateFragment"),
    ("SPLAT_VERTEX", "kSplatVertex"),
    ("SPLAT_FRAGMENT", "kSplatFragment"),
    ("FINAL_FRAGMENT", "kFinalFragment"),
]
JS_NAME = dict((symbol, name) for name, symbol in SHADERS)
JS_NAME["kVersion"] = "VERSION"

# The setters each GLSL uniform type takes, scalar and array.
SETTER = {
    "sampler2D": {"glUniform1i"},
    "sampler2DArray": {"glUniform1i"},
    "int": {"glUniform1i"},
    "bool": {"glUniform1i"},
    "uint": {"glUniform1ui"},
    "ivec4": {"glUniform4i"},
    "float": {"glUniform1f"},
    "vec2": {"glUniform2f"},
    "vec3": {"glUniform3f"},
    "vec4": {"glUniform4f", "glUniform4fv"},
}
ARRAY_SETTER = {"vec2": {"glUniform2fv"}, "vec4": {"glUniform4fv"}, "float": {"glUniform1fv"}}

# edtest's functions, copied into the glue.
HARNESS = ["sine", "defaultToneHz"]
# Lines in both, the glue's `instance->plugin.` read as edtest's `rig.plugin.`.
HARNESS_LINES = [
    "const int host = rig.plugin.HostIndexOf( PT_AUDIO );",
    "for( int j = 0; j < 64; ++j )",
    "rig.plugin.SetParamElementValue( static_cast< unsigned int >( host ), static_cast< unsigned int >( j ),",
    "j == hostBin ? 0.5f : ( j == 0 ? 0.9f : 0.02f ) );",
]
# edtest's lines the page's rules rest on, and where the page repeats each.
EDTEST_RULES = [
    ("plugin.SetClockScaleForTest( 1.0 );", "instance->plugin.SetClockScaleForTest( 1.0 );",
     "Rig::Init's clock preset"),
    ("size_t target = static_cast< size_t >( std::llround( ( frame + 1 ) / fps * wav->rate ) );", None,
     "the Feeder's one-frame lead"),
    ("out.samples  = sine( toneHz, -12.0, out.rate, std::max( seconds, 1.0 ) );", "constexpr double kToneDb = -12.0;  ///< edtest --tone's level",
     "--tone's -12 dBFS"),
]

# What a page that never touches a device must never name.
DEVICE_APIS = ("getUserMedia", "enumerateDevices", "mediaDevices")
# And what plays sound: any AudioContext but the offline one that decodes to memory.
SOUND = re.compile(r"(?<!Offline)AudioContext\b")

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \\` for a backtick inside a comment.
"""

MANIFEST = os.path.join("demo", "wasm", "inputs.sha256")
OUTPUTS = ("demo/eidophone-core.js", "demo/eidophone-core.wasm")
REGISTRATION = ("SourcePlugin.cpp", "EffectPlugin.cpp")
# earshot's device backend, which build-wasm.sh leaves out (EARSHOT_CAPTURE=OFF).
NOT_COMPILED = ("Miniaudio.cpp", "MiniaudioImpl.cpp")


class Stale(Exception):
    """The C++ is not shaped the way this script reads it: fail, never guess."""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def unknown_pieces(cpp):
    """Raw-string pieces in Shaders.cpp that this script does not copy."""
    declared = re.findall(r'const char\* const (k\w+) = R"\(', cpp)
    return [symbol for symbol in declared if symbol not in JS_NAME]


def pieces_of(expression, where, text=None):
    """`std::string( kVersion ) + kA + kB` -> [kVersion, kA, kB]; `text` stands
    for the pieces Assemble()'s local of that name holds."""
    pieces = []
    for term in (t.strip() for t in expression.split("+")):
        if text is not None and term == "text":
            pieces += text
            continue
        m = re.fullmatch(r"(?:std::string\(\s*)?(?:shaders::)?(k\w+)(?:\s*\))?", term)
        if m is None:
            raise Stale(f"{where} joins `{term}`, which is not a piece")
        if m.group(1) not in JS_NAME:
            raise Stale(f"{where} uses {m.group(1)}, a piece this script does not copy")
        pieces.append(m.group(1))
    return pieces


def function_body(source, signature, where):
    body = re.search(re.escape(signature) + r"\s*\{(.*?)\n\}", source, re.S)
    if body is None:
        raise Stale(f"no `{signature}` in {where}")
    return body.group(1)


def assembly(cpp, header, plugin_cpp):
    """(stages, programs): stages [(stage name, [JS pieces])] from Shaders.cpp's
    Assemble() and SplatVertex() and InitGL's quad vertex; programs
    [(pass name, shader member, vertex stage, fragment stage)] from InitGL's
    table of stages, in its order."""
    # Assemble( Pass ): one `text`, then one case per pass, each a return.
    body = function_body(cpp, "std::string Assemble( Pass pass )", "source/Shaders.cpp")
    lines = [line.strip() for line in body.strip().splitlines() if line.strip()]
    head = re.fullmatch(r"const std::string text = (.*);", lines[0]) if lines else None
    if head is None or lines[1:3] != ["switch( pass )", "{"] or lines[-2:] != ["}", "return text;"]:
        raise Stale("Assemble() is no longer `text = ...; switch( pass ) { case ...: return ...; } return text;`")
    text = pieces_of(head.group(1), "Assemble()'s text")
    cases = {}
    for line in lines[3:-2]:
        m = re.fullmatch(r"case Pass::(\w+): return (.*);", line)
        if m is None:
            raise Stale(f"Assemble() has `{line}`, which is not `case Pass::X: return <pieces>;`")
        cases[m.group(1)] = pieces_of(m.group(2), f"Assemble()'s case {m.group(1)}", text)
    enum = re.search(r"enum class Pass\s*\{(.*?)\};", header, re.S)
    passes = [] if enum is None else [p.strip() for p in enum.group(1).split(",") if p.strip()]
    if not passes or sorted(passes) != sorted(cases):
        raise Stale(f"Assemble() covers {sorted(cases)}, and Shaders.h's Pass is {passes}")

    splat = function_body(cpp, "std::string SplatVertex()", "source/Shaders.cpp").strip()
    m = re.fullmatch(r"return (.*);", splat)
    if m is None:
        raise Stale("SplatVertex() is not one `return <pieces>;`")
    splat_vertex = pieces_of(m.group(1), "SplatVertex()")

    # InitGL: the two vertex stages, the table, and the one Compile.
    quad = re.search(r"const std::string quadVertex\s*= (.*?);", plugin_cpp)
    if quad is None:
        raise Stale("Eidophone.cpp's InitGL no longer builds `quadVertex`")
    quad_vertex = pieces_of(quad.group(1), "InitGL's quadVertex")
    for needed in ("const std::string splatVertex = shaders::SplatVertex();",
                   "const std::string fragment = shaders::Assemble( stage.pass );",
                   "stage.shader->Compile( stage.vertex->c_str(), fragment.c_str() )"):
        if needed not in plugin_cpp:
            raise Stale(f"Eidophone.cpp's InitGL no longer has `{needed}`")
    if len(re.findall(r"(?:\.|->)Compile\(", plugin_cpp)) != 1:
        raise Stale("Eidophone.cpp compiles in more than one place; this script reads InitGL's table")
    table = re.findall(r'\{ &(\w+), &(\w+), shaders::Pass::(\w+), "(\w+)" \}', plugin_cpp)
    if sorted(p for _, _, p, _ in table) != sorted(passes):
        raise Stale(f"InitGL's table compiles {[p for _, _, p, _ in table]}, and Shaders.h's Pass is {passes}")
    vertex_stage = {"quadVertex": "quad_vertex", "splatVertex": "splat_vertex"}

    stages = [("quad_vertex", [JS_NAME[s] for s in quad_vertex]),
              ("splat_vertex", [JS_NAME[s] for s in splat_vertex])]
    programs = []
    for member, vertex, pass_, name in table:
        if vertex not in vertex_stage:
            raise Stale(f"InitGL compiles the {name} pass with `{vertex}`, a vertex stage this script does not read")
        stage = f"{pass_.lower()}_fragment"
        stages.append((stage, [JS_NAME[s] for s in cases[pass_]]))
        programs.append((name, member, vertex_stage[vertex], stage))
    return stages, programs


def render(cpp, header, plugin_cpp):
    """shaders.js as --write produces it."""
    version = cpp_version(cpp)
    if version is None:
        raise Stale("kVersion not found in source/Shaders.cpp")
    out = [HEADER]
    out.append(f"export const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body = from_cpp(cpp, symbol)
        if body is None:
            raise Stale(f"{symbol} not found in source/Shaders.cpp")
        if "\\" in body or "${" in body:
            raise Stale(f"{symbol} holds a backslash or ${{; the escape scheme cannot carry it")
        out.append(f"\n// {symbol}, source/Shaders.cpp\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    stages, programs = assembly(cpp, header, plugin_cpp)
    out.append(
        "\n// Each stage's source, piece by piece, in the order the plugin joins them:\n"
        "// Shaders.cpp's Assemble() and SplatVertex(), and InitGL's quad vertex.\n"
        "export const ASSEMBLY = {\n"
    )
    for name, pieces in stages:
        out.append(f"  {name}: [{', '.join(repr(p) for p in pieces)}],\n")
    out.append("};\n")
    out.append(
        "\n// The five programs InitGL compiles, in its order: [vertex stage, fragment stage].\n"
        "export const PROGRAMS = {\n"
    )
    for name, _, vertex, fragment in programs:
        out.append(f"  {name}: [{vertex!r}, {fragment!r}],\n")
    out.append("};\n")
    return "".join(out)


def js_version(js):
    match = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    return None if match is None else match.group(1)


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, header, plugin_cpp, js):
    problems = 0

    for symbol in unknown_pieces(cpp):
        print(f"FAIL  {symbol} is a piece of source/Shaders.cpp this script does not copy -- add it to SHADERS")
        problems += 1

    version_cpp = cpp_version(cpp)
    if version_cpp is None or js_version(js) != version_cpp:
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<24} matches kVersion")

    for name, symbol in SHADERS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<24} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    try:
        stages, programs = assembly(cpp, header, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return problems + 1
    block = re.search(r"^export const ASSEMBLY = \{\n(.*?)^\};$", js, re.S | re.M)
    rows = {} if block is None else dict(re.findall(r"^  (\w+): \[(.*)\],$", block.group(1), re.M))
    for name, pieces in stages:
        want = ", ".join(repr(p) for p in pieces)
        if rows.get(name) == want:
            print(f"ok    {'ASSEMBLY.' + name:<24} {' + '.join(pieces)}")
        else:
            print(f"FAIL  ASSEMBLY.{name} is [{rows.get(name, '<missing>')}], the plugin's is [{want}]")
            problems += 1
    for name in sorted(set(rows) - set(n for n, _ in stages)):
        print(f"FAIL  ASSEMBLY.{name} is no stage of the plugin's")
        problems += 1
    print(f"ok    {'PROGRAMS':<24} " + ", ".join(f"{n} = {v} + {f}" for n, _, v, f in programs))

    try:
        expected = render(cpp, header, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if expected != js:
        where = first_difference(expected, js)
        print("FAIL  demo/shaders.js is not exactly what --write produces")
        if where:
            print(f"        first difference at line {where[0]}")
            print(f"          want: {where[1]}")
            print(f"          have: {where[2]}")
        problems += 1
    return problems


#---------------------------------------------------------------------------
# The uniforms.
#---------------------------------------------------------------------------

def declared_uniforms(text):
    """{name: (type, is_array)} of every `uniform <type> <Name>[ n ];` in a source."""
    out = {}
    for kind, name, array in re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*;", text, re.M):
        out[name] = (kind, bool(array))
    return out


def split_arguments(text):
    """Top-level comma split."""
    out, depth, current = [], 0, ""
    for ch in text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(current.strip())
            current = ""
        else:
            current += ch
    out.append(current.strip())
    return out


def balanced_call(expression, prefix):
    """True when `expression` is exactly `prefix( ... )` with its parentheses closing at the end."""
    if not expression.startswith(prefix):
        return False
    depth = 0
    for i, ch in enumerate(expression[len(prefix) - 1:], start=len(prefix) - 1):
        depth += ch == "("
        depth -= ch == ")"
        if depth == 0:
            return i == len(expression) - 1
    return False


def declared_type(name, function, header):
    """The C++ type of a variable, from its declaration in the calling
    function's text or, failing that, the class's header."""
    for text in (function, header):
        m = re.search(r"\b(unsigned int|int|float|double|uint32_t)\s+" + re.escape(name) + r"\b\s*[=;,)]", text)
        if m:
            return m.group(1)
    return None


def argument_type(expression, function, header):
    expression = expression.strip()
    m = re.match(r"static_cast< (unsigned int|int|float) >\(", expression)
    if m and balanced_call(expression, f"static_cast< {m.group(1)} >("):
        return m.group(1)
    if re.fullmatch(r"\d+", expression):
        return "int"
    if re.fullmatch(r"\d+\.\d*f", expression):
        return "float"
    m = re.fullmatch(r"\w+ \? (.+) : (.+)", expression)
    if m:
        kinds = set()
        for branch in m.groups():
            try:
                kinds.add(argument_type(branch, function, header))
            except Stale:
                pass
        if len(kinds) == 1:
            return kinds.pop()
        raise Stale(f"cannot tell the type of `{expression}`")
    if re.fullmatch(r"\w+", expression):
        kind = declared_type(expression, function, header)
        if kind in ("int", "unsigned int", "float"):
            return kind
    raise Stale(f"cannot tell the type of `{expression}`: spell it with a static_cast, or teach this script")


def enclosing_function(lines, index):
    start = index
    while start > 0 and not re.match(r"^\S.*EidophonePlugin::\w+\(", lines[start]):
        start -= 1
    end = index
    while end < len(lines) and lines[end] != "}":
        end += 1
    return "\n".join(lines[start:end + 1])


def wrappers(plugin_cpp):
    """{(argument types): GL setter} from the uniform() overloads' definitions."""
    out = {}
    for args, setter in re.findall(
            r"void EidophonePlugin::uniform\( const FFGLShader& shader, const char\* name, ([^)]*) \)\n\{\n\t(glUniform\w+)\( where\( shader, name \), [^)]*\);\n\}",
            plugin_cpp):
        types = tuple(re.sub(r"\s+\w+$", "", a.strip()) for a in args.split(","))
        out[types] = setter
    return out


def check_uniforms(cpp, header, plugin_cpp, plugin_h):
    try:
        stages, programs = assembly(cpp, header, plugin_cpp)
    except Stale:
        return 0  # already reported
    pieces = {name: from_cpp(cpp, symbol) or "" for name, symbol in SHADERS}
    pieces["VERSION"] = cpp_version(cpp) or ""
    stage_text = {name: "".join(pieces[p] for p in stage) for name, stage in stages}
    by_member = {member: (name, declared_uniforms(stage_text[v] + stage_text[f])) for name, member, v, f in programs}

    overloads = wrappers(plugin_cpp)
    if len(overloads) != 4:
        print(f"FAIL  Eidophone.cpp's uniform() overloads read as {overloads}; this script expects four")
        return 1

    problems = 0
    calls = []  # (member, uniform, setter, array count or None)
    lines = plugin_cpp.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r'^\s*uniform\(\s*(\w+),\s*"(\w+)",\s*(.*)\);\s*$', line)
        if m:
            function = enclosing_function(lines, i)
            try:
                types = tuple(argument_type(a, function, plugin_h) for a in split_arguments(m.group(3)))
            except Stale as stale:
                print(f"FAIL  Eidophone.cpp:{i + 1}: {stale}")
                problems += 1
                continue
            if types not in overloads:
                print(f"FAIL  Eidophone.cpp:{i + 1}: no uniform() overload takes {types}")
                problems += 1
                continue
            calls.append((m.group(1), m.group(2), overloads[types], None))
        for setter, member, name, rest in re.findall(r'(glUniform\w+)\(\s*where\(\s*(\w+),\s*"(\w+)"\s*\),\s*([^,)]*)', line):
            calls.append((member, name, setter, rest.strip() if setter.endswith("v") else None))
    wrapped = len(re.findall(r'\buniform\(\s*\w+,\s*"', plugin_cpp))
    raw = len(re.findall(r'\bwhere\(\s*\w+,\s*"', plugin_cpp))
    if wrapped + raw != len(calls) + problems:
        print(f"FAIL  Eidophone.cpp names {wrapped + raw} uniforms and this script read {len(calls)} of them as calls")
        return problems + 1

    for member, (program, declared) in by_member.items():
        named = [(n, s, c) for m, n, s, c in calls if m == member]
        for name in sorted(set(declared) - {n for n, _, _ in named}):
            print(f"FAIL  the {program} program's uniform {declared[name][0]} {name} is never set (a sampler would sit on unit 0)")
            problems += 1
        for name, setter, count in named:
            if name not in declared:
                print(f"FAIL  {setter} sets {name} on {member}, and the {program} program does not declare it")
                problems += 1
                continue
            kind, array = declared[name]
            allowed = ARRAY_SETTER.get(kind, set()) if array else SETTER.get(kind, set())
            if setter not in allowed:
                print(f"FAIL  {program}'s {name} is {kind}{'[]' if array else ''} and is set with {setter}, not {' or '.join(sorted(allowed)) or '?'}")
                problems += 1
            if array and count != "shaders::kMaxModes":
                print(f"FAIL  {program}'s {name} is an array set with a count of `{count}`, not shaders::kMaxModes")
                problems += 1
        set_names = [n for n, _, _ in named]
        for name in sorted({n for n in set_names if set_names.count(n) > 1}):
            print(f"FAIL  {name} is set on {member} more than once a frame")
            problems += 1
    for member in sorted({m for m, _, _, _ in calls} - set(by_member)):
        print(f"FAIL  a uniform is set on {member}, which InitGL's table does not compile")
        problems += 1
    if problems == 0:
        count = sum(len(d) for _, d in by_member.values())
        print(f"ok    {'UNIFORMS':<24} all {count} across the {len(by_member)} programs set, each by its type's setter ({len(calls)} calls)")
    return problems


#---------------------------------------------------------------------------
# edtest's audio, and no device.
#---------------------------------------------------------------------------

def function_text(source, name):
    """A top-level C++ function: from the line that declares `name(` at the
    start of a line (its return type first) to the first `}` at the start of
    a line after it."""
    match = re.search(r"^[A-Za-z_][\w:<>&* ]*\b" + name + r"\(.*?^\}$", source, re.S | re.M)
    return None if match is None else match.group(0)


def check_harness(main_cpp, glue):
    problems = 0
    for name in HARNESS:
        theirs = function_text(main_cpp, name)
        ours = function_text(glue, name)
        if theirs is None:
            print(f"FAIL  {name} not found in tools/edtest/main.cpp")
            problems += 1
        elif ours is None:
            print(f"FAIL  {name} not found in demo/wasm/glue.cpp")
            problems += 1
        elif ours == theirs:
            print(f"ok    {name:<24} matches tools/edtest/main.cpp's ({len(ours)} chars)")
        else:
            print(f"FAIL  {name} in demo/wasm/glue.cpp has drifted from tools/edtest/main.cpp's")
            where = first_difference(theirs, ours)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          edtest: {where[1]}")
                print(f"          glue  : {where[2]}")
            problems += 1
    edtest_lines = {line.strip() for line in main_cpp.splitlines()}
    raw_glue_lines = {line.strip() for line in glue.splitlines()}
    # The glue's instance is edtest's rig: the one renaming the check allows.
    glue_lines = {line.replace("instance->plugin.", "rig.plugin.") for line in raw_glue_lines}
    for line in HARNESS_LINES:
        missing = [where for where, lines in (("tools/edtest/", edtest_lines), ("demo/wasm/glue.cpp", glue_lines)) if line not in lines]
        if missing:
            print(f"FAIL  `{line}` is not in {' or '.join(missing)}")
            problems += 1
    for theirs, ours, what in EDTEST_RULES:
        if theirs not in edtest_lines:
            print(f"FAIL  edtest no longer has {what} (`{theirs}`): the page repeats it, so look again")
            problems += 1
        elif ours is not None and ours not in raw_glue_lines:
            print(f"FAIL  demo/wasm/glue.cpp no longer repeats {what} (`{ours}`)")
            problems += 1
    if problems == 0:
        print(f"ok    {'HARNESS LINES':<24} all {len(HARNESS_LINES)} --bins lines in both, and edtest's clock preset, feed lead and tone level")
    return problems


def check_no_devices():
    problems = 0
    demo = os.path.join(REPO, "demo")
    served = []
    for root, dirs, files in os.walk(demo):
        rel = os.path.relpath(root, demo)
        if rel.split(os.sep)[0] in ("tools", "wasm"):
            continue  # not served (.assetsignore), and this file names them
        for name in files:
            if name.endswith((".js", ".html")):
                served.append(os.path.join(root, name))
    for path in sorted(served):
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for api in DEVICE_APIS:
            if api in text:
                print(f"FAIL  {os.path.relpath(path, REPO)} names {api}: the page must never ask for or list an audio device")
                problems += 1
        if SOUND.search(text):
            print(f"FAIL  {os.path.relpath(path, REPO)} names an AudioContext: the page plays nothing aloud")
            problems += 1
    if problems == 0:
        print(f"ok    {'NO DEVICES':<24} none of the {len(served)} served scripts and pages names {', '.join(DEVICE_APIS)} or an AudioContext but the offline one")
    return problems


#---------------------------------------------------------------------------
# The WebAssembly.
#---------------------------------------------------------------------------

def sha256(path):
    digest = hashlib.sha256()
    with open(os.path.join(REPO, path), "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def check_wasm():
    try:
        lines = read(MANIFEST).splitlines()
    except FileNotFoundError:
        print(f"FAIL  {MANIFEST} is missing -- run demo/tools/build-wasm.sh")
        return 1

    problems = 0
    entries = 0
    recorded = set()
    for line in lines:
        pin = re.match(r"^# FFGL SDK ([0-9a-f]{40})$", line)
        if pin:
            try:
                head = subprocess.run(["git", "-C", os.path.join(REPO, "external", "ffgl"), "rev-parse", "HEAD"],
                                      capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                print("FAIL  cannot read the FFGL SDK's commit -- is the submodule checked out?")
                problems += 1
                continue
            if head != pin.group(1):
                print(f"FAIL  the .wasm was built against FFGL SDK {pin.group(1)[:7]}, the submodule is at {head[:7]}")
                problems += 1
            continue
        if not line or line.startswith("#"):
            continue
        want, path = line.split(None, 1)
        recorded.add(path)
        entries += 1
        if not os.path.exists(os.path.join(REPO, path)):
            print(f"FAIL  {path} went into the .wasm and no longer exists")
            problems += 1
        elif sha256(path) != want:
            print(f"FAIL  {path} has changed since demo/eidophone-core.wasm was built -- rerun demo/tools/build-wasm.sh")
            problems += 1

    # A source file added since the build is an input the build never saw.
    trees = [("source", REGISTRATION), (os.path.join("external", "earshot", "src"), NOT_COMPILED),
             (os.path.join("external", "earshot", "include", "earshot"), ())]
    for tree, skipped in trees:
        for name in sorted(os.listdir(os.path.join(REPO, tree))):
            if name in skipped:
                continue
            rel = f"{tree}/{name}".replace(os.sep, "/")
            if (name.endswith(".h") or name.endswith(".cpp")) and rel not in recorded:
                print(f"FAIL  {rel} is newer than the .wasm's manifest -- rebuild")
                problems += 1

    for output in OUTPUTS:
        if output not in recorded:
            print(f"FAIL  {MANIFEST} does not record {output}")
            problems += 1
    if problems == 0:
        print(f"ok    {'WASM':<24} built from these sources ({entries} files hashed, SDK pin agrees)")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    header = read("source", "Shaders.h")
    plugin_cpp = read("source", "Eidophone.cpp")
    plugin_h = read("source", "Eidophone.h")
    if "--write" in argv:
        unknown = unknown_pieces(cpp)
        if unknown:
            print(f"FAIL  {', '.join(unknown)}: piece(s) this script does not copy -- add them to SHADERS")
            return 1
        try:
            text = render(cpp, header, plugin_cpp)
        except Stale as stale:
            print(f"FAIL  {stale}")
            return 1
        with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
            handle.write(text)
        print("wrote demo/shaders.js")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check_shaders(cpp, header, plugin_cpp, js)
    problems += check_uniforms(cpp, header, plugin_cpp, plugin_h)
    edtest = "".join(read("tools", "edtest", name) for name in ("main.cpp", "checks.inc"))
    problems += check_harness(edtest, read("demo", "wasm", "glue.cpp"))
    problems += check_no_devices()
    problems += check_wasm()
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for the GLSL and"
              " demo/tools/build-wasm.sh for the .wasm; never edit either output by hand")
        return 1
    print(f"all {len(SHADERS) + 1} shader pieces and the order every stage joins them in are the plugin's,"
          f" every uniform is set by its type's setter, the glue's {len(HARNESS)} edtest functions are edtest's,"
          " no served file names a device API or plays a sound, and the .wasm is built from the current sources")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
