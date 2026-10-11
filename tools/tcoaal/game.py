import json
import pathlib
import re

from . import crypto


class GameError(Exception):
    pass


class Game:
    def __init__(self, path):
        root = self._find_root(pathlib.Path(path).expanduser().resolve())
        if root is None:
            raise GameError(f"could not find the game in {path} (expected a folder containing www/js/main.js)")
        self.root = root
        self.version = self._read_version()
        self._redirs = {}

    @staticmethod
    def _is_root(p):
        return (p / "js" / "main.js").is_file() and (p / "data").is_dir() and (p / "img").is_dir()

    @classmethod
    def _find_root(cls, start):
        if not start.is_dir():
            return None
        for cand in (start, start / "www"):
            if cls._is_root(cand):
                return cand
        queue = [(start, 0)]
        while queue:
            d, depth = queue.pop(0)
            if depth > 5:
                continue
            try:
                children = sorted(c for c in d.iterdir() if c.is_dir())
            except OSError:
                continue
            for c in children:
                if cls._is_root(c):
                    return c
                queue.append((c, depth + 1))
        return None

    def _read_version(self):
        text = (self.root / "js" / "main.js").read_text(encoding="utf-8", errors="replace")
        m = re.search(r'GAME_VERSION\s*=\s*"([^"]+)"', text)
        return m.group(1) if m else "unknown"

    def resolve(self, path):
        if path in self._redirs:
            return self._redirs[path]
        found = None
        plain = self.root / path
        if plain.is_file():
            found = path
        else:
            dot = path.rfind(".")
            stem = path[:dot] if dot > path.rfind("/") else path
            if (self.root / stem).is_file():
                found = stem
            else:
                hashed = crypto.hash_path(path)
                if (self.root / hashed).is_file():
                    found = hashed
        self._redirs[path] = found
        return found

    def exists(self, path):
        return self.resolve(path) is not None

    def read_raw(self, stored):
        return (self.root / stored).read_bytes()

    def read_stored(self, stored):
        return crypto.decrypt(self.read_raw(stored), stored)

    def read(self, path):
        stored = self.resolve(path)
        if stored is None:
            raise GameError(f"missing resource: {path}")
        return self.read_stored(stored)

    def json(self, path):
        return json.loads(self.read(path).decode("utf-8"))

    def stored_files(self, folder):
        base = self.root / folder
        if not base.is_dir():
            return []
        return sorted(
            str(p.relative_to(self.root)).replace("\\", "/")
            for p in base.rglob("*")
            if p.is_file()
        )

    def plugins(self):
        text = (self.root / "js" / "plugins.js").read_text(encoding="utf-8", errors="replace")
        start = text.index("[")
        end = text.rindex("]")
        body = re.sub(r",\s*\]$", "]", text[start : end + 1])
        return json.loads(body)

    def language(self, path="data/9c7050ae76645487"):
        raw = self.read_stored(path)
        if not raw.startswith(crypto.LANG_SIG):
            raise GameError("language file signature missing")
        return json.loads(raw[len(crypto.LANG_SIG) :].decode("utf-8"))
