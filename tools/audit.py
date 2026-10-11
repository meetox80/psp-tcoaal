import argparse
import collections
import json
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from tcoaal import crypto, jsunpack, mvcodes
from tcoaal.game import Game

DB_FILES = [
    "Actors", "Classes", "Skills", "Items", "Weapons", "Armors", "Enemies", "Troops",
    "States", "Animations", "Tilesets", "CommonEvents", "System", "MapInfos",
]


def iter_lists(game):
    infos = game.json("data/MapInfos.json")
    maps = {}
    for info in infos:
        if not info:
            continue
        path = "data/Map%03d.json" % info["id"]
        if game.exists(path):
            maps[info["id"]] = game.json(path)
    for mid, m in maps.items():
        for ev in m["events"]:
            if not ev:
                continue
            for pi, page in enumerate(ev["pages"]):
                yield f"map{mid}/ev{ev['id']}/p{pi}", page["list"], page
    for ce in game.json("data/CommonEvents.json"):
        if ce:
            yield f"ce{ce['id']}", ce["list"], None
    for tr in game.json("data/Troops.json"):
        if tr:
            for pi, page in enumerate(tr["pages"]):
                yield f"troop{tr['id']}/p{pi}", page["list"], None
    return maps


def png_size(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return w, h


def ogg_info(data):
    if data[:4] != b"OggS":
        return None
    i = data.find(b"\x01vorbis")
    if i < 0:
        return None
    channels = data[i + 11]
    rate = struct.unpack("<I", data[i + 12 : i + 16])[0]
    last = data.rfind(b"OggS")
    granule = struct.unpack("<q", data[last + 6 : last + 14])[0]
    return {"channels": channels, "rate": rate, "seconds": round(granule / rate, 2) if rate else 0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game")
    ap.add_argument("--out", default="_work/audit")
    args = ap.parse_args()
    game = Game(args.game)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    report = {"version": game.version}
    cmd_count = collections.Counter()
    cmd_lists = collections.defaultdict(set)
    route_count = collections.Counter()
    plugin_cmds = collections.Counter()
    plugin_samples = collections.defaultdict(list)
    scripts = collections.Counter()
    branch_scripts = collections.Counter()
    var_scripts = collections.Counter()
    route_scripts = collections.Counter()
    comments = collections.Counter()
    text_codes = collections.Counter()
    text_tags = collections.Counter()
    branch_types = collections.Counter()
    var_operands = collections.Counter()
    page_cond = collections.Counter()
    triggers = collections.Counter()
    refs = collections.defaultdict(set)
    sample_text = []
    list_count = 0
    total_cmds = 0

    def add_route(route):
        for step in route.get("list", []):
            route_count[step["code"]] += 1
            p = step.get("parameters") or []
            if step["code"] == 45 and p:
                route_scripts[p[0]] += 1
            if step["code"] == 41 and p:
                refs["img/characters"].add(p[0])
            if step["code"] == 44 and p:
                refs["audio/se"].add(p[0]["name"])

    for where, lst, page in iter_lists(game):
        list_count += 1
        if page is not None:
            triggers[page.get("trigger")] += 1
            c = page["conditions"]
            for k in ("switch1Valid", "switch2Valid", "variableValid", "selfSwitchValid", "itemValid", "actorValid"):
                if c.get(k):
                    page_cond[k] += 1
            img = page.get("image") or {}
            if img.get("characterName"):
                refs["img/characters"].add(img["characterName"])
            if page.get("moveType") == 3:
                add_route(page.get("moveRoute") or {})
        i = 0
        while i < len(lst):
            cmd = lst[i]
            code = cmd["code"]
            p = cmd.get("parameters") or []
            cmd_count[code] += 1
            cmd_lists[code].add(where.split("/")[0])
            total_cmds += 1
            if code == 355:
                body = [p[0]]
                j = i + 1
                while j < len(lst) and lst[j]["code"] == 655:
                    body.append(lst[j]["parameters"][0])
                    j += 1
                scripts["\n".join(body)] += 1
            elif code == 356:
                name = p[0].split(" ")[0] if p else ""
                plugin_cmds[name] += 1
                if len(plugin_samples[name]) < 6 and p[0] not in plugin_samples[name]:
                    plugin_samples[name].append(p[0])
            elif code == 111:
                branch_types[p[0]] += 1
                if p[0] == 12:
                    branch_scripts[p[1]] += 1
            elif code == 122:
                var_operands[p[3]] += 1
                if p[3] == 4:
                    var_scripts[p[4]] += 1
            elif code in (108, 408):
                tag = re.findall(r"<([^:>\s]+)", p[0])
                for t in tag:
                    comments[t] += 1
                if not tag:
                    comments["(plain) " + p[0][:40]] += 1
            elif code == 401:
                for m in re.findall(r"\\([A-Za-z]+)", p[0]):
                    text_codes[m] += 1
                for m in re.findall(r"<([A-Za-z]+)", p[0]):
                    text_tags[m] += 1
                if len(sample_text) < 30:
                    sample_text.append(p[0])
            elif code == 101:
                if p and p[0]:
                    refs["img/faces"].add(p[0])
            elif code == 205:
                add_route(p[1])
            elif code == 231 and p:
                refs["img/pictures"].add(p[1])
            elif code == 284 and p:
                refs["img/parallaxes"].add(p[0])
            elif code == 322 and p:
                refs["img/characters"].add(p[1])
                refs["img/faces"].add(p[3])
            elif code in (241, 245, 249, 250) and p and p[0]:
                folder = {241: "bgm", 245: "bgs", 249: "me", 250: "se"}[code]
                refs["audio/" + folder].add(p[0]["name"])
            i += 1

    report["event_lists"] = list_count
    report["total_commands"] = total_cmds
    report["commands"] = [
        {"code": c, "name": mvcodes.COMMANDS.get(c, "?"), "count": n, "lists": len(cmd_lists[c])}
        for c, n in cmd_count.most_common()
    ]
    report["route_steps"] = [
        {"code": c, "name": mvcodes.ROUTE.get(c, "?"), "count": n} for c, n in route_count.most_common()
    ]
    report["plugin_commands"] = [
        {"name": k, "count": n, "samples": plugin_samples[k]} for k, n in plugin_cmds.most_common()
    ]
    report["branch_types"] = dict(branch_types.most_common())
    report["variable_operands"] = dict(var_operands.most_common())
    report["page_conditions"] = dict(page_cond)
    report["triggers"] = dict(triggers)
    report["comment_tags"] = dict(comments.most_common(60))
    report["text_escape_codes"] = dict(text_codes.most_common())
    report["text_tags"] = dict(text_tags.most_common())
    report["sample_text"] = sample_text

    def calls(counter):
        c = collections.Counter()
        for src, n in counter.items():
            for m in re.findall(r"([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\s*\(", src):
                c[m] += n
            for m in re.findall(r"(\$game[A-Za-z]+(?:\.[A-Za-z_]\w*)?)", src):
                c["@" + m] += n
        return c

    all_scripts = collections.Counter()
    for counter in (scripts, branch_scripts, var_scripts, route_scripts):
        all_scripts.update(counter)
    report["scripts"] = {
        "event_scripts": len(scripts),
        "event_script_uses": sum(scripts.values()),
        "branch_scripts": len(branch_scripts),
        "variable_scripts": len(var_scripts),
        "route_scripts": len(route_scripts),
        "identifiers": dict(calls(all_scripts).most_common()),
    }
    (out / "scripts.json").write_text(
        json.dumps(
            {
                "event": scripts.most_common(),
                "branch": branch_scripts.most_common(),
                "variable": var_scripts.most_common(),
                "route": route_scripts.most_common(),
            },
            indent=1,
            ensure_ascii=False,
        ),
        encoding="utf-8",
    )

    plugins = game.plugins()
    report["plugins"] = [
        {
            "name": p["name"],
            "enabled": p["status"],
            "size": (game.root / "js" / "plugins" / (p["name"] + ".js")).stat().st_size
            if (game.root / "js" / "plugins" / (p["name"] + ".js")).exists()
            else None,
        }
        for p in plugins
    ]
    (out / "plugin_params.json").write_text(
        json.dumps({p["name"]: p["parameters"] for p in plugins}, indent=1, ensure_ascii=False), encoding="utf-8"
    )

    hidden = jsunpack.extract_hidden_script(game)
    if hidden:
        hidden = jsunpack.deobfuscate(hidden)
        (out / "hidden.js").write_text(hidden, encoding="utf-8")
        defs = collections.Counter()
        for m in re.finditer(r"(?:^|[;,}\s])([A-Z][A-Za-z_]+)(\.prototype)?\.([A-Za-z_]+)=function", hidden):
            defs[m.group(1) + (".prototype" if m.group(2) else "")] += 1
        funcs = re.findall(r"function ([A-Za-z_][\w]*)\(", hidden)
        report["hidden_script"] = {
            "chars": len(hidden),
            "overrides": dict(defs.most_common()),
            "functions": sorted(set(f for f in funcs if not f.startswith("_0x"))),
        }
        for m in re.findall(r'"((?:img|audio)/[a-z0-9]+/[0-9a-f]{16})"', hidden):
            folder, name = m.rsplit("/", 1)
            refs["@hashed:" + folder].add(name)

    system = game.json("data/System.json")
    for key in ("titleBgm", "battleBgm", "victoryMe", "defeatMe", "gameoverMe"):
        a = system.get(key) or {}
        if a.get("name"):
            refs["audio/" + ("bgm" if key.endswith("Bgm") else "me")].add(a["name"])
    for se in system.get("sounds", []):
        if se.get("name"):
            refs["audio/se"].add(se["name"])
    if system.get("title1Name"):
        refs["img/titles1"].add(system["title1Name"])
    report["system"] = {
        "screen_from_core_engine": {
            k: v
            for p in plugins
            if p["name"] == "YEP_CoreEngine"
            for k, v in p["parameters"].items()
            if k in ("Screen Width", "Screen Height")
        },
        "tile_size": 48,
        "switches": len(system["switches"]),
        "variables": len(system["variables"]),
        "start": {"map": system["startMapId"], "x": system["startX"], "y": system["startY"]},
        "party": system["partyMembers"],
    }

    tilesets = game.json("data/Tilesets.json")
    for ts in tilesets:
        if ts:
            for n in ts["tilesetNames"]:
                if n:
                    refs["img/tilesets"].add(n)

    infos = game.json("data/MapInfos.json")
    maps = []
    note_tags = collections.Counter()
    event_note_tags = collections.Counter()
    for info in infos:
        if not info:
            continue
        path = "data/Map%03d.json" % info["id"]
        if not game.exists(path):
            continue
        m = game.json(path)
        for t in re.findall(r"<([^:>]+)", m.get("note", "")):
            note_tags[t] += 1
        for t, v in re.findall(r"<([^:>]+):([^>]*)>", m.get("note", "")):
            refs["@note:" + t].add(v)
        if m.get("parallaxName"):
            refs["img/parallaxes"].add(m["parallaxName"])
        evs = [e for e in m["events"] if e]
        for e in evs:
            for t in re.findall(r"<([^:>]+)", e.get("note", "")):
                event_note_tags[t] += 1
        maps.append(
            {
                "id": info["id"],
                "w": m["width"],
                "h": m["height"],
                "tileset": m["tilesetId"],
                "events": len(evs),
                "pages": sum(len(e["pages"]) for e in evs),
                "parallax": m.get("parallaxName", ""),
                "note": m.get("note", "")[:200],
            }
        )
    report["maps"] = {
        "count": len(maps),
        "max_w": max(m["w"] for m in maps),
        "max_h": max(m["h"] for m in maps),
        "size_hist": collections.Counter(f"{m['w']}x{m['h']}" for m in maps).most_common(15),
        "tilesets_used": collections.Counter(m["tileset"] for m in maps).most_common(),
        "note_tags": dict(note_tags.most_common()),
        "event_note_tags": dict(event_note_tags.most_common()),
        "total_events": sum(m["events"] for m in maps),
    }
    (out / "maps.json").write_text(json.dumps(maps, indent=1), encoding="utf-8")

    lang = game.language()
    report["language"] = {
        "name": lang["langName"],
        "version": lang["langVers"],
        "font": lang["fontFace"],
        "font_size": lang["fontSize"],
        "lines": len(lang["linesLUT"]),
        "labels": len(lang["labelLUT"]),
        "menus": len(lang["sysMenus"]),
        "sys_labels": list(lang["sysLabel"].keys()),
        "chars": sorted(
            set(
                ch
                for v in lang["linesLUT"].values()
                for page in v
                for line in (page if isinstance(page, list) else [page])
                for ch in str(line)
            )
        ),
    }

    assets = {}
    for folder in ["img/" + d for d in ("characters", "faces", "parallaxes", "pictures", "system", "tilesets", "titles1")] + [
        "audio/" + d for d in ("bgm", "bgs", "me", "se")
    ]:
        files = game.stored_files(folder)
        entry = {"files": len(files), "bytes": sum((game.root / f).stat().st_size for f in files)}
        if folder.startswith("img"):
            dims = []
            for f in files:
                d = crypto.decrypt(game.read_raw(f)[:64], f)
                s = png_size(d)
                if s:
                    dims.append(s)
            entry["max_w"] = max((d[0] for d in dims), default=0)
            entry["max_h"] = max((d[1] for d in dims), default=0)
            entry["over_1280x720"] = sum(1 for d in dims if d[0] > 1280 or d[1] > 720)
            entry["rgba_mb_full"] = round(sum(w * h * 4 for w, h in dims) / 2**20, 1)
            entry["rgba_mb_scaled"] = round(sum(w * h * 4 * 0.140625 for w, h in dims) / 2**20, 1)
            entry["dims"] = collections.Counter(f"{w}x{h}" for w, h in dims).most_common(8)
        else:
            secs = 0
            rates = collections.Counter()
            for f in files:
                info = ogg_info(game.read_stored(f))
                if info:
                    secs += info["seconds"]
                    rates[(info["rate"], info["channels"])] += 1
            entry["seconds"] = round(secs)
            entry["formats"] = [f"{r}Hz/{c}ch x{n}" for (r, c), n in rates.items()]
        stored_names = {f.rsplit("/", 1)[1] for f in files}
        named = refs.get(folder, set())
        hits = set()
        for n in named:
            r = game.resolve(f"{folder}/{n}." + ("png" if folder.startswith("img") else "ogg"))
            if r:
                hits.add(r.rsplit("/", 1)[1])
        for key in ("@hashed:" + folder,):
            hits |= refs.get(key, set()) & stored_names
        for key, vals in refs.items():
            if key.startswith("@note:"):
                hits |= vals & stored_names
                for v in vals:
                    r = game.resolve(f"{folder}/{v}.png")
                    if r:
                        hits.add(r.rsplit("/", 1)[1])
        entry["referenced_names"] = len(named)
        entry["resolved_files"] = len(hits)
        assets[folder] = entry
    report["assets"] = assets

    (out / "report.json").write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k not in ("sample_text",)}, indent=1, ensure_ascii=False)[:20000])


if __name__ == "__main__":
    main()
