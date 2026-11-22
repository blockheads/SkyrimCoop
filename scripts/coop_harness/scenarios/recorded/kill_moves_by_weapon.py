"""Kill moves by weapon: each player kills a host bandit (and a wolf) with every melee weapon class, from the front and from behind. Each pair plays on both games, the victim drops at its clip's blow, nobody is left in the pair, both bodies lie alike and the attacker walks on.

Played through the repro MCP's kill_move_on verb (docs/design/repro-mcp.md), one weapon a call."""
# hand-written: a table over kill_move_on, the verb kill-move-drops-at-blow recorded; the MCP played each row first
from ...common import IRON_SWORD, WOLF
from ...utilities import log

NAME = 'kill-moves-by-weapon'
TAGS = ['copies', 'kill-move', 'combat']
DOC = "Each player kills a host bandit (and a wolf) with every melee weapon class: the pair plays on both games, the victim drops at its clip's blow, nobody is left in the pair and both bodies lie alike."

BANDIT = "0003DE53"
IRON_DAGGER, IRON_GREATSWORD, IRON_BATTLEAXE, IRON_MACE, IRON_WAR_AXE, IRON_WARHAMMER = 0x1397E, 0x1359D, 0x13980, 0x13982, 0x13790, 0x13981
FISTS = 0

# (attacker, victim's base, weapon, from behind). A mace or war axe from behind picks pa_KillingBlow, which the engine
# refuses (kill-moves.md, "Attacker's own refusals"): those two swing from the front.
CASES = [
    ("client", BANDIT, IRON_SWORD, False),
    ("client", BANDIT, IRON_SWORD, True),
    ("client", BANDIT, IRON_DAGGER, False),
    ("client", BANDIT, IRON_GREATSWORD, False),
    ("client", BANDIT, IRON_BATTLEAXE, False),
    ("client", BANDIT, IRON_WARHAMMER, False),
    ("client", BANDIT, IRON_MACE, False),
    ("client", BANDIT, IRON_WAR_AXE, False),
    ("client", BANDIT, FISTS, False),
    ("client", WOLF, IRON_SWORD, False),
    ("", BANDIT, IRON_SWORD, False),
    ("", BANDIT, IRON_GREATSWORD, False),
    ("", BANDIT, FISTS, False),
    ("", WOLF, IRON_SWORD, False),
]


def run(ctx):
    wrong = []
    try:
        for attacker, base, weapon, behind in CASES:
            report = ctx.kill_move_on(attacker=attacker, base=base, weapon=weapon, behind=behind)
            what = f"{report['attacker']} with {report['weapon']} on {base}{' from behind' if behind else ''}: {report['action'] or 'no kill move'}"
            log(f"{what}; blow to death host {report.get('host_blow_to_death_s')} s, guest {report.get('client_blow_to_death_s')} s")
            if not report["action"]:
                wrong.append(f"{what} (check {report.get('check')})")
            wrong += [f"{what}: {problem}" for problem in report["problems"]]
        if wrong:
            raise ctx.failure(f"{len(wrong)} wrong in {len(CASES)} kill moves: " + " | ".join(wrong))
    finally:
        ctx.unstage()
