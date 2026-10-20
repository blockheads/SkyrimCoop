"""What vanilla scripts do with "the player" after checking for it, by cause and by kind of use.

    player_scan.py OUT.json [SOURCES]   (docs/research/player-events.md, "Script scan")

Heuristic (regex over sources): handlers are events with their same-script functions inlined two levels.
"The player" is Game.GetPlayer(), and any variable or property named player*/assigned from GetPlayer().
"""
import collections, json, os, re, sys

SRC = sys.argv[2] if len(sys.argv) > 2 else "build/re/scripts/Source/Scripts"  # Data/Scripts.zip, extracted

# Events a player's own action raises (with the actor as an argument): the "caused" side.
CAUSED = {"onactivate", "ontriggerenter", "ontriggerleave", "ontrigger", "onhit", "ondeath", "ondying", "oncontainerchanged",
          "onitemadded", "onitemremoved", "onread", "onequipped", "onunequipped", "onmagiceffectapply", "ongrab",
          "onrelease", "onsell", "onopen", "onclose", "onlockstatechanged", "oncombatstatechanged", "onenterbleedout",
          "onspellcast", "ontranslationcomplete", "onsit", "ongetup", "onplayerbowshot", "onstoryactivateactor",
          "onstorykillactor", "onstoryhello", "onstoryassaultactor", "onstorycraftitem", "onstoryreadbook"}
# No player action behind them.
UNCAUSED = {"onupdate", "onupdategametime", "onload", "onunload", "oncellattach", "oncelldetach", "oninit",
            "onreset", "onattachedtocell", "ondetachedfromcell", "onanimationevent", "onlocationchange",
            "onpackagestart", "onpackageend", "onpackagechange", "onsleepstart", "onsleepstop"}

# What a call on/with the player is, by native name.
KINDS = {
    "shared": "additem removeitem getitemcount removeallitems dropobject getgoldvalue".split(),
    "party": ("addspell removespell addperk removeperk addtofaction removefromfaction setfactionrank modfactionrank "
              "setrelationshiprank addshout teachword unlockword setrace advanceskill incrementskill incrementstat "
              "modfavorpoints setcrimegold modcrimegold setplayerresistingarrest sendassaultalarm "
              "setplayerenemy setallowflying setplayerteammate").split(),
    "aimed": ("cast remotecast pushactoraway knockareaeffect damageav damageactorvalue restoreav restoreactorvalue "
              "kill killessential killsilent playidle playanimation playanimationandwait applyhavokimpulse "
              "startcombat stopcombat setghost setalpha equipitem unequipitem unequipall equipspell setposition "
              "setangle translateto moveto movetonode placeatme dispelspell dispelallspells setav setactorvalue "
              "modav modactorvalue forceav forceactorvalue resurrect sendtrespassalarm setunconscious "
              "setdontmove setrestrained setvehicle dismount playsubgraphanimation evaluatepackage "
              "setlookat clearlookat say playgamebryoanimation").split(),
    "position": ("getdistance isinlocation getcurrentlocation getparentcell isininterior getpositionx getpositiony "
                 "getpositionz getanglez getheadingangle haslos getworldspace isnearplayer getcurrentscene "
                 "isinfaction getav getactorvalue getbaseav getbaseactorvalue getlevel issneaking isdead "
                 "isincombat isweapondrawn issprinting isswimming isridinghorse isonmount getsitstate "
                 "getsleepstate isdetectedby isinkillmove getcombattarget getequippedweapon wornhaskeyword "
                 "haskeyword hasspell hasperk hasmagiceffect hasmagiceffectwithkeyword israce getrace "
                 "getactorbase getleveledactorbase isplayerslastriddenhorse getlightlevel getvoicetype "
                 "isequipped getequippeditemtype iscommandedactor isbleedingout isinscene isarrested").split(),
}
UI = ("show showasHelpMessage notification messagebox trace fadeoutgame imagespacemodifier apply applycrossfade "
      "disableplayercontrols enableplayercontrols forcefirstperson forcethirdperson shakecamera shakecontroller "
      "setincharGen showracemenu showlimitedracemenu requestsave requestautosave fadetoblackimod "
      "setcameratarget showtitlesequencemenu hidetitlesequencemenu").lower().split()
KIND_OF = {name: kind for kind, names in KINDS.items() for name in names}


def strip(source):
    source = re.sub(r";/.*?/;", "", source, flags=re.S)
    source = re.sub(r"\{[^}]*\}", "", source)
    return "\n".join(line.split(";")[0] for line in source.lower().splitlines())


def parse(source):
    """{(state, event): body}, {function: body}, player names."""
    events, functions, state = {}, {}, ""
    current, body = None, []
    for line in source.splitlines():
        line = line.strip()
        if m := re.match(r"(?:auto\s+)?state\s+(\w+)", line):
            state = m.group(1)
            continue
        if line.startswith("endstate"):
            state = ""
            continue
        if m := re.match(r"event\s+(\w+)\s*\(([^)]*)\)", line):
            current, body = ("event", state, m.group(1), m.group(2)), []
            continue
        if m := re.match(r"(?:\w+(?:\[\])?\s+)?function\s+(\w+)\s*\(([^)]*)\)", line):
            if "native" in line:
                continue
            current, body = ("function", state, m.group(1), m.group(2)), []
            continue
        if current and (line.startswith("endevent") or line.startswith("endfunction")):
            text = "\n".join(body)
            if current[0] == "event":
                events[(current[1], current[2])] = (current[3], text)
            else:
                functions.setdefault(current[2], text)
            current = None
            continue
        if current:
            body.append(line)
    names = {"game.getplayer()", "getplayer()"}
    for m in re.finditer(r"(?:actor|objectreference)\s+property\s+(\w+)", source):
        if "player" in m.group(1) and "horse" not in m.group(1) and "house" not in m.group(1):
            names.add(m.group(1))
    for m in re.finditer(r"(\w+)\s*=\s*(?:game\.)?getplayer\(\)", source):
        names.add(m.group(1))
    return events, functions, names


def inline(text, functions, depth=2):
    if depth == 0:
        return text
    extra = [inline(functions[f], functions, depth - 1) for f in set(re.findall(r"(?<![.\w])(\w+)\s*\(", text)) if f in functions]
    return text + "\n" + "\n".join(extra)


def player_pattern(names):
    alternatives = "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True))
    return rf"(?:\(\s*)?(?:{alternatives})(?:\s+as\s+\w+)?(?:\s*\))?"


def uses(text, names):
    """Calls on the player (`player.X(`) and calls with the player as an argument (`X(..., player ...)`)."""
    p = player_pattern(names)
    found = collections.Counter()
    for m in re.finditer(rf"{p}\s*\.\s*(\w+)\s*\(", text):
        found[m.group(1)] += 1
    for m in re.finditer(r"\.?(\w+)\s*\(([^()]*(?:\([^()]*\))?[^()]*)\)", text):
        if re.search(rf"(?:^|[,(\s]){p}(?:$|[,)\s])", m.group(2)) and m.group(1) not in ("if", "while"):
            found["arg:" + m.group(1)] += 1
    return found


POSITION = set("getdistance isinlocation getcurrentlocation getparentcell isininterior getpositionx getpositiony "
               "getpositionz getanglez getheadingangle haslos getworldspace isdetectedby getlightlevel "
               "getfacingtotarget pathtoreference keepoffsetfromactor findrandomactorfromref "
               "findclosestreferenceofanytypeinlistfromref isnearplayer".split())
TELEPORT = set("moveto movetonode translateto splinetranslateto setposition setangle".split())
AIMED_ARG = set("cast remotecast pushactoraway startcombat kill killessential setlookat damageav damageactorvalue "
                "restoreav applyhavokimpulse docombatspellapply createdetectionevent knockareaeffect "
                "playidlewithtarget castspellfromsun".split())
PLACED_AT = set("moveto placeatme".split())
FX = set("play stop".split())
LISTEN = set("registerforanimationevent unregisterforanimationevent unregisterforevents".split())
PARTY_ARG = set("sendstealalarm sendassaultalarm setplayerenemy processinfection vampireprogression vampirechange "
                "forcerefto getrelationshiprank getfactionreaction ishostiletoactor".split())


HOSTILE = set("startcombat stopcombat stopcombatalarm setattackactoronsight createdetectionevent sendassaultalarm".split())
UI_TOO = set("ramprumble displayallobjectives".split())


def kind(call):
    arg = call.startswith("arg:")
    name = call[4:] if arg else call
    if name in HOSTILE:
        return "party"  # hostile to the player: to the party; the host's AI picks the target
    if name in UI_TOO:
        return "ui"
    if arg:
        if name == "activate":
            return "activate as player"
        if name in PLACED_AT or name in POSITION:
            return "position"
        if name in AIMED_ARG:
            return "aimed"
        if name in FX:
            return "fx on player"
        if name in LISTEN:
            return "listens to body"
        if name in PARTY_ARG or name in KIND_OF and KIND_OF[name] in ("party", "shared"):
            return "party" if name not in KINDS["shared"] else "shared"
        return None  # a script function taking the ref (testForTrigger...), or a parse artifact
    if name in TELEPORT:
        return "teleport"
    if name in POSITION:
        return "position"
    if name in KIND_OF:
        return {"position": "state"}.get(KIND_OF[name], KIND_OF[name])
    if name in UI:
        return "ui"
    return "state" if name.startswith(("get", "is", "has")) else "aimed"


def classify(found):
    kinds = collections.Counter()
    for call, n in found.items():
        k = kind(call)
        if k:
            kinds[k] += n
    return kinds


IMPLICIT_UI = re.compile(r"\.show\(|debug\.notification|debug\.messagebox|showashelpmessage|disableplayercontrols|"
                         r"fadeoutgame|\.applycrossfade|imagespacemodifier|\w+imod\.apply|shakecamera")


def main():
    stats = collections.Counter()
    by_kind = collections.defaultdict(collections.Counter)
    calls = collections.defaultdict(collections.Counter)
    triggers = []
    for file in sorted(os.listdir(SRC)):
        if not file.lower().endswith(".psc"):
            continue
        source = strip(open(os.path.join(SRC, file), errors="ignore").read())
        if "player" not in source:
            continue
        name = file[:-4].lower()
        events, functions, names = parse(source)
        p = player_pattern(names)
        gate = re.compile(rf"(?:==|!=)\s*{p}|{p}\s*(?:==|!=)")
        fragment = name.startswith(("qf_", "sf_", "tif_", "pf_", "prkf_"))
        handlers = dict(events)
        if fragment:
            handlers.update({("", f): ("", b) for f, b in functions.items() if f.startswith("fragment")})
        for (state, event), (params, body) in handlers.items():
            text = inline(body, functions)
            gated = bool(gate.search(text))
            found = uses(text, names)
            if not gated and not found:
                continue
            side = ("dialogue fragment" if name.startswith("tif_") else "quest/scene fragment") if fragment else \
                "caused" if event in CAUSED else "uncaused" if event in UNCAUSED else None
            if side is None:
                stats["other events"] += 1
                continue
            kinds = classify(found)
            if gated and IMPLICIT_UI.search(text):
                kinds["ui"] += 1
            key = "gated" if gated else "ungated"
            stats[f"{side} {key} handlers"] += 1
            if not kinds:
                stats[f"{side} {key} identity only"] += 1
            for k in kinds:
                by_kind[side][f"{key}:{k}"] += 1
            body = {"state", "position", "aimed", "teleport", "fx on player", "listens to body"} & set(kinds)
            if body:
                stats[f"{side} {key} needs a body"] += 1
                if body <= {"teleport"}:
                    stats[f"{side} {key} body: teleport only"] += 1
            for call in found:
                calls[kind(call) or "ignored"][(side, call)] += 1
            if event == "ontriggerenter" and gated:
                leave = any(e == "ontriggerleave" and gate.search(inline(b, functions)) for (s, e), (_, b) in events.items())
                triggers.append({
                    "script": name, "state": state,
                    "activates": bool(re.search(r"\.activate\s*\(", text)),
                    "aimed": kinds["aimed"] > 0,
                    "aimed_calls": sorted(c for c in found if kind(c) == "aimed"),
                    "teleport": kinds["teleport"] > 0,
                    "position": kinds["position"] > 0,
                    "ui": kinds["ui"] > 0,
                    "stage_or_scene": bool(re.search(r"setstage|\.start\(\)|forcestart|setobjective", text)),
                    "counts_occupants": "gettriggerobjectcount" in source,
                    "leave_gated": leave,
                    "states": bool(re.search(r"gotostate", text)),
                    "once": bool(re.search(r"gotostate\(\s*\"(done|inactive|dead|finished|off|disabled)\"|\bdisable\(\)|\bdelete\(\)|doonce|startonce|hasbeentriggered|triggered\s*=\s*true", text)),
                })
    json.dump({"stats": stats, "by_kind": by_kind, "triggers": triggers,
               "calls": {k: [[s, c, n] for (s, c), n in v.most_common(60)] for k, v in calls.items()}},
              open(sys.argv[1], "w"), indent=1)


if __name__ == "__main__":
    main()
