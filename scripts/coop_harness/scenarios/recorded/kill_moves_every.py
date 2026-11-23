"""Every kill move a player's normal swing can pick, played by each player on a restrained host NPC one hit from death:
the pair plays on both games, the two games show its partners alike, the victim drops at its clip's blow and lies
alike, nobody is left in the pair and the attacker walks on.

The table is the idle tree's kill moves per victim, weapon and side, made from the game's ESMs by
scripts/re/kill_moves.py --rows (docs/re/systems/kill-moves.md, "Every kill move"); kill_move_table swings it.
A whole table is about 80 minutes a player: KILL_MOVES_ATTACKERS (host, guest) and KILL_MOVES_VICTIMS (the table's
names, comma separated) run a part of it."""
# hand-written: a table over kill_move_table, the verb the MCP played its rows with (victim by victim)
import os

from ...utilities import log

NAME = 'kill-moves-every'
TAGS = ['copies', 'kill-move', 'combat']
DOC = "Every kill move a player's normal swing can pick, by each player: the pair plays alike on both games, the victim drops at its blow, nobody is left in it."


def run(ctx):
    wrong = []
    try:
        attackers = os.environ.get('KILL_MOVES_ATTACKERS', 'guest,host').split(',')
        victims = [name for name in os.environ.get('KILL_MOVES_VICTIMS', '').split(',') if name]
        for said in attackers:
            table = ctx.kill_move_table('client' if said == 'guest' else '', victims)
            log(f"[{said}] {len(table['played'])} kill moves played; not picked in their tries: {table['missed']}")
            wrong += [f"[{said}] no kill move: {row}" for row in table['unpaired']]
            for event, times in sorted(table['played'].items()):
                if bad := [problems for problems in times if problems]:
                    wrong.append(f"[{said}] {event} wrong {len(bad)} of {len(times)}: {bad[0]}")
        if wrong:
            raise ctx.failure(f"{len(wrong)} kill moves wrong: " + " | ".join(wrong))
    finally:
        ctx.unstage()
