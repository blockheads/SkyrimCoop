"""Native reproductions for guest exits, shared mouse selection and picked-text rendering."""
import contextlib
import json
import subprocess
import time

from ..common import ACTIVATE_KEY, DOWN_KEY, ESCAPE_KEY, GOLDEN_CLAW_QUEST, LUCAN, LYDIA, ONMUND_NOT_A_NORD, TAB_KEY, TEST_HOME, WINDOWS
from ..utilities import log
from ..comparisons import distance
from . import dialogue, dialogue_mouse as mouse


def evidence_folder(label):
    folder = TEST_HOME / 'dialogue-repro' / (time.strftime('%Y%m%d-%H%M%S') + '-' + label)
    folder.mkdir(parents=True)
    return folder


def capture(ctx, folder, label, **values):
    data = {'time': time.time(), **values, 'games': {}}
    for game in ctx.games:
        name = game.name or 'host'
        status = game.read_status() or {}
        data['games'][name] = {'frames': status.get('frames'), 'game': status.get('game'),
                               'menus': status.get('menus'), 'player': status.get('player')}
        with contextlib.suppress(Exception):
            data['games'][name]['input'] = ctx.player_command(game.name, {'cmd': 'input_state'})
            data['games'][name]['dialogue'] = ctx.dialogue(game.name)
            data['games'][name]['rendered'] = mouse.value(ctx, game.name, 'selectedEntry.text')
            data['games'][name]['rows'] = mouse.rows(ctx, game.name)
            data['games'][name]['picked'] = {
                key: ctx.menu_var(game.name, 'Dialogue Menu', '_root.DialogueMenu_mc.TopicListHolder.TextCopy_mc.' + key)
                for key in ('_visible', '_x', '_y', 'textField.text', 'textField._y')}
        if not WINDOWS:
            subprocess.run(['import', '-display', game.display, '-window', 'root',
                            str(folder / f'{label}-{name}.png')], capture_output=True)
    (folder / f'{label}.json').write_text(json.dumps(data, indent=2) + '\n')
    log(f'Dialogue evidence: {folder / label}')


@contextlib.contextmanager
def lucan(ctx, folder):
    copy = ctx.stage_host_npc(LUCAN)
    ctx.console_on('', f'{LUCAN:X}.setrestrained 1')
    ctx.freeze('', LUCAN, False)
    failed = False
    try:
        if not ctx.quest_running('', GOLDEN_CLAW_QUEST):
            ctx.console_on('', 'resetquest MS13')
            ctx.console_on('', 'startquest MS13')
            ctx.wait_for('MS13 runs for the dialogue fixture', lambda: ctx.quest_running('', GOLDEN_CLAW_QUEST), 10)
        ctx.console_on('', 'setstage MS13 60')
        ctx.place_before('', LUCAN, 110)
        ctx.place_before('client', copy, 110)
        ctx.activate_on('client', copy)
        ctx.wait_for('the guest opens Lucan without the host joining', lambda: ctx.talking_to('client', copy), 10)
        ctx.activate_on('', LUCAN)
        ctx.wait_for('the host joins Lucan', lambda: ctx.talking_to('', LUCAN), 10)
        for instance in ('', 'client'):
            ctx.wait_for(f'[{instance or "host"}] Lucan takes a choice', lambda instance=instance: ctx.menu_takes_pick(instance), 30)
        ctx.client_shows_hosts_list('before the mouse test')
        if len(ctx.options_shown('client')) < 2:
            raise ctx.failure('the mouse fixture needs at least two Lucan options')
        yield copy
    except BaseException:
        failed = True
        with contextlib.suppress(Exception):
            capture(ctx, folder, 'failure')
        raise
    finally:
        def cleanup():
            ctx.close_dialogues()
            ctx.wait_for('both players return to gameplay after the mouse test',
                         lambda: all(ctx.takes_input(name) for name in ('', 'client')), 10)
            ctx.console_on('', f'{LUCAN:X}.setrestrained 0')
            ctx.release_host_npc(LUCAN)
            ctx.console_on('', 'stopquest MS13')
        if failed:
            with contextlib.suppress(Exception):
                cleanup()
        else:
            cleanup()


def guest_exits(ctx, placed=None, walking=False):
    folder = evidence_folder('s9-wylandriah-exits' if placed else 'guest-exits')
    if placed:
        npc = placed
        copy = ctx.stage_host_npc(npc)
        ctx.console_on('', f'{npc:X}.setrestrained 1')
        ctx.freeze('', npc, False)
    else:
        npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    failed = False
    try:
        ctx.place_before('', npc, 800, 90)
        ctx.place_before('client', copy, 110)
        modes = [('list-tab', TAB_KEY), ('greeting-tab', TAB_KEY), ('walk-back', TAB_KEY)] if walking else [
            ('list-tab', TAB_KEY), ('list-escape', ESCAPE_KEY), ('answer-tab', TAB_KEY)]
        for mode, key in modes:
            ctx.activate_on('client', copy)
            ctx.wait_for('the guest opens dialogue alone', lambda: ctx.talking_to('client', copy), 10)
            if ctx.dialogue('')['menu']:
                raise ctx.failure('the host unexpectedly joined the guest-only conversation')
            if mode == 'greeting-tab':
                before = ctx.dialogue('client')
                ctx.tap_key('client', TAB_KEY)
                capture(ctx, folder, 'greeting-tab-press', before=before)
                ctx.wait_for('the greeting can be skipped or completed without host intervention',
                             lambda: ctx.menu_closed('client') or ctx.menu_takes_pick('client'), 30)
            else:
                ctx.wait_for('the guest list accepts input', lambda: ctx.menu_takes_pick('client'), 30)
            if mode == 'answer-tab':
                ctx.tap_key('client', ACTIVATE_KEY)
                ctx.wait_for('the host starts the selected answer', lambda: ctx.line_playing('', npc), 10)
            if mode == 'walk-back':
                start, frame = ctx.my_position('client'), ctx.game_frame('client')
                ctx.player_command('client', {'cmd': 'input', 'button': 'back', 'down': True})
                ctx.guest.held_buttons.add('back')
                try:
                    ctx.wait_for('the guest attempts to walk away with its dialogue menu open',
                                 lambda: ctx.menu_closed('client') or distance(ctx.my_position('client'), start) > 150
                                 or ctx.game_frame('client') > frame + 90, 10)
                finally:
                    ctx.release('client', 'back')
                capture(ctx, folder, 'movement-with-menu', start=start, end=ctx.my_position('client'),
                        moved=distance(ctx.my_position('client'), start))
            capture(ctx, folder, mode + '-before')
            if not ctx.menu_closed('client'):
                ctx.tap_key('client', key)
            ctx.wait_for(f'{mode}: the guest exits without host intervention',
                         lambda: ctx.menu_closed('client') and not ctx.dialogue('')['speaker']
                         and not ctx.dialogue('client')['speaker'] and ctx.takes_input('client'), 10)
            if ctx.dialogue('')['menu']:
                raise ctx.failure('the host joined during the guest exit test')
            capture(ctx, folder, mode + '-after')
            if mode == 'walk-back':
                start = ctx.my_position('client')
                ctx.press('client', 'back')
                try:
                    ctx.wait_for('the guest can really walk after closing dialogue',
                                 lambda: distance(ctx.my_position('client'), start) > 25, 5)
                finally:
                    ctx.release('client', 'back')
                capture(ctx, folder, 'movement-after-tab', start=start, end=ctx.my_position('client'))
    except BaseException:
        failed = True
        with contextlib.suppress(Exception):
            capture(ctx, folder, 'failure')
        raise
    finally:
        def cleanup():
            ctx.close_dialogues()
            if placed:
                ctx.console_on('', f'{npc:X}.setrestrained 0')
                ctx.release_host_npc(npc)
            else:
                ctx.remove_host_npc(npc)
        if failed:
            with contextlib.suppress(Exception):
                cleanup()
        else:
            cleanup()


def mouse_highlights(ctx, fast_pick=False):
    folder = evidence_folder('mouse-pick' if fast_pick else 'mouse-highlight')
    with lucan(ctx, folder):
        scale = mouse.calibrate(ctx, 'client')
        options = ctx.dialogue('client')['options']
        marks = {name: ctx.log_trail(name, 1 << 62)[1] for name in ('', 'client')}
        history = []
        for index in [1, 0, min(2, len(options) - 1), 1, 0]:
            history.append(mouse.hover(ctx, 'client', index, scale, settle=not fast_pick))
            if not fast_pick:
                ctx.wait_for(f'the host follows mouse option {index}', lambda: ctx.cursor_index('') == index, 5)
                for name in ('', 'client'):
                    if mouse.value(ctx, name, 'selectedEntry.text') != options[index]['text']:
                        raise ctx.failure(f'[{name or "host"}] rendered selection disagrees with mouse option {index}')
                capture(ctx, folder, f'hover-{len(history)}', history=history)
        if fast_pick:
            before = ctx.dialogue('client')
            ctx.click('client')
            capture(ctx, folder, 'after-click', before=before, history=history)
            wanted = options[0]
            ctx.wait_for('the mouse click reaches the host with the requested topic',
                         lambda: any(f'picked topic {wanted["topic"]:X} (info ' in row['text']
                                     for row in ctx.log_trail('', marks[''])[0]), 10)
            capture(ctx, folder, 'accepted', wanted=wanted, dialogue_logs={
                name or 'host': [row for row in ctx.log_trail(name, mark)[0] if 'Dialogue:' in row['text']]
                for name, mark in marks.items()})
        else:
            ctx.tap_key('', DOWN_KEY)
            target = ctx.cursor_index('')
            ctx.wait_for('the guest follows the host while its mouse stays still', lambda: ctx.cursor_index('client') == target, 5)
            frame = ctx.game_frame('client')
            def stable():
                if ctx.cursor_index('client') != target or ctx.cursor_index('') != target:
                    raise ctx.failure('a stationary mouse or delayed highlight changed the final shared choice')
                return ctx.game_frame('client') > frame + 60
            ctx.wait_for('the shared highlight stays stable without further input', stable, 5)
            capture(ctx, folder, 'stable', history=history)


def picked_text(ctx):
    folder = evidence_folder('claw-picked-text')
    tap = ctx.tap_key
    count = 0
    def record(instance, code, shift=False):
        nonlocal count
        tap(instance, code, shift=shift)
        if code == ACTIVATE_KEY:
            count += 1
            capture(ctx, folder, f'pick-{count}')
    ctx.tap_key = record
    try:
        dialogue.scenario_dialogue_options_are_the_partys(ctx)
    except BaseException:
        with contextlib.suppress(Exception):
            capture(ctx, folder, 'failure')
        raise
    finally:
        ctx.tap_key = tap


def refused_pick(ctx):
    """A locally refused pick must restore its list without another player's help."""
    folder = evidence_folder('refused-pick')
    pick = ctx.pick_with_keys
    def record(instance, topic, info=None):
        pick(instance, topic, info)
        if instance == '' and topic == ONMUND_NOT_A_NORD:
            capture(ctx, folder, 'after-refused-pick')
            try:
                ctx.wait_for('a refused choice restores the choosing player\'s list without the guest answering',
                             lambda: ctx.menu_takes_pick(''), 5)
            except BaseException:
                with contextlib.suppress(Exception):
                    capture(ctx, folder, 'still-blocked')
                raise
            capture(ctx, folder, 'restored')
    ctx.pick_with_keys = record
    try:
        dialogue.scenario_dialogue_question_answered_by_who_can(ctx)
    except BaseException:
        with contextlib.suppress(Exception):
            capture(ctx, folder, 'failure')
        raise
    finally:
        ctx.pick_with_keys = pick


def greeting_end(ctx):
    """Replay the recorded early-press lead while keeping the menu's readiness guard."""
    folder = evidence_folder('greeting-end')
    wait, tap = ctx.wait_for, ctx.tap_key
    def early(description, predicate, *args, **kwargs):
        if description == "[client] its copy's greeting is over":
            return True
        return wait(description, predicate, *args, **kwargs)
    def record(instance, code, shift=False):
        tap(instance, code, shift=shift)
        if instance == 'client' and code == ACTIVATE_KEY:
            capture(ctx, folder, 'after-early-press')
    ctx.wait_for, ctx.tap_key = early, record
    try:
        dialogue.scenario_dialogue_pick_reaches_host(ctx)
    except BaseException:
        with contextlib.suppress(Exception):
            capture(ctx, folder, 'failure')
        raise
    finally:
        ctx.wait_for, ctx.tap_key = wait, tap
