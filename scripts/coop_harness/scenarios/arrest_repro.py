"""The playtest's Ivarstead theft, guard choice and travel, through native player input."""
import contextlib
import json
import subprocess
import time
import uuid

from ..common import TEST_HOME, WINDOWS
from ..utilities import log
from . import crime, dialogue_mouse as mouse

INN = 0x13A5C
RIFT = 0x2816B
GOLD = 0x9E63D
THEFTS = [(GOLD, 0xF), (0x9E63A, 0xF), (0x9E5C2, 0x65C97), (0x9E572, 0x65C9D)]
GUARD = 0x2123C
FINE = 0x267DB
JAIL = 0x267DD
LIST = '_root.DialogueMenu_mc.TopicList.'


def capture(ctx, folder, label, **values):
    data = {'time': time.time(), **values, 'games': {}}
    for game in ctx.games:
        status = game.read_status() or {}
        data['games'][game.name or 'host'] = {
            'frames': status.get('frames'), 'player': status.get('player'), 'game': status.get('game')}
        with contextlib.suppress(Exception):
            data['games'][game.name or 'host']['dialogue'] = ctx.dialogue(game.name)
        with contextlib.suppress(Exception):
            data['games'][game.name or 'host']['bounty'] = crime.bounty(ctx, game.name)
    (folder / f'{label}.json').write_text(json.dumps(data, indent=2) + '\n')
    if not WINDOWS:
        subprocess.run(['import', '-display', ctx.guest.display, '-window', 'root',
                        str(folder / f'{label}.png')], capture_output=True)
    log(f'S3 evidence: {folder / label}')


def pick_mouse(ctx, topic, folder, rapid=False):
    instance = 'client'
    ctx.wait_for('the guard menu accepts mouse input', lambda: ctx.menu_takes_pick(instance), 10)
    options = ctx.dialogue(instance)['options']
    target = next((i for i, option in enumerate(options) if option['topic'] == topic), None)
    if target is None:
        raise ctx.failure(f'the guard menu does not list {topic:X}: {options}')
    value = lambda suffix: ctx.menu_var(instance, 'Dialogue Menu', LIST + suffix)
    scale = mouse.calibrate(ctx, instance)
    sequence = [2, 0, 1, target] if rapid else [target]
    hovered = [mouse.hover(ctx, instance, index, scale, settle=not rapid) for index in sequence]
    ctx.wait_for('the final hovered guard choice is selected', lambda: ctx.cursor_index(instance) == target, 5)
    selected = ctx.dialogue(instance)
    text = value('selectedEntry.text')
    capture(ctx, folder, 'before-click', requested=options[target], selected=selected,
            rendered_text=text, mouse_scale=scale, hovers=hovered)
    if selected['swf']['cursor'] != topic or text != options[target]['text']:
        raise ctx.failure(f'guard highlight/text mismatch before click: wanted {options[target]}, visible {text}, state {selected}')
    log(f'Guest mouse selects {topic:X}: {text}')
    ctx.click(instance)


def ivarstead(ctx, outcome='fine', rapid=False):
    """A guest takes the reported Ivarstead objects, then uses the guard's mouse fine/jail choice."""
    folder = TEST_HOME / 's3-repro' / (time.strftime('%Y%m%d-%H%M%S') + '-' + outcome + ('-sweep' if rapid else '-mouse'))
    folder.mkdir(parents=True)
    name = 'S3Arrest' + uuid.uuid4().hex[:10]
    home = ctx.player_cell('')
    checkpoint = ctx.save_game('', name)
    ctx.wait_for('the reproduction checkpoint is written', ctx.saves_written(checkpoint), 15)
    completed = False
    try:
        ctx.both_go('IvarsteadVilemyrInn')
        if ctx.player_cell('') != INN:
            raise ctx.failure('the party did not enter the reported inn')
        crime.restore(ctx, {})
        if ctx.party_gold() < 100:
            ctx.set_party_gold(100)
        ctx.console_on('', 'player.setav invisibility 1')
        ctx.place_before('client', GOLD, 90)
        guest_copy = ctx.player_copy('')
        guard = GUARD
        ctx.console_on('', f'{guard:X}.enable')
        ctx.console_on('', f'{guard:X}.moveto {guest_copy:X}')
        guard_sid = ctx.wait_for('the Ivarstead guard is synchronized',
                                lambda: (sid := ctx.host_npc_state(guard).get('session_id')) is not None and sid >= 0 and (sid,), 10)[0]
        copy = ctx.copy_of('client', guard_sid)
        ctx.console_on('', f'{guard:X}.setpos x {ctx.local_player("client")["position"][0] + 100}')
        crime.face_guest(ctx, guard)
        ctx.wait_for('the Rift guard sees the guest stealing',
                     lambda: crime.crime(ctx, action='detection', actor=guard, target=guest_copy)['level'] > 0, 10)
        for ref, item in THEFTS:
            if ctx.talking_to('', guard):
                break
            ctx.place_before('client', ref, 90)
            ctx.console_on('', f'{guard:X}.moveto {guest_copy:X}')
            ctx.console_on('', f'{guard:X}.setpos x {ctx.local_player("client")["position"][0] + 100}')
            crime.face_guest(ctx, guard)
            ctx.wait_for(f'the guard sees the guest at {ref:X}',
                         lambda: crime.crime(ctx, action='detection', actor=guard, target=guest_copy)['level'] > 0, 10)
            count = ctx.item_count('client', 0x14, item)
            ctx.activate_on('client', ref)
            ctx.wait_for(f'the guest picks up {ref:X}',
                         lambda: ctx.item_count('client', 0x14, item) > count, 10)
            capture(ctx, folder, f'after-theft-{ref:X}',
                    guard=crime.crime(ctx, action='reaction', actor=guard, target=guest_copy))
        ctx.wait_for('the theft adds a Rift bounty', lambda: sum(crime.bounty(ctx).get(RIFT, (0, 0))) > 0, 10)
        amount = sum(crime.bounty(ctx)[RIFT])
        ctx.wait_for('the guard naturally arrests the party', lambda: ctx.talking_to('', guard), 20)
        ctx.wait_for('the guest receives that arrest dialogue', lambda: ctx.talking_to('client', copy), 10)
        ctx.client_shows_hosts_list('for the Ivarstead arrest')
        ctx.wait_for('the guard finishes the arrest greeting', lambda: ctx.line_over('', guard), 30)
        purse = ctx.party_gold()
        pick_mouse(ctx, FINE if outcome == 'fine' else JAIL, folder, rapid)
        ctx.wait_for('the native fine or sentence moves the host out of the inn',
                     lambda: ctx.player_cell('') not in (None, INN), 30)
        destination = ctx.player_cell('')
        ctx.wait_for('the guest finishes following the host',
                     lambda: ctx.player_cell('client') == destination and ctx.both_see_each_other()
                     and all(ctx.takes_input(g.name) for g in ctx.games), 30)
        sentence = crime.crime(ctx, action='jail_state')
        capture(ctx, folder, 'arrived', destination=destination, sentence=sentence,
                bounty_before=amount, gold_before=purse, gold_after=ctx.party_gold())
        if outcome == 'fine':
            if destination != 0x42245 or sentence['sentence']:
                raise ctx.failure(f'the fine took the wrong route: cell {destination:X}, {sentence}')
            crime.shared(ctx, RIFT, (0, 0))
        elif sentence['faction'] != RIFT or sentence['sentence'] <= 0:
            raise ctx.failure(f'the jail choice did not impose a Rift sentence: {sentence}')
        frames = {g.name: ctx.game_frame(g.name) for g in ctx.games}
        ctx.wait_for('both games keep drawing frames after arrest travel',
                     lambda: all(ctx.game_frame(g.name) > frames[g.name] + 3 for g in ctx.games), 10)
        completed = True
    except BaseException:
        with contextlib.suppress(Exception):
            capture(ctx, folder, 'failure')
        raise
    finally:
        # Keep the failed state available to the runner's report; successful runs restore the whole world.
        if completed:
            ctx.console_on('', f'load {name}')
            ctx.wait_for('the party reloads its pre-theft checkpoint',
                         lambda: all(ctx.player_cell(g.name) == home and ctx.takes_input(g.name) for g in ctx.games)
                         and ctx.both_see_each_other(), 90)
        for file in [checkpoint, checkpoint.with_suffix('.skse')]:
            file.unlink(missing_ok=True)
