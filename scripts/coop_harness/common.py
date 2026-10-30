"""Paths, form ids, tolerances and other immutable harness configuration."""
import os
from pathlib import Path
import re
import time

CLI_HELP = """Co-op game tests and diagnostics.

Use scenario --dry-run or coop_specs.py --dry to discover flows without games.
Use duo --keep-running, then scenario <names>, to run targeted Skyrim flows.
See docs/design/testing.md for fixtures, ownership, selection and reporting.
"""

ROOT = Path(__file__).resolve().parents[2]
STEAM_LIBRARY = Path("/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d1/SteamLibrary/steamapps/common")
GAME = Path(os.environ.get("SKYRIM_GAME_DIR",
                           "/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"))
PROTON = Path(os.environ.get("SKYRIM_PROTON_DIR", STEAM_LIBRARY / "Proton 10.0"))
RUNTIME = Path(os.environ.get("SKYRIM_RUNTIME_DIR", STEAM_LIBRARY / "SteamLinuxRuntime_sniper"))
STEAM = Path(os.environ.get("STEAM_COMPAT_CLIENT_INSTALL_PATH", Path.home() / ".steam/steam"))
CLIENT_USER = os.environ.get("COOP_CLIENT_USER", "")
TEST_HOME = (ROOT / "build/coop-test").resolve()
DLL = Path(os.environ.get("COOP_DLL", ROOT / "build/windows/x64/dev/SkyrimTogetherClient.dll"))
LOADER = DLL.with_name("SkyrimCoopLoader.dll")
VANILLA_HARNESS = DLL.with_name("SkyrimCoopVanillaHarness.dll")
PEER = ROOT / "build/linux/x86_64/debug/coop_peer"
PLUGINS = GAME / "Data/SKSE/Plugins"
START = time.monotonic()
CLIENT_NETNS, HOST_SIDE_ADDRESS, CLIENT_ADDRESS = "coop2", "10.77.0.1", "10.77.0.2"
UI_SOURCE = ROOT / "Code/client/Ui/assets"
CRASH_LOGGER_VERSION = "1.25.0"
CRASH_LOGGER_SHA256 = "ce8592d60a2394bc05874d4cc759d3513686c3e26f841fe831a98c9d66fa979c"
CRASH_LOGGER_FILES = ("CrashLogger.dll", "CrashLogger.pdb", "msdia140.dll")
CRASH_LOGGER_INI = """[Debug]
Auto Open Crash Log = false
Auto Upload Crash Log = false
Crash Log Write Minidump = false
Max Crash Logs = 50
"""
DUMMY_XORG_CONFIG = "xorg-coop-dummy.conf"
BUNDLED_START = "SkyrimCoopStart.esp"
CONSOLE_ERRORS = re.compile(r"not found|Syntax error|Missing parameter|Compiled script not saved|not run|cannot be moved", re.IGNORECASE)
CONSOLE_ON_REFERENCE = re.compile(r"^([0-9A-Fa-f]{1,8})\.(\S.*)$")
GAME_MEMORY_LIMIT_MB = 6000
IRON_SWORD = 0x00012EB7
FIRE_DAMAGE_ENCHANTMENT = 0x49BB7
FLAMES = 0x00012FCD
FIREBOLT = 0x00012FD0
FIREBALL = 0x0001C789
RIGHT_HAND = 1
LYDIA = "000A2C8E"
WOLF = "00023ABE"
CHICKEN = "000FA6D9"
MELEE_GAP = 90
SPELL_GAP = 250
WEAPON_SHEATHED, WEAPON_DRAWN = 0, 3
CASTER_CHARGED = 3
ON_GROUND = 0
CONCENTRATION_SPELLS = {FLAMES}
STAGE_DEPTH = 450
TRACES = TEST_HOME / "traces"
TRACE_NOISE = re.compile(r"^(Foot|Sound|tail|HeadTracking|PitchOverride|IdleOffset|EnableBumper|DisableBumper|CastOK)")
LOG_LINE = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\] \[(\w+)\] (.*)$")
REFUSED_REPLAY = re.compile(r"Copy ([0-9A-F]+) refused replayed action ([0-9A-F]+) '([^']*)'")
# A copy's menu may trail its host's this long (the line's arrival, the list sent after it).
DIALOGUE_GRACE = 1.0
# Not the line said: a guest's copy voices it through the relay, its own manager says none (always 0 there).
DIALOGUE_FIELDS = ("shown", "options", "cursor")


def log_time(stamp):
    """A plugin log line's time (LOG_LINE's first group) as wall-clock seconds."""
    return time.mktime(time.strptime(stamp[:19], "%Y-%m-%d %H:%M:%S")) + int(stamp[20:]) / 1000
TEST_PLUGIN = "SkyrimCoopTest.esp"
FRIENDS_COUNT_RIFT = 0x87E1C
LUTE_QUEST, CR03_QUEST = 0xD93FA, 0x25230
SYNCED_QUEST_FLAGS = 0xC3
BANNERED_MARE = "WhiterunBanneredMare"
STORY_NODE_PASSED, STORY_QUEST_STARTED = 3, 8
SALT_PILE, RAW_BEEF, COOKED_BEEF = 0x34CDF, 0x65C99, 0x721E8
CRAFTING_LISTS = "_root.Menu.InventoryLists"
CRAFTING_LIST = "_root.Menu.ItemList"
CREATE_YES = 0
CRAFT_RAISED = re.compile(r"HostEngine: story event (\d+) \(CRFT\) raised for ([0-9A-F]+)")
CRITTER_SPAWNER = 0x6B45C
VALDR, VALDR_QUEST = 0x411BA, 0x376EE
STAGE_ON_DEATH = "defaultSetStageOnDeath"
QUICKSAVE_KEY = 0x3F
SAVE_REFUSAL = "Only the host saves: your game can't save while you're in a co-op session."
QUICKLOAD_KEY = 0x43
LOAD_REFUSAL = "Only the host loads saves: your game can't load while you're in a co-op session."
JOURNAL_ROOT = "_root.QuestJournalFader.Menu_mc"
JOURNAL_SYSTEM = JOURNAL_ROOT + ".SystemFader.Page_mc."
GOLD_ITEM, ARMSMAN, FIREBOLT_SPELL = "0:F", "0:BABE4", "0:12FD0"
MAIN_LIST = "_root.MenuHolder.Menu_mc.MainList"
MAIN_MENU_STATE = "_root.MenuHolder.Menu_mc.strCurrentState"
RACE_MENU, RACE_PANELS = "RaceSex Menu", "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance"
BODY_CATEGORY, HEAD_CATEGORY, FACE_CATEGORY, EYES_CATEGORY, HAIR_CATEGORY = 1, 2, 3, 4, 7
WAR_PAINT_TINT = 7
NPC_FACE = 0x800
RACE_MENU_DONE_KEY = 0x13
KHAJIIT_RACE = 0x13745
NEW_CHARACTER_NAME = "Rinara"
BODY_TAB = (782, 36)
FINISH_NAMING_OK = (587, 389)
NAME_ACCEPT = (580, 316)
JOIN_BASE_FLAGS = 0x03000E36
NPC_SKILLS = 0x200
NPC_RACE = 0x2000000
NORD_VAMPIRE_RACE = 0x88794
EMBERSHARD_MINE, EMBERSHARD_REFS = 0xB6BE6, range(0x10A7C2, 0x10A7CD)
CR03_SPAWN_ALIAS = 5
DO_NOTHING_PACKAGE = 27
TAB_KEY = 0x0F
ACTIVATE_KEY = 0x12
UP_KEY, DOWN_KEY = 0x11, 0x1F
HULDA = "00013BA3"
BRENUIN = "00013BA7"
GOLD = 0xF
GIVE_COIN_TOPIC = 0x6001F
GIVE_COIN_INFO = 0x60022
RENT_ROOM_TOPIC = 0x9CC92
PLAYER_BASE = 0x7
RENT_ROOM_NO_GOLD_INFO = 0x9CC95
CHARACTER_LAYER = 30
CAMERA_STATES = ["first person", "auto vanity", "VATS", "free", "iron sights", "furniture", "transition", "tween",
                 "animated", "third person", "mount", "bleedout", "dragon"]
FOLLOWER_QUEST = 0x750BA
LUCAN = 0x13487
GOLDEN_CLAW = 0x39647
CLAW_TOPIC = 0x5D46F
NO_CLAW_LINE = 0x8AFA2
GOLDEN_CLAW_QUEST = 0x39645
MAGELIGHT = 0x43323
STEED_ABILITY, CARRY_WEIGHT = 0xE5F5E, 32
CLAW_FOUND_LINES = {0x5D471, 0x8AFA3, 0x8AFA4, 0x8AFA5, 0x8AFA6}
LUCAN_TROUBLE_TOPIC = 0x39621
BEGGARS_QUEST = "FavorJobsBeggars"
GIFT_OF_CHARITY = 0xEEF58
SPEECH_SKILL = 17
UNGRIEN = "0001337E"
UNGRIEN_TOPIC = 0x369D2
UNGRIEN_PERSUADE_TOPIC = 0x369D1
PERSUADE_ACHIEVEMENT_STATS = ("Bribes", "Intimidations")
ANDURS = "00013BA8"
ANDURS_KNOW_ARKAY = 0xD74C1
ANDURS_WHAT_LOST = 0x94E32
ANDURS_AMULET = {0xD3D5D, 0xD3D5C}
HULDA_COLLEGE_TOPIC = 0x1F80E
HULDA_COLLEGE_LINES = (0x8EC38, 0x8EC39)
ONMUND = "0001C194"
ONMUND_FEW_NORDS = 0x8B58D
ONMUND_NOT_A_NORD = 0x66963
ONMUND_NOT_A_NORD_LINE = 0x6696E
ONMUND_GREETINGS = (0x66973, 0x66974)
NORD_RACE = 0x13746
AMREN = "00013BAA"
TRAINING_TOPIC = 0xE3A35
ONE_HANDED_SKILL = 6
TRAINING_MENU = "Training Menu"
CLEAR_WEATHER, OTHER_WEATHER = 0x81A, 0x10E1F1
OUTDOOR_CELL, HOME_CELL = "Riverwood", "QASmoke"
INN = "RiverwoodSleepingGiantInn"
RIVER_CELL, RIVER_SPOT, RIVER_BANK = "cow Tamriel 7 -8", (30592, -29696, -1300), (30592, -29200, -1100)
DIVE_SECONDS = 6
SURFACE_PACKAGE = 29
INN_DOOR_INSIDE, INN_DOOR_OUTSIDE = 0x13419, 0x13424
HORSE = "00109E3D"
ACTIVATE_KEY = 0x12
COOKING_SPIT = 0x101973
IN_FURNITURE = 3
UNRELENTING_FORCE = 0x13E07
UNRELENTING_FORCE_THREE = 0x13F3A
UNRELENTING_FORCE_WORDS = (0x13E22, 0x13E23, 0x13E24)
KILL_MOVE_RANDOM = 50
KILL_MOVE_LAG = 0.6
PAIR_TOLERANCE = 15
BLOW_TOLERANCE = 0.35
OAKFLESH = 0x5AD5C
CHARITY_SHADER = 0x4E220
DOOR_OPEN, DOOR_OPENING, DOOR_CLOSED, DOOR_CLOSING = 1, 2, 3, 4
KOLBJORN_CELL, KOLBJORN_PLATE, PLATE_SCRIPT = "DLC2KolbjornBarrow01", 0x0401DC12, "DLC2dunKolbjornPressureTileScript"
REBELS_CAIRN_CELL, SWORD_BOX, SWORD_IN_STONE, SECRET_DOOR, RED_EAGLE_SWORD = "RebelsCairn01", 0xA05C0, 0xA05AC, 0xC149D, 0x9FD50
HALLDIR_STAFF = 0xAB704
SOLJUND_CELL, SOLJUND_TRIGGER, SOLJUND_SCRIPT = "SoljundsSinkhole01", 0x6AEBE, "dunSoljundsBossSCRIPT"
SOLJUND_FIRE, SOLJUND_FIRE_DAMAGE = 0x12FD0, 0x12F03
CLAW_DOOR_CELL, CLAW_KEYHOLE, CLAW = "E3demoBleakFallsBarrow", 0x847D6, 0x999E7
CLAW_RINGS = {0x847D3: "position01", 0x847D4: "position03", 0x847D5: "position02"}
VOLUNRUUD_CELL, VOLUNRUUD_DOOR, VOLUNRUUD_SCRIPT = "Volunruud01", 0xCDDA4, "dunVolunruudDoorSCRIPT"
VOLUNRUUD_QUEST, VOLUNRUUD_QUEST_ID = "dunVolunruudQST", 0x8ADFA
VOLUNRUUD_AXE, VOLUNRUUD_AXE_SHOWN = 0x8ADFB, 0x8FFDB
MESSAGE_BOX_OK = (636, 389)
QA_SHOUT_BUTTON, QA_SHOUT_SCRIPT = 0x0402A891, "DLC2QALearnShouts"
QA_SHOUT_WORDS = [0x040179D9, 0x040179DA, 0x040179DB, 0x040200C2, 0x040200C3, 0x040200C4,
                  0x0401DF93, 0x0401DF94, 0x0401DF95, 0x040200E4, 0x040200E5, 0x040200E6]
WORD_FUS, WORD_RO = 0x13E22, 0x13E23
DRAGON_SOULS = 133
ENC_DRAGON = 0x1CA03
HARBINGER_FACTION = 0x1070DD
PLAYER_WEREWOLF_FACTION = 0x91822
SANGUINARE_VAMPIRIS = 0x020037E9
PLAYER_VAMPIRE_QUEST = 0xEAFD5
AB_VAMPIRE_01 = 0x2E1C3
PLAYER_IS_VAMPIRE = 0xED06D
VC01 = 0x5C625
ALL_CONTROLS = 0xFFFFFFFF
FADE_TO_BLACK_IMOD = 0xF756D
TAKE_CONTROLS = [True, True, False, False, False, True, True, False, 0]
GIVE_CONTROLS = [True] * 8 + [0]
MONK_SCENE_QUEST = 0x76F39
MONK_INTRO_IMOD, MONK_LOOP_IMOD, MONK_OUTRO_IMOD = 0xEF960, 0xEF95F, 0xEF961
MQ206, MQ105 = 0x36193, 0x242BA
STONE_MESSAGE, STONE_CANCEL = 0x16869, 1
SHRINE_CELL = "SolitudeTempleoftheDivines"
SHRINES = {"": (0xD9880, 0xFB995), "client": (0xD9882, 0xFB988)}
VILKAS, C00_QUEST, VILKAS_TRAINING, VILKAS_HATE = 0x1A695, 0x4B2D9, 0xA3EBC, 0x10F5A0
SANDBOX_256, SANDBOX_1024 = 0x956B8, 0xBFB6B
POTENTIAL_MARRIAGE = 0x19809
SMALL_CHEST, SWEETROLL, TAKE_ALL_KEY = 0xC2CE8, 0x64B3D, 0x13
ITEM_LISTS = "_root.Menu_mc.InventoryLists_mc"
LISTS_SHOWN = (1, 2)
HEIRLOOM = {"item": IRON_SWORD, "count": 1, "health": 1.4, "enchantment": FIRE_DAMAGE_ENCHANTMENT,
            "enchant_charge": 1000, "charge": 800.0, "name": "Old Friend"}
ALCHEMY_LAB = 0x10196B
BLUE_MOUNTAIN_FLOWER, WHEAT = 0x77E1C, 0x4B0BA
DEATHBELL, NIGHTSHADE = 0x516C8, 0x2F44C
INGREDIENT_NAMES = {BLUE_MOUNTAIN_FLOWER: "Blue Mountain Flower", WHEAT: "Wheat", DEATHBELL: "Deathbell", NIGHTSHADE: "Nightshade"}
CRAFT_KEY = 0x13
POISON_YES = 0
REFR_LOCK = 1 << 12
BEAST_FORM, WEREWOLF_CHANGE_EFFECT, WEREWOLF_BEAST_RACE, PLAYER_WEREWOLF_QUEST = 0x92C48, 0x92C45, 0xCDD84, 0x2BA16
WORLD_FIELDS = ("base", "cell", "flags", "position", "rotation", "scale", "has_3d", "hidden", "position_3d",
                "lock", "open_state", "owner", "items", "changes", "pose", "collision", "graph", "effects")
WORLD_TOLERANCES = {"position": 1.0, "position_3d": 1.0, "rotation": 0.01, "scale": 0.001}
POSE_TOLERANCE, ROTATE_TOLERANCE, BODY_TOLERANCE, BODY_TURN_TOLERANCE = 1.0, 0.01, 2.0, 0.02
CREATED_ID = 0xFF000000
FORM_DISABLED = 0x800
FORM_DELETED = 0x20
FORM_PERSISTENT = 0x400
FORM_ALTERED = 0x2
FORM_HEADING_TARGET = 0x40000
FORM_NOT_SYNCED = FORM_ALTERED | FORM_HEADING_TARGET | FORM_PERSISTENT
CHANGES_FORM_FLAGS = 0x1
CHANGES_OWN = 0x10000000 | 0x2000000 | 0x80000000
CHANGES_MOVED, CHANGES_HAVOK_MOVED = 0x2, 0x4
IDLE_SAMPLES, IDLE_INTERVAL = 3, 0.3
WORLD_WINDOW = 3.0
WORLD_CHANGES = [
    ("disable", ("STAT", 0), [], "{r}.disable", ["{r}.enable"]),
    ("enable", ("STAT", 1), ["{r}.disable"], "{r}.enable", []),
    # Statics cannot be moved (the console's SetPos and SetAngle leave them).
    ("move", ("ACTI", 0), [], "{r}.setpos z {z_up}", ["{r}.setpos z {z}"]),
    # The console's SetAngle leaves the 3D's world transform until something updates it: a SetPos in place does.
    ("rotate", ("ACTI", 1), [], ["{r}.setangle z {turned}", "{r}.setpos z {z}"], ["{r}.setangle z {angle}", "{r}.setpos z {z}"]),
    ("scale", ("STAT", 2), [], "{r}.setscale 2", ["{r}.setscale {scale}"]),
    # At its own level: the console's Lock reads level 0 as none given, so no level could be put back.
    ("lock", ("DOOR", 0, lambda r: not r.get("lock", {}).get("locked")), [], "{r}.lock {level}", ["{r}.unlock"]),
    ("open", ("DOOR", 0), [], "{r}.setopenstate 1", ["{r}.setopenstate 0"]),
    ("add items", ("CONT", 0), [], f"{{r}}.additem {GOLD:X} 7", [f"{{r}}.removeitem {GOLD:X} 7"]),
    # No console line clears an owner: left owned (the client follows), each run takes the next unowned container.
    ("own", ("CONT", 1, lambda r: not r.get("owner")), [], "{r}.setownership", []),
]
OBJECT_EVENTS = {"activate", "lock_changed", "open", "close", "cell_attach", "cell_detach", "loaded", "unloaded"}
DYNAMIC_MOTIONS = {1, 2, 3, 6}
KEYFRAMED_MOTION = 4
BONE_ON_BODY = 15.0
BONE_APART = 10.0
SINKHOLE = "SoljundsSinkhole01"
BASE_PICKED = 0x80
COMPANIONS_HOUSEKEEPING = 0x4B2D9
WEREWOLF_TRANSFORM_VISUAL = 0xF8209
ENTER_KEY, ESCAPE_KEY, SHIFT_KEY = 0x1C, 0x01, 0x2A
TYPED_KEYS = {**{c: (code, False) for c, code in zip("qwertyuiop", range(0x10, 0x1A))},
              **{c: (code, False) for c, code in zip("asdfghjkl", range(0x1E, 0x27))},
              **{c: (code, False) for c, code in zip("zxcvbnm", range(0x2C, 0x33))},
              **{c: (code, False) for c, code in zip("1234567890", range(0x02, 0x0C))},
              " ": (0x39, False), ",": (0x33, False), ".": (0x34, False), "'": (0x28, False),
              "!": (0x02, True), "?": (0x35, True), "/": (0x35, False)}
QUICK_MAP_KEY, PLACE_MARKER_KEY, TWEEN_MENU_KEY = 0x32, 0x19, 0x0F
LEAVE_IT_BUTTON, REMOVE_IT_BUTTON = (630, 393), (746, 393)
WAIT_KEY, MENU_RIGHT_KEY, MENU_ACCEPT_KEY = 0x14, 0x20, 0x12
HOME_FILE = "home.json"
TEST_ITEMS = {IRON_SWORD}
HOME_TOLERANCE = 40.0
PLAYER_VALUES = ("health", "magicka", "stamina")
POSITION_TOLERANCE = 50.0
ANGLE_TOLERANCE = 0.35
VALUE_TOLERANCE = 1.0
POSE_TOLERANCE = 20.0
FLOAT_TOLERANCE = 15.0
RAISED_PELVIS = 85.0
LOW_PELVIS = 40.0
DESYNC_WINDOW = 0.8
DESYNC_INTERVAL = 0.1
LOCK_PATTERN = re.compile(r"^(\[ours\] )?(lock order inversion|self-deadlock) \[([^\]]*)\]")
KNOWN_LOCK_PATTERNS = ROOT / "scripts/lock_order_known.txt"
FORM_TYPE_NAMES = dict(enumerate(
    "NONE TES4 GRUP GMST KYWD LCRT AACT TXST MICN GLOB CLAS FACT HDPT EYES RACE SOUN ASPC SKIL MGEF SCPT LTEX ENCH "
    "SPEL SCRL ACTI TACT ARMO BOOK CONT DOOR INGR LIGH MISC APPA STAT SCOL MSTT GRAS TREE FLOR FURN WEAP AMMO NPC_ "
    "LVLN KEYM ALCH IDLM NOTE COBJ PROJ HAZD SLGM LVLI WTHR CLMT SPGD RFCT REGN NAVI CELL REFR ACHR PMIS PARW PGRE "
    "PBEA PFLA PCON PBAR PHZD WRLD LAND NAVM TLOD DIAL INFO QUST IDLE PACK CSTY LSCR LVSP ANIO WATR EFSH TOFT EXPL "
    "DEBR IMGS IMAD FLST PERK BPTD ADDN AVIF CAMS CPTH VTYP MATT IPCT IPDS ARMA ECZN LCTN MESG RGDL DOBJ LGTM MUSC "
    "FSTP FSTS SMBN SMQN SMEN DLBR MUST DLVW WOOP SHOU EQUP RELA SCEN ASTP OTFT ARTO MATO MOVT SNDR DUAL SNCT SOPM "
    "COLL CLFM REVB LENS LSPR VOLI".split()))
PLUGIN_LOADED = re.compile(r"\] Loaded \d+ resources$")
UNSYNCED_LINE = re.compile(r"^\[([^\]]+)\].*Reference sync: change flags ([0-9A-F]+) of ([0-9A-F]+) have no row")
REPROS = {
    "qasmoke-exit": ("QASmoke", ["player.placeatme 000A2C8E", "coc Riverwood"]),
    # The same with the console open, as when typed (vanilla does not crash that way).
    "qasmoke-exit-console": ("QASmoke", ["player.placeatme 000A2C8E", "@show Console", "coc Riverwood"]),
}
CRASH_REGISTER = re.compile(r"^\t(R[A-Z0-9]+)\s+0x([0-9A-F]+)\s")
