import hashlib

ASSET_SIG = b"TCOAAL"
LANG_SIG = b"LANGDATA"


def hash_string(text, length=16):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:length]


def hash_path(path, length=16):
    parts = path.replace("\\", "/").split("/")
    last = parts[-1]
    hashed = hash_string("/".join(parts), length)
    if "[BUST]" in last.upper():
        hashed += "[BUST]"
    if last.startswith("!"):
        hashed = "!" + hashed
    parts[-1] = hashed
    return "/".join(parts)


def _js_int32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def name_mask(url):
    name = url.replace("\\", "/").rsplit("/", 1)[-1]
    dot = name.rfind(".")
    if dot > 0:
        name = name[:dot]
    mask = 0
    for ch in name.upper():
        mask = _js_int32((mask << 1) ^ ord(ch))
    return mask


def is_encrypted(buf):
    return buf[: len(ASSET_SIG)] == ASSET_SIG


def decrypt(buf, url):
    if not is_encrypted(buf):
        return bytes(buf)
    sig = len(ASSET_SIG)
    count = buf[sig]
    body = buf[sig + 1 :]
    if count == 0:
        count = len(body)
    key = (name_mask(url) + 1) % 256
    if key < 0:
        key += 256
    out = bytearray(body)
    for i in range(min(count, len(body))):
        b = body[i]
        out[i] = b ^ key
        key = ((key << 1) ^ b) & 255
    return bytes(out)
