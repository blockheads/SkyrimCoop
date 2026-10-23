"""Every Papyrus native a binary registers, and which ones reach the player singleton.

Lists each native's class, name, signature (its NativeFunctionN vftable: self type, return type, argument types) and
C++ function, and the player's singletons its function, or a callee down to `depth` calls (tail jumps included),
reads: the player (g_thePlayer 403521, gPlayerCharacter3 401069), its misc stats (g_MiscStatManager), and this
game's screen (the menus' UIMessageQueue and UI, PlayerControls, ControlMap, MenuControls, PlayerCamera,
ImageSpaceManager, BSInputDeviceManager, ScreenSplatter, the gamepad's rumble queues). It is IDA's reading of the rule the plugin applies at
run time (Papyrus/NativeReach: a global native reaching one is the player's, docs/re/systems/papyrus-guest-scripts.md,
"Natives on the player"); its output over the game, docs/re/papyrus_natives.tsv, is what the scenario
natives-reach-matches-ida compares the plugin's reading with.

How natives are found: each registration builds a NativeFunctionN through NativeFunction::Impl_ctor (104653,
`(this, name, class, isStatic, paramCount)`), either inline (the name and class are `lea rdx` / `lea r8` before the
call, the vftable and the function stored into `[this]` and `[this+50h]` after it) or in a per-signature constructor
helper `(this, name, class, function, vm)` whose callers pass the strings in rdx / r8 and the function in r9.

Run it in IDA (File > Script file...: writes papyrus_natives.tsv next to the database), or paste what lies between
the two py_eval markers into the IDA MCP's py_eval followed by `as_tsv(papyrus_natives())` (no imports there:
idautils, idc and ida_funcs are its globals, and nothing outside a function is seen inside one). For an SKSE plugin DLL, point CTOR at its NativeFunction constructor and PLAYER at the
singletons it reads (its import of the Address Library resolves at run time: give the game's addresses it loads).
"""
if __name__ == "__main__":
    import ida_funcs  # noqa: F401
    import ida_nalt
    import idautils  # noqa: F401
    import idc  # noqa: F401

# --- py_eval from here ---


def papyrus_natives(ctor=None, player=None, depth=3, only_player=True):
    """Rows (class, name, self type, return type, arguments, function, the player's singletons it reads and how).
    player: {address: label} of the singletons to look for (default: the game's, by IDA name)."""
    base = idc.get_inf_attr(idc.INF_MIN_EA) & ~0xFFFFFFF
    ctor = ctor or base + 0x143E0E0  # NativeFunction::Impl_ctor, 104653
    if player is None:
        named = {"g_thePlayer": "player", "gPlayerCharacter3": "player", "g_MiscStatManager": "player's stats",
                 "UIMessageQueue::singleton": "screen: menus", "UI::singleton": "screen: menus",
                 "PlayerControls::singleton": "screen: controls", "ControlMap::singleton": "screen: controls",
                 "MenuControls::singleton": "screen: controls", "PlayerCamera::singleton": "screen: camera",
                 "ImageSpaceManager::singleton": "screen: imagespace", "BSInputDeviceManager::singleton": "screen: input",
                 "ScreenSplatter::singleton": "screen: blood"}
        player = {idc.get_name_ea_simple(n): f"{kind} ({n})" for n, kind in named.items() if idc.get_name_ea_simple(n) != idc.BADADDR}
        # Unnamed in IDA: the gamepad's rumble queues (410222, 410225), read by BSInputDeviceManager::PollInputDevices.
        player.update({base + 0x31CC3A0: "screen: rumble (410222)", base + 0x31CC3B8: "screen: rumble (410225)"})

    def previous(ea, count):
        for _ in range(count):
            ea = idc.prev_head(ea, ea - 64)
            yield ea

    def following(ea, count):
        for _ in range(count):
            ea = idc.next_head(ea, ea + 64)
            yield ea

    def loaded(ea, register):
        """What `lea register, x` before ea loads (x), or None: the closest write to the register decides."""
        for at in previous(ea, 12):
            if idc.print_operand(at, 0) == register and idc.print_insn_mnem(at) in ("lea", "mov", "xor"):
                return idc.get_operand_value(at, 1) if idc.print_insn_mnem(at) == "lea" else None
        return None

    def text(ea):
        value = idc.get_strlit_contents(ea) if ea else None
        return value.decode(errors="replace") if value else None

    def lea_before(ea, register, count=400):
        """What the closest `lea register, x` before ea (in its function) loads; the compiler hoists some."""
        start = idc.get_func_attr(ea, idc.FUNCATTR_START)
        for at in previous(ea, count):
            if at < start:
                break
            if idc.print_insn_mnem(at) == "lea" and idc.print_operand(at, 0) == register:
                return idc.get_operand_value(at, 1)
        return None

    def stored_after(ea, offset, count=24):
        """What the first `mov [reg+offset], source` after ea stores: a `lea source, x` loaded before it."""
        for at in following(ea, count):
            target = idc.print_operand(at, 0)
            if idc.print_insn_mnem(at) == "mov" and target.startswith("[") and (target.endswith(f"+{offset}]") if offset else "+" not in target):
                source = idc.print_operand(at, 1)
                if source.startswith("r"):
                    return lea_before(at, source)
        return None

    def vftable_after(ea):
        vftable = stored_after(ea, "")
        name = idc.get_name(vftable) if vftable else ""
        return name if name.startswith("??_7?$Native") else None

    def function_after(ea):
        """The function stored into [this+50h] after the constructor."""
        return stored_after(ea, "50h")

    def signature(vftable):
        """(self type, return type, [argument types]) from NativeFunctionN<Self, Return, Args...>'s demangled name."""
        demangled = idc.demangle_name(vftable, idc.get_inf_attr(idc.INF_SHORT_DN)) or vftable
        inner = demangled[demangled.find("<") + 1:demangled.rfind(">")]
        parts, level, current = [], 0, ""
        for character in inner:
            if character == "," and level == 0:
                parts.append(current.strip())
                current = ""
                continue
            level += character == "<"
            level -= character == ">"
            current += character
        parts.append(current.strip())
        clean = [p.replace("BSScript::", "").replace("class ", "").replace("struct ", "").replace(" const &", "")
                 .replace(" *", "").replace("__ptr64", "").strip() for p in parts]
        return clean[0], clean[1] if len(clean) > 1 else "?", clean[2:]

    natives = []
    helpers = {}
    for xref in idautils.XrefsTo(ctor):
        site = xref.frm
        if idc.print_insn_mnem(site) != "call":
            continue
        name, klass = text(loaded(site, "rdx")), text(loaded(site, "r8"))
        if name and klass:
            natives.append((klass, name, vftable_after(site), function_after(site)))
        else:
            helper = idc.get_func_attr(site, idc.FUNCATTR_START)
            helpers[helper] = vftable_after(site)
    for helper, vftable in helpers.items():
        for xref in idautils.XrefsTo(helper):
            if idc.print_insn_mnem(xref.frm) not in ("call", "jmp"):
                continue
            name, klass = text(loaded(xref.frm, "rdx")), text(loaded(xref.frm, "r8"))
            if name and klass:
                natives.append((klass, name, vftable, loaded(xref.frm, "r9")))

    reached = {}

    def reaches(function, left):
        """{singleton: the path to it} for what the function, or a callee down to `left` calls, reads."""
        key = (function, left)
        if key in reached:
            return reached[key]
        reached[key] = {}
        found = {}
        callees = []
        for at in idautils.FuncItems(function):
            for ref in idautils.DataRefsFrom(at):
                if ref in player:
                    found.setdefault(player[ref], "")
            if idc.print_insn_mnem(at) in ("call", "jmp"):
                target = idc.get_operand_value(at, 0)
                callee = ida_funcs.get_func(target)
                if callee and callee.start_ea == target and target != function:
                    callees.append(target)
        if left > 0:
            for callee in dict.fromkeys(callees):
                for singleton, path in reaches(callee, left - 1).items():
                    found.setdefault(singleton, f"{idc.get_func_name(callee)} > {path}".rstrip(" >"))
        reached[key] = found
        return found

    rows = []
    for klass, name, vftable, function in sorted(set(natives), key=lambda n: (n[0].lower(), n[1].lower())):
        self_type, returns, arguments = signature(vftable) if vftable else ("?", "?", [])
        if function and not ida_funcs.get_func(function):
            ida_funcs.add_func(function)  # a few leaf natives were never made functions (EnableFastTravel, ServeTime)
        found = reaches(function, depth) if function and ida_funcs.get_func(function) else {}
        how = "; ".join(f"{singleton}{': ' + path if path else ''}" for singleton, path in sorted(found.items()))
        if how or not only_player:
            rows.append((klass, name, self_type, returns, ", ".join(arguments), idc.get_func_name(function) if function else "?", how))
    return rows


def as_tsv(rows):
    header = "class\tname\tself\treturns\targuments\tfunction\treaches the player"
    return "\n".join([header] + ["\t".join(map(str, row)) for row in rows])

# --- py_eval to here ---


if __name__ == "__main__":
    import os

    path = os.path.join(os.path.dirname(idc.get_idb_path()), "papyrus_natives.tsv")
    with open(path, "w") as out:
        out.write(as_tsv(papyrus_natives()) + "\n")
    print(f"papyrus_natives: {path}")
