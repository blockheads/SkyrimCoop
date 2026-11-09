"""Wolves appear and fight the players at the host's: each one's copy on the guest's game readies (combat stance) with
its owner and keeps it, never sheathed by the guest's game alone.

docs/re/systems/animation-replay-recovery.md, "Why copies refuse", 4."""
# hand-written: understood from a wolf-fight trace of graph events, 2026-11-09 (the copy's draw state set from its spawn
# over the newer draw); the wolves are new references each boot, which a recording would keep by id.
import re
import time
from datetime import datetime

NAME = 'npc-copy-keeps-weapon-ready'
TAGS = ['combat', 'copies']
DOC = 'Four wolves fight the players; each wolf copy on the guest readies with its owner and stays ready, not sheathed.'

WOLVES = 4
DRAWN, SHEATHED = 3, 0
WATCH_FRAMES = 600   # about 10 s at 60 fps
MOST_APART = 1.0     # s a copy may stay sheathed while its owner is ready: its replayed draw comes its delay later


def run(ctx):
    pairs, wolves = [], []
    log_file = ctx.plugin_log('client')
    log_start = log_file.stat().st_size
    try:
        for game in ('', 'client'):
            ctx.console_on(game, 'tgm')
        before = set(ctx.worlds()[0])
        ctx.console_on('', f'player.placeatme 00023ABE {WOLVES}')
        # Into combat at once, so each draws right after its spawn goes out, while its copy may still be loading.
        wolves = ctx.wait_for('the host places the wolves', lambda: (lambda found: found if len(found) >= WOLVES else None)(
            [int(form) for form, row in ctx.worlds()[0].items()
             if form not in before and 'wolf' in (row['identity'].get('name') or '').lower()]), 10)
        for wolf in wolves:
            ctx.console_on('', f'{wolf:X}.startcombat 14')
        pairs = ctx.wait_for('the wolves and their copies', lambda: (lambda found: found if len(found) >= WOLVES else None)(
            [actor for actor in ctx.nearby_shared() if actor['host'] in wolves]), 20)
        copies = {f"{pair['guest']:X}": f"{pair['host']:X}" for pair in pairs}
        samples, apart, since, both_ready = [], {}, {}, set()

        def watched():
            now = time.time()
            host, guest = ctx.worlds()
            sample = {}
            for pair in pairs:
                owner = (host.get(str(pair['host'])) or {}).get('state', {}).get('weapon_state')
                copy = (guest.get(str(pair['guest'])) or {}).get('state', {}).get('weapon_state')
                wolf = f"{pair['host']:X}"
                sample[wolf] = (owner, copy)
                if owner == DRAWN and copy == DRAWN:
                    both_ready.add(wolf)
                if owner == DRAWN and copy == SHEATHED:
                    since.setdefault(wolf, now)
                    apart[wolf] = max(apart.get(wolf, 0.0), now - since[wolf])
                else:
                    since.pop(wolf, None)
            samples.append((now, sample))
            return ctx.game_frame('client') > start + WATCH_FRAMES

        start = ctx.game_frame('client')
        ctx.wait_for('the guest shows the fight a while', watched, 60)
        print('both ready:', sorted(both_ready), 'longest copy sheathed while its owner is ready (s):',
              {wolf: round(t, 2) for wolf, t in apart.items()})
        if not both_ready:
            raise ctx.failure(f'no wolf and its copy were seen ready together (owner, copy): {samples[-1][1] if samples else None}')
        stuck = {wolf: round(t, 2) for wolf, t in apart.items() if t > MOST_APART}
        if stuck:
            raise ctx.failure(f'copies stayed sheathed while their owners were ready (s): {stuck}')
        # Also the guest's convergence sheathing a wolf's copy while its owner reads ready (InventoryService).
        wrong = []
        for line in log_file.read_bytes()[log_start:].decode(errors='replace').splitlines():
            found = re.search(r"\[(\d+-\d+-\d+ [\d:.]+)\].*Copy ([0-9A-F]+) hands up \(weapon state \d\) while its owner's are down", line)
            if not found or found.group(2) not in copies:
                continue
            at = datetime.strptime(found.group(1), '%Y-%m-%d %H:%M:%S.%f').timestamp()
            nearest = min(samples, key=lambda sample: abs(sample[0] - at), default=None)
            if nearest and abs(nearest[0] - at) < 2.0 and nearest[1].get(copies[found.group(2)], (None,))[0] == DRAWN:
                wrong.append((found.group(2), copies[found.group(2)], found.group(1)))
        if wrong:
            raise ctx.failure(f'the guest sheathed wolf copies whose owners were ready (copy, owner, when): {wrong}')
    finally:
        for wolf in wolves:
            ctx.console_on('', f'{wolf:X}.disable')
