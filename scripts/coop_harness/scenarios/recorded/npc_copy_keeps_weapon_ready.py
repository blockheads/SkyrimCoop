"""Wolves appear and fight the players at the host's: each one's copy on the guest's game readies (combat stance) with
its owner and keeps it, never sheathed by the guest's game alone.

docs/re/systems/animation-replay-recovery.md, "Why copies refuse", 4."""
# hand-written: understood from a wolf-fight trace of graph events, 2026-11-09 (the copy's draw state set from its spawn over the newer draw);
# the wolves are new references each boot, which a recording would keep by id.
import re
import time
from datetime import datetime

NAME = 'npc-copy-keeps-weapon-ready'
TAGS = ['combat', 'copies']
DOC = 'Four wolves fight the players; each wolf copy on the guest stays ready (weapon state 3) as its owner, not sheathed.'

WOLVES = 4
DRAWN = 3
WATCH_FRAMES = 600  # about 10 s at 60 fps: the bug sheathed a copy 0.25 s after its replayed draw


def run(ctx):
    pairs = []
    log_file = ctx.plugin_log('client')
    log_start = log_file.stat().st_size
    try:
        for game in ('', 'client'):
            ctx.console_on(game, 'tgm')
        ctx.console_on('', f'player.placeatme 00023ABE {WOLVES}')
        pairs = ctx.wait_for('the wolves and their copies', lambda: (lambda found: found if len(found) >= WOLVES else None)(
            [actor for actor in ctx.nearby_shared() if 'wolf' in actor['name'].lower()]), 20)
        copies = {f"{pair['guest']:X}": f"{pair['host']:X}" for pair in pairs}
        samples = []  # (wall s, {owner: (owner's weapon state, its copy's)})

        def watched():
            host, guest = ctx.worlds()
            samples.append((time.time(), {f"{pair['host']:X}": ((host.get(str(pair['host'])) or {}).get('state', {}).get('weapon_state'),
                                                               (guest.get(str(pair['guest'])) or {}).get('state', {}).get('weapon_state'))
                                          for pair in pairs}))
            return ctx.game_frame('client') > start + WATCH_FRAMES

        start = ctx.game_frame('client')
        ctx.wait_for('the guest shows them a while', watched, 60)
        print('weapon states (owner, copy), first and last:', samples[0][1] if samples else None, samples[-1][1] if samples else None)
        # The guest's convergence sheathing a wolf's copy (InventoryService::RunWeaponDrawConvergence) while its owner
        # reads ready: the guest's knowledge of the owner's hands was stale (its spawn's, over a newer draw).
        wrong = []
        for line in log_file.read_bytes()[log_start:].decode(errors='replace').splitlines():
            found = re.search(r"\[(\d+-\d+-\d+ [\d:.]+)\].*Copy ([0-9A-F]+) hands up \(weapon state \d\) while its owner's are down", line)
            if not found or found.group(2) not in copies:
                continue
            at = datetime.strptime(found.group(1), '%Y-%m-%d %H:%M:%S.%f').timestamp()
            nearest = min(samples, key=lambda sample: abs(sample[0] - at), default=None)
            owner = copies[found.group(2)]
            if nearest and abs(nearest[0] - at) < 2.0 and nearest[1].get(owner, (None,))[0] == DRAWN:
                wrong.append((found.group(2), owner, found.group(1)))
        if wrong:
            raise ctx.failure(f'the guest sheathed wolf copies whose owners were ready (copy, owner, when): {wrong}')
    finally:
        for pair in pairs:
            ctx.console_on('', f"{pair['host']:X}.disable")
