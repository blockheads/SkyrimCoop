#!/usr/bin/env python3
"""Compare two Skyrim SE saves (.ess) block by block: what differs between two games' worlds.

  save_diff.py HOST.ess CLIENT.ess [--json] [--details N] [--noise A.ess B.ess]

Change forms are matched by their real form id (each save's RefIDs resolved through its own form id
array). Forms created during play (0xFF......) differ per game by design and are listed apart. The
player's own character, camera, menus and controls differ per player by design: they are marked
"expected", not hidden. Quests are compared field by field (docs/re/systems/quest-change-form.md).
With --noise, what differs between two saves of one game (A and B, seconds apart) is marked "noise". Format: UESP "Skyrim Mod:Save File Format"; flag names from CommonLibSSE-NG.
Findings: docs/research/world-state-drift.md.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import zlib
from collections import Counter, defaultdict

import lz4.block

GLOBAL_NAMES = {
    0: "Misc Stats", 1: "Player Location", 2: "TES", 3: "Global Variables", 4: "Created Objects",
    5: "Effects", 6: "Weather", 7: "Audio", 8: "SkyCells",
    100: "Process Lists", 101: "Combat", 102: "Interface", 103: "Actor Causes", 104: "Unknown 104",
    105: "Detection Manager", 106: "Location MetaData", 107: "Quest Static Data", 108: "StoryTeller",
    109: "Magic Favorites", 110: "PlayerControls", 111: "Story Event Manager", 112: "Ingredient Shared",
    113: "MenuControls", 114: "MenuTopicManager",
    1000: "Temp Effects", 1001: "Papyrus", 1002: "Anim Objects", 1003: "Timer",
    1004: "Synchronized Animations", 1005: "Main",
}

# Change form type (low 6 bits of the type byte) -> record name.
FORM_TYPES = [
    "REFR", "ACHR", "PMIS", "PGRE", "PBEA", "PFLA", "CELL", "INFO", "QUST", "NPC_", "ACTI", "TACT", "ARMO",
    "BOOK", "CONT", "DOOR", "INGR", "LIGH", "MISC", "APPA", "STAT", "MSTT", "FURN", "WEAP", "AMMO", "KEYM",
    "ALCH", "IDLM", "NOTE", "ECZN", "CLAS", "FACT", "PACK", "NAVM", "WOOP", "MGEF", "SMQN", "SCEN", "LCTN",
    "RELA", "PHZD", "PBAR", "PCON", "FLST", "LVLN", "LVLI", "LVSP", "PARW", "ENCH",
]

# Change flags per form type (CommonLibSSE-NG's ChangeFlags enums). Bit 0 is TESForm::kFlags for all.
_OBJECT = {1: "ObjectValue", 2: "ObjectFullName"}
_REFR = {1: "Moved", 2: "HavokMoved", 3: "CellChanged", 4: "Scale", 5: "Inventory", 6: "OwnershipExtra",
         7: "BaseObject", 10: "ItemExtraData", 11: "AmmoExtra", 12: "LockExtra", 17: "TeleportExtra",
         21: "Empty", 22: "OpenDefaultState", 23: "OpenState", 25: "Promoted", 26: "ActivatingChildren",
         27: "LeveledInventory", 28: "Animation", 29: "EncZoneExtra", 30: "CreatedOnlyExtra",
         31: "GameOnlyExtra"}
_ACHR = {**_REFR, 10: "LifeState", 11: "PackageExtraData", 12: "MerchantContainer", 17: "DismemberedLimbs",
         18: "LeveledActor", 19: "DispModifiers", 20: "TempModifiers", 21: "DamageModifiers",
         22: "OverrideModifiers", 23: "PermanentModifiers"}
_LEVELED = {31: "AddedObject"}
FLAG_NAMES = {
    "REFR": _REFR, "ACHR": _ACHR, "PMIS": _REFR, "PGRE": _REFR, "PBEA": _REFR, "PFLA": _REFR, "PHZD": _REFR,
    "PBAR": _REFR, "PCON": _REFR, "PARW": _REFR,
    "NPC_": {1: "BaseData", 2: "Attributes", 3: "AIData", 4: "SpellList", 5: "FullName", 6: "Factions",
             9: "NPCSkills", 10: "Class", 11: "Face", 12: "DefaultOutfit", 13: "SleepOutfit", 24: "Gender",
             25: "Race"},
    "QUST": {1: "QuestFlags", 2: "QuestScriptDelay", 26: "AlreadyRun", 27: "InstanceData", 28: "RuntimeData",
             29: "Objectives", 30: "Script", 31: "Stages"},
    "CELL": {1: "Flags", 2: "FullName", 3: "Ownership", 28: "ExteriorShort", 29: "ExteriorChar",
             30: "DetachTime", 31: "SeenData"},
    "FACT": {1: "FactionFlags", 2: "Reactions", 31: "CrimeCounts"},
    "BOOK": {**_OBJECT, 5: "TeachesSkill", 6: "Read"},
    "DOOR": {**_OBJECT, 17: "TeleportExtra"},
    "TACT": {**_OBJECT, 23: "Speaker"},
    "INGR": {**_OBJECT, 31: "IngredientUse"},
    "INFO": {31: "SaidOnce"}, "PACK": {26: "Waiting", 31: "NeverRun"}, "NOTE": {31: "NoteRead"},
    "ECZN": {1: "ZoneFlags", 31: "GameData"}, "CLAS": {1: "TagSkills"}, "SMQN": {31: "TimeLastRun"},
    "SCEN": {31: "Active"}, "LCTN": {30: "KeywordData", 31: "Cleared"}, "RELA": {1: "RelationshipData"},
    "FLST": {31: "AddedForm"}, "LVLN": _LEVELED, "LVLI": _LEVELED, "LVSP": _LEVELED,
}
for _name in ("ACTI", "ARMO", "CONT", "LIGH", "MISC", "APPA", "STAT", "MSTT", "FURN", "WEAP", "AMMO",
              "KEYM", "ALCH", "IDLM", "ENCH", "MGEF"):
    FLAG_NAMES.setdefault(_name, _OBJECT)

# Report classes: drift (world state that differs), actors (an NPC's own state: the copy contract
# syncs loaded ones, unloaded ones run on each game), noise (also differs between two saves of one game
# seconds apart, --noise), expected (per player by design).
CLASSES = ("drift", "actors", "noise", "expected")

# What differs per player by design (the design's "each player's character stays their own").
PLAYER_REF, PLAYER_BASE = 0x14, 0x7
EXPECTED_FORMS = {PLAYER_REF: "the player's own character (position, inventory, values)",
                  PLAYER_BASE: "the player's own base (name, face, skills)"}
EXPECTED_GLOBALS = {0: "the player's own stats", 1: "the player's own location", 102: "the player's menus",
                    109: "the player's favourites", 110: "the player's controls", 113: "the player's menus",
                    114: "the player's dialogue menu"}


DATA = Path(os.environ.get("SKYRIM_GAME_DIR", "/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/"
                           "SteamLibrary/steamapps/common/Skyrim Special Edition")) / "Data"
# Records whose editor ids name a difference. References without one are named by their base form.
NAMED_RECORDS = {b"QUST", b"GLOB", b"FACT", b"NPC_", b"SMQN", b"SMBN", b"SMEN", b"LCTN", b"SCEN", b"CELL",
                 b"DOOR", b"CONT", b"PACK", b"FLST", b"RELA", b"ECZN", b"BOOK", b"MISC", b"KEYM", b"STAT",
                 b"ACTI", b"FURN", b"WEAP", b"ARMO", b"ALCH", b"LIGH", b"FLOR", b"TACT", b"INGR", b"AMMO",
                 b"SCRL", b"SLGM", b"NOTE", b"WRLD", b"MSTT", b"LVLN", b"REFR", b"ACHR", b"PGRE", b"PHZD"}
REFERENCES = {b"REFR", b"ACHR", b"PGRE", b"PHZD"}


def plugin_editor_ids(path):
    """(masters, {form id as the plugin writes it: editor id}, {reference: its base}) of one plugin."""
    data = path.read_bytes()
    size = struct.unpack_from("<I", data, 4)[0]
    masters, o = [], 24
    while o < 24 + size:
        kind, length = data[o:o + 4], struct.unpack_from("<H", data, o + 4)[0]
        if kind == b"MAST":
            masters.append(data[o + 6:o + 5 + length].decode("cp1252"))
        o += 6 + length
    names, bases = {}, {}

    def walk(o, end):
        while o < end:
            kind, size = data[o:o + 4], struct.unpack_from("<I", data, o + 4)[0]
            if kind == b"GRUP":
                label, group_type = data[o + 8:o + 12], struct.unpack_from("<i", data, o + 12)[0]
                if group_type != 0 or label in NAMED_RECORDS:
                    walk(o + 24, o + size)
                o += size
                continue
            flags, form_id = struct.unpack_from("<II", data, o + 8)
            if kind in NAMED_RECORDS:
                body = data[o + 24:o + 24 + size]
                if flags & 0x40000:
                    body = zlib.decompress(body[4:])
                p = 0
                while p + 6 <= len(body):
                    sub, length = body[p:p + 4], struct.unpack_from("<H", body, p + 4)[0]
                    if sub == b"EDID":
                        names[form_id] = body[p + 6:p + 5 + length].decode("cp1252")
                    elif sub == b"NAME" and kind in REFERENCES:
                        bases[form_id] = struct.unpack_from("<I", body, p + 6)[0]
                        break
                    elif kind not in REFERENCES:
                        break
                    p += 6 + length
            o += 24 + size

    walk(24 + size, len(data))
    return masters, names, bases


def editor_ids(save, data_dir=DATA):
    """{load-order form id: editor id, or "[base's editor id]" for a reference without one} over the
    save's plugins found in data_dir."""
    order = {name.lower(): index for index, name in enumerate(save.plugins)}
    light = {name.lower(): index for index, name in enumerate(save.light_plugins)}
    names, references = {}, {}
    for plugin in save.plugins + save.light_plugins:
        path = data_dir / plugin
        if not path.exists():
            continue
        masters, local, bases = plugin_editor_ids(path)

        def load_order(form_id):
            index = form_id >> 24
            owner = (masters[index] if index < len(masters) else plugin).lower()
            if owner in order:
                return order[owner] << 24 | form_id & 0xFFFFFF
            if owner in light:
                return 0xFE000000 | light[owner] << 12 | form_id & 0xFFF
            return None

        for form_id, name in local.items():
            if (resolved := load_order(form_id)) is not None:
                names[resolved] = name
        for form_id, base in bases.items():
            if (resolved := load_order(form_id)) is not None:
                references[resolved] = load_order(base)
    for reference, base in references.items():
        if reference not in names and base in names:
            names[reference] = f"[{names[base]}]"
    return names


def flag_names(form_type, flags):
    names = FLAG_NAMES.get(form_type, {})
    return [("Flags" if bit == 0 else names.get(bit, f"bit{bit}")) for bit in range(32) if flags >> bit & 1]


class Reader:
    def __init__(self, data, offset=0):
        self.data, self.offset = data, offset

    def take(self, size):
        chunk = self.data[self.offset:self.offset + size]
        if len(chunk) != size:
            raise ValueError(f"truncated at {self.offset}")
        self.offset += size
        return chunk

    def unpack(self, fmt):
        values = struct.unpack_from("<" + fmt, self.data, self.offset)
        self.offset += struct.calcsize("<" + fmt)
        return values if len(values) > 1 else values[0]

    def wstring(self):
        return self.take(self.unpack("H")).decode("cp1252", errors="replace")

    def refid(self):
        b = self.take(3)
        return b[0] << 16 | b[1] << 8 | b[2]

    def vsval(self):
        first = self.data[self.offset]
        size = first & 3
        raw = self.unpack({0: "B", 1: "H", 2: "I"}[size]) if size < 3 else self.unpack("I")
        return raw >> 2


class Save:
    """One parsed .ess: header, plugins, global data {type: bytes}, change forms {form id: entry}."""

    def __init__(self, path):
        self.path = path
        data = open(path, "rb").read()
        if data[:13] != b"TESV_SAVEGAME":
            raise ValueError(f"{path}: not a Skyrim save")
        r = Reader(data, 13)
        header_size = r.unpack("I")
        self.header = h = {}
        h["version"], h["save_number"] = r.unpack("II")
        h["player"] = r.wstring()
        h["level"] = r.unpack("I")
        h["location"], h["date"], h["race"] = r.wstring(), r.wstring(), r.wstring()
        h["sex"], h["xp"], h["xp_needed"] = r.unpack("Hff")
        h["filetime"], width, height, compression = r.unpack("QIIH")
        # File location table offsets count from the file start as if the body were not compressed.
        base = 17 + header_size + width * height * 4
        r.offset = base
        if compression:
            uncompressed, compressed = r.unpack("II")
            packed = r.take(compressed)
            body = lz4.block.decompress(packed, uncompressed_size=uncompressed) if compression == 2 \
                else zlib.decompress(packed)
        else:
            body = data[base:]
        self.body = body
        b = Reader(body)
        self.form_version = b.unpack("B")
        b.unpack("I")
        self.plugins = [b.wstring() for _ in range(b.unpack("B"))]
        self.light_plugins = [b.wstring() for _ in range(b.unpack("H"))] if h["version"] >= 12 else []
        (formid_offset, unknown3_offset, g1_offset, g2_offset, changes_offset, g3_offset,
         g1_count, g2_count, g3_count, change_count) = b.unpack("10I")
        b.take(15 * 4)

        def at(offset, what):
            if b.offset + base != offset:
                raise ValueError(f"{path}: {what} at {b.offset + base}, table says {offset}")

        # The form id array first: change forms and global data refer to forms through it.
        r2 = Reader(body, formid_offset - base)
        self.formids = [r2.unpack("I") for _ in range(r2.unpack("I"))]
        self.worldspaces = [r2.unpack("I") for _ in range(r2.unpack("I"))]

        self.globals = {}
        at(g1_offset, "global data 1")
        self._global_table(b, g1_count)
        at(g2_offset, "global data 2")
        self._global_table(b, g2_count)
        at(changes_offset, "change forms")
        self.forms, self.created = {}, {}
        for _ in range(change_count):
            ref = b.refid()
            flags, type_byte, version = b.unpack("IBB")
            length_format = "BHI"[type_byte >> 6]
            length1, length2 = b.unpack(length_format * 2)
            raw = b.take(length1)
            payload = zlib.decompress(raw) if length2 else raw
            form_type = type_byte & 0x3F
            form_id = self.resolve(ref)
            entry = {"type": FORM_TYPES[form_type] if form_type < len(FORM_TYPES) else f"type{form_type}",
                     "flags": flags, "version": version, "data": payload}
            (self.created if form_id >> 24 == 0xFF else self.forms)[form_id] = entry
        at(g3_offset, "global data 3")
        # The table's count leaves out Papyrus (1001): read on to the form id array.
        while b.offset + base < formid_offset:
            self._global_table(b, 1)

    def _global_table(self, reader, count):
        for _ in range(count):
            kind, length = reader.unpack("II")
            self.globals[kind] = reader.take(length)

    def resolve(self, ref):
        """A RefID's real form id: an index into the form id array (1-based), a default form, or created."""
        kind, value = ref >> 22, ref & 0x3FFFFF
        if kind == 0:
            return self.formids[value - 1] if 0 < value <= len(self.formids) else 0
        if kind == 1:
            return value
        if kind == 2:
            return 0xFF000000 | value
        return 1 << 32 | ref  # type 3: unknown, kept apart from real ids

    def global_variables(self):
        """Global data 3: {global's form id: value}."""
        data = self.globals.get(3)
        if data is None:
            return {}
        r = Reader(data)
        values = {}
        for _ in range(r.vsval()):
            ref = r.refid()
            values[self.resolve(ref)] = r.unpack("f")
        return values


def parse_quest(save, entry):
    """A QUST change form's fields, in the order TESQuest::WriteToSaveGame (25029) writes them. Forms are
    real form ids (through the save's form id array). "rest" counts bytes left over (0: parsed whole)."""
    r, flags, quest = Reader(entry["data"]), entry["flags"], {}

    def form():
        return save.resolve(r.refid())

    if flags & 1:
        quest["form_flags"] = r.unpack("IH")
    if flags & 1 << 1:
        quest["quest_flags"] = r.unpack("H")
    if flags & 1 << 2:
        quest["script_delay"] = r.unpack("f")
    if flags & 1 << 31:
        quest["stages"] = {}
        for _ in range(r.vsval()):
            stage, done = r.unpack("hB")
            quest["stages"][stage] = done
    if flags & 1 << 29:
        quest["objectives"] = dict(r.unpack("II") for _ in range(r.vsval()))
    if flags & 1 << 28:
        quest["promoting"] = r.unpack("B")
        quest["ref_aliases"] = {}
        for _ in range(r.unpack("I")):
            alias, temporary = r.unpack("IB")
            fill = {"ref": form()}
            if fill["ref"] and temporary:
                # A temporary reference (created for the quest): its base, two extra-data forms, its cell.
                fill.update(base=form(), extra1=form(), extra2=form(), cell=form())
            quest["ref_aliases"][alias] = fill
        quest["location_aliases"] = {r.unpack("I"): form() for _ in range(r.unpack("I"))}
        if r.unpack("B"):
            # The story manager event that started the quest (BGSStoryEventManager): its data by kind.
            event = {"header": r.unpack("I"), "type": r.unpack("I").to_bytes(4, "little").decode("latin1"),
                     "data": []}
            for _ in range(r.unpack("I")):
                kind = r.unpack("I")
                event["data"].append(r.unpack("I") if kind == 3 else form())
            quest["start_event"] = event
    if flags & 1 << 27:
        quest["instance_id"] = r.unpack("I")
        quest["instances"] = []
        for _ in range(r.vsval()):
            instance = {"id": r.unpack("I")}
            instance["a"] = [(r.unpack("I"), form()) for _ in range(r.vsval())]
            instance["b"] = [(form(), r.unpack("I")) for _ in range(r.vsval())]
            instance["tail"] = r.unpack("HB")
            quest["instances"].append(instance)
    if flags & 1 << 26:
        quest["already_run"] = r.unpack("B")
    quest["rest"] = len(entry["data"]) - r.offset
    return quest


def quest_differences(host, client, a, b, names):
    """What differs between two parsed QUST change forms, field by field, readable."""
    def name(form_id):
        return form_label(host, form_id, names) if form_id else "none"

    def fill(f):
        if not f:
            return "unfilled"
        if f.get("base") is not None:  # temporary: its id is the game's own, its base says what it is
            return f"temporary {name(f['base'])} in {name(f.get('cell'))}"
        return name(f["ref"])

    out = []
    for key in ("form_flags", "quest_flags", "script_delay", "promoting", "instance_id", "already_run"):
        if a.get(key) != b.get(key):
            value = (lambda v: f"{v:#06x}") if key == "quest_flags" else repr
            out.append(f"{key} {value(a.get(key)) if a.get(key) is not None else '-'} / "
                       f"{value(b.get(key)) if b.get(key) is not None else '-'}")
    for key, label in (("stages", "stage"), ("objectives", "objective")):
        for index in sorted(set(a.get(key, {})) | set(b.get(key, {}))):
            va, vb = a.get(key, {}).get(index), b.get(key, {}).get(index)
            if va != vb:
                out.append(f"{label} {index}: {va if va is not None else '-'} / {vb if vb is not None else '-'}")
    for index in sorted(set(a.get("ref_aliases", {})) | set(b.get("ref_aliases", {}))):
        fa, fb = a.get("ref_aliases", {}).get(index), b.get("ref_aliases", {}).get(index)
        if fill(fa) != fill(fb):
            out.append(f"alias {index}: {fill(fa)} / {fill(fb)}")
    for index in sorted(set(a.get("location_aliases", {})) | set(b.get("location_aliases", {}))):
        la, lb = a.get("location_aliases", {}).get(index), b.get("location_aliases", {}).get(index)
        if la != lb:
            out.append(f"location alias {index}: {name(la)} / {name(lb)}")
    ea, eb = a.get("start_event"), b.get("start_event")
    if (ea and (ea["type"], ea["data"])) != (eb and (eb["type"], eb["data"])):
        def event(e):
            return f"{e['type']}({', '.join(name(d) if d > 0xFFFF else str(d) for d in e['data'])})" if e else "-"
        out.append(f"start event {event(ea)} / {event(eb)}")
    if a.get("instances") != b.get("instances"):
        out.append(f"instances {len(a.get('instances', []))} / {len(b.get('instances', []))} (differ)")
    for side, parsed in (("host", a), ("client", b)):
        if parsed["rest"]:
            out.append(f"{side}: {parsed['rest']} bytes not parsed")
    return out


def remapped_id(a, b, start, save_a, save_b):
    """The 3 bytes at `start` are RefIDs naming the same form in each save (the two form id arrays order
    forms differently)."""
    ra = a[start] << 16 | a[start + 1] << 8 | a[start + 2]
    rb = b[start] << 16 | b[start + 1] << 8 | b[start + 2]
    return ra >> 22 == rb >> 22 == 0 and ra != rb and save_a.resolve(ra) == save_b.resolve(rb) != 0


def equivalent(a, b, save_a, save_b, stamps=frozenset()):
    """Equal bytes, except at RefIDs that name the same form and at clock stamps (4-byte values the
    engine writes into many forms at once, see find_stamps). A heuristic: payloads are not parsed."""
    if a == b:
        return True
    if len(a) != len(b):
        return False
    i, n = 0, len(a)
    while i < n:
        if a[i] == b[i]:
            i += 1
            continue
        for start in range(max(0, i - 3), i + 1):
            if start + 4 <= n and (a[start:start + 4], b[start:start + 4]) in stamps:
                i = start + 4
                break
            if start >= i - 2 and start + 3 <= n and remapped_id(a, b, start, save_a, save_b):
                i = start + 3
                break
        else:
            return False
    return True


def find_stamps(save_a, save_b, minimum=20):
    """4-byte (a, b) value pairs that differ in at least `minimum` forms: the engine's clocks (a seconds
    timer, counters) written into every actor. They differ between any two saves taken apart in time."""
    seen = Counter()
    for form_id, entry in save_a.forms.items():
        other = save_b.forms.get(form_id)
        if not other or entry["data"] == other["data"] or len(entry["data"]) != len(other["data"]):
            continue
        a, b = entry["data"], other["data"]
        windows = set()
        for i in range(len(a)):
            if a[i] != b[i]:
                windows.update((a[s:s + 4], b[s:s + 4]) for s in range(max(0, i - 3), min(i, len(a) - 4) + 1))
        seen.update(windows)
    return frozenset(pair for pair, count in seen.items() if count >= minimum)


def first_difference(a, b):
    return next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))


def form_label(save, form_id, names=None):
    """'Skyrim.esm:012345 EditorId' (or '<light plugin>:123') for a form id, by the save's load order."""
    name = (names or {}).get(form_id)
    return _form_label(save, form_id) + (f" {name}" if name else "")


def _form_label(save, form_id):
    top = form_id >> 24
    if top == 0xFE:
        index = form_id >> 12 & 0xFFF
        name = save.light_plugins[index] if index < len(save.light_plugins) else f"FE{index:03X}"
        return f"{name}:{form_id & 0xFFF:03X}"
    if top == 0xFF:
        return f"created:{form_id:08X}"
    name = save.plugins[top] if top < len(save.plugins) else f"{top:02X}"
    return f"{name}:{form_id & 0xFFFFFF:06X}"


def noise_from(path_a, path_b):
    """What differs between two saves of one game: {"globals": {type}, "forms": {id: changed flags}}."""
    report = compare(Save(path_a), Save(path_b))
    return {"globals": {g["type"] for g in report["globals"]},
            "global_variables": {v["form"] for v in report["global_variables"]},
            "forms": {f["form"] for f in report["forms"]}}


def compare(host, client, noise=None, names=None):
    """Every difference between two saves, as a dict (see --json). `names`: editor ids for the labels."""
    noise = noise or {"globals": set(), "global_variables": set(), "forms": set()}
    stamps = find_stamps(host, client)
    report = {"host": host.path, "client": client.path, "header": {}, "plugins": [], "globals": [],
              "global_variables": [], "forms": [],
              "created": {"host": len(host.created), "client": len(client.created),
                          "host_types": dict(Counter(e["type"] for e in host.created.values())),
                          "client_types": dict(Counter(e["type"] for e in client.created.values()))},
              "counts": {"host_forms": len(host.forms), "client_forms": len(client.forms)},
              "stamps": len(stamps)}
    for key in host.header:
        if key != "save_number" and host.header[key] != client.header[key]:
            report["header"][key] = [host.header[key], client.header[key]]
    if host.plugins != client.plugins or host.light_plugins != client.light_plugins:
        report["plugins"] = [p for p in set(host.plugins + host.light_plugins) ^
                             set(client.plugins + client.light_plugins)] or ["(same plugins, other order)"]

    for kind in sorted(set(host.globals) | set(client.globals)):
        a, b = host.globals.get(kind), client.globals.get(kind)
        if a is not None and b is not None and equivalent(a, b, host, client, stamps):
            continue
        report["globals"].append({
            "type": kind, "name": GLOBAL_NAMES.get(kind, str(kind)),
            "host_size": None if a is None else len(a), "client_size": None if b is None else len(b),
            "first_difference": None if a is None or b is None else first_difference(a, b),
            "class": "expected" if kind in EXPECTED_GLOBALS else "noise" if kind in noise["globals"] else "drift",
            "why": EXPECTED_GLOBALS.get(kind)})

    host_vars, client_vars = host.global_variables(), client.global_variables()
    for form_id in sorted(set(host_vars) | set(client_vars)):
        a, b = host_vars.get(form_id), client_vars.get(form_id)
        if a != b:
            report["global_variables"].append({
                "form": form_id, "label": form_label(host, form_id, names), "host": a, "client": b,
                "class": "noise" if form_id in noise["global_variables"] else "drift"})

    for form_id in sorted(set(host.forms) | set(client.forms)):
        a, b = host.forms.get(form_id), client.forms.get(form_id)
        entry = {"form": form_id, "label": form_label(host if a else client, form_id, names),
                 "type": (a or b)["type"]}
        if a is None or b is None:
            entry.update(kind="only_host" if b is None else "only_client",
                         flags=flag_names(entry["type"], (a or b)["flags"]), size=len((a or b)["data"]))
        elif a["flags"] != b["flags"]:
            entry.update(kind="flags", host_only_flags=flag_names(entry["type"], a["flags"] & ~b["flags"]),
                         client_only_flags=flag_names(entry["type"], b["flags"] & ~a["flags"]),
                         flags=flag_names(entry["type"], a["flags"] | b["flags"]))
            if entry["type"] == "QUST":
                entry["fields"] = quest_differences(host, client, parse_quest(host, a), parse_quest(client, b), names)
        elif not equivalent(a["data"], b["data"], host, client, stamps) or a["version"] != b["version"]:
            entry.update(kind="data", flags=flag_names(entry["type"], a["flags"]),
                         host_size=len(a["data"]), client_size=len(b["data"]),
                         first_difference=first_difference(a["data"], b["data"]))
            if entry["type"] == "QUST":
                entry["fields"] = quest_differences(host, client, parse_quest(host, a), parse_quest(client, b), names)
        else:
            continue
        if form_id in EXPECTED_FORMS:
            entry.update({"class": "expected", "why": EXPECTED_FORMS[form_id]})
        elif form_id in noise["forms"]:
            entry["class"] = "noise"
        else:
            entry["class"] = "actors" if entry["type"] == "ACHR" else "drift"
        report["forms"].append(entry)
    return report


def print_report(report, details):
    print(f"host   {report['host']}\nclient {report['client']}\n")
    if report["plugins"]:
        print(f"PLUGINS DIFFER: {report['plugins']}\n")
    if report["header"]:
        print("Header: " + ", ".join(f"{k} {v[0]!r} / {v[1]!r}" for k, v in report["header"].items()))
    counts = report["counts"]
    created = report["created"]
    print(f"Change forms: host {counts['host_forms']}, client {counts['client_forms']} "
          f"(+ created: host {created['host']}, client {created['client']}, not compared); "
          f"{report['stamps']} clock-stamp byte patterns ignored\n")

    by_class = Counter(f["class"] for f in report["forms"])
    print("Summary (change forms that differ): " +
          (", ".join(f"{by_class[c]} {c}" for c in CLASSES if by_class[c]) or "none"))
    groups = defaultdict(Counter)
    for f in report["forms"]:
        if f["class"] == "expected":
            continue
        changed = f.get("host_only_flags", []) + f.get("client_only_flags", []) if f["kind"] == "flags" \
            else f["flags"]
        key = f"{f['kind']:11s} {'+'.join(changed) or '-'}"
        groups[(f["class"], f["type"])][key] += 1
    for (klass, form_type), keys in sorted(groups.items(), key=lambda g: (CLASSES.index(g[0][0]), -sum(g[1].values()))):
        print(f"  {klass:8s} {form_type}  {sum(keys.values())}")
        common = keys.most_common()
        for key, count in common[:8]:
            print(f"             {count:5d}  {key}")
        if len(common) > 8:
            print(f"             {sum(c for _, c in common[8:]):5d}  ({len(common) - 8} other flag sets)")
    print()
    print("Global data:")
    for g in report["globals"]:
        sizes = f"{g['host_size']} / {g['client_size']} bytes"
        print(f"  {g['class']:8s} {g['type']:4d} {g['name']:24s} {sizes}"
              + (f"  ({g['why']})" if g.get("why") else ""))
    if not report["globals"]:
        print("  all equal")
    print(f"Global variables that differ: {len(report['global_variables'])}")
    for v in report["global_variables"][:details]:
        print(f"  {v['class']:8s} {v['label']:40s} {v['host']:.6g} / {v['client']:.6g}"
              if v['host'] is not None and v['client'] is not None else
              f"  {v['class']:8s} {v['label']:40s} {v['host']} / {v['client']}")

    if details:
        print("\nDetails (first %d per class):" % details)
        for klass in CLASSES:
            shown = [f for f in report["forms"] if f["class"] == klass][:details]
            for f in shown:
                extra = {"only_host": f"only on host [{'+'.join(f['flags'])}]",
                         "only_client": f"only on client [{'+'.join(f['flags'])}]",
                         "flags": f"flags host+[{'+'.join(f.get('host_only_flags', []))}] "
                                  f"client+[{'+'.join(f.get('client_only_flags', []))}]",
                         "data": f"data [{'+'.join(f['flags'])}] {f.get('host_size')}/{f.get('client_size')} "
                                 f"bytes, from byte {f.get('first_difference')}"}[f["kind"]]
                print(f"  {klass:8s} {f['type']} {f['label']:44s} {extra}"
                      + (f"  ({f['why']})" if f.get("why") else ""))
                for field in f.get("fields", []):
                    print(f"             {field}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host")
    parser.add_argument("client")
    parser.add_argument("--json", action="store_true", help="the whole report as JSON")
    parser.add_argument("--details", type=int, default=25, help="forms listed per class (0: summary only)")
    parser.add_argument("--data", type=Path, default=DATA, help="the game's Data folder, for editor ids")
    parser.add_argument("--noise", nargs=2, metavar=("A", "B"),
                        help="two saves of one game seconds apart: what differs between them is marked noise")
    args = parser.parse_args()
    noise = noise_from(*args.noise) if args.noise else None
    host = Save(args.host)
    report = compare(host, Save(args.client), noise, editor_ids(host, args.data))
    if args.json:
        json.dump(report, sys.stdout, indent=1, default=lambda o: sorted(o) if isinstance(o, set) else str(o))
        print()
    else:
        print_report(report, args.details)


if __name__ == "__main__":
    main()
