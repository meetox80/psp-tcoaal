import base64
import json
import re
import zlib

CHUNK_RE = re.compile(r'function (_0x[0-9a-f]+_)\(\) \{ return "([A-Za-z0-9+/=]+)"')
ORDER_RE = re.compile(r"let _0x[0-9a-f]+_=((?:_0x[0-9a-f]+_\(\)\+?)+),")


def extract_hidden_script(game):
    sources = [p.read_text(encoding="utf-8", errors="replace") for p in (game.root / "js" / "plugins").glob("*.js")]
    chunks = {}
    order = None
    for text in sources:
        for m in CHUNK_RE.finditer(text):
            chunks[m.group(1)] = m.group(2)
        m = ORDER_RE.search(text)
        if m:
            order = re.findall(r"(_0x[0-9a-f]+_)\(\)", m.group(1))
    if not order:
        return None
    blob = "".join(chunks[name] for name in order)
    return zlib.decompress(base64.b64decode(blob)).decode("utf-8")


def _unescape(s):
    return json.loads('"' + s.replace('"', '\\"').replace("\\'", "'").replace("\\x", "\\u00") + '"')


def deobfuscate(code):
    m = re.match(r"const (_0x[0-9a-f]+)=\[", code)
    if not m:
        return code
    arr_name = m.group(1)
    i = m.end()
    lit = re.compile(r"'((?:[^'\\]|\\.)*)'")
    strings = []
    while True:
        mm = lit.match(code, i)
        strings.append(_unescape(mm.group(1)))
        i = mm.end()
        if code[i] == "]":
            break
        i += 1
    rot = re.search(r"\(" + re.escape(arr_name) + r",(0x[0-9a-f]+)\)\);", code)
    for _ in range(int(rot.group(1), 16)):
        strings.append(strings.pop(0))
    acc = re.search(
        r"const (_0x[0-9a-f]+)=function\(_0x[0-9a-f]+,_0x[0-9a-f]+\)\{_0x[0-9a-f]+=_0x[0-9a-f]+-(0x[0-9a-f]+);",
        code,
    )
    base = int(acc.group(2), 16)
    aliases = {acc.group(1)}
    changed = True
    pairs = re.findall(r"(_0x[0-9a-f]+)=(_0x[0-9a-f]+)[;,]", code)
    while changed:
        changed = False
        for a, b in pairs:
            if b in aliases and a not in aliases:
                aliases.add(a)
                changed = True
    call = re.compile(r"\b(" + "|".join(sorted(aliases)) + r")\((0x[0-9a-f]+)\)")
    out = call.sub(lambda mo: json.dumps(strings[int(mo.group(2), 16) - base]), code)
    src = out

    def dot(mo):
        nxt = src[mo.end() : mo.end() + 1]
        return "." + mo.group(2) + (" " if re.match(r"[A-Za-z0-9_$]", nxt) else "")

    out = re.sub(r"\[([\"'])([A-Za-z_$][A-Za-z0-9_$]*)\1\]", dot, src)
    out = re.sub(r"(?<=[A-Za-z0-9_$])!!\[\]", " true", out)
    out = re.sub(r"(?<=[A-Za-z0-9_$])!\[\]", " false", out)
    return out.replace("!![]", "true").replace("![]", "false")
