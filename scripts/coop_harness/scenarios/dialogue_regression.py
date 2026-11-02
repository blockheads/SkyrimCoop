"""Player-input coverage for competing picks, late closes and same-topic mouse choices."""
import contextlib
import re
import uuid
from concurrent.futures import ThreadPoolExecutor

from ..common import (AMREN, BRENUIN, CLAW_FOUND_LINES, CLAW_TOPIC, DOWN_KEY,
                      GIFT_OF_CHARITY, GIVE_COIN_TOPIC, GOLDEN_CLAW, LUCAN, LUCAN_TROUBLE_TOPIC,
                      LYDIA, NO_CLAW_LINE, TAB_KEY, UP_KEY)
from . import dialogue_mouse as mouse, dialogue_repro as evidence, menus


def picks(ctx, mark):
    result = []
    for row in ctx.log_trail('', mark)[0]:
        if match := re.search(r'Dialogue: player (\d+) picked topic ([0-9A-F]+) \(info ([0-9A-F]+)\)', row['text']):
            result.append({'player': int(match[1]), 'topic': int(match[2], 16), 'info': int(match[3], 16)})
    return result


def mouse_racing_pick(ctx, instance, point):
    ctx.move_cursor(instance, point)
    ctx.held_a_frame(instance)
    ctx.click(instance)


def ready(ctx, instances=('', 'client')):
    return all(ctx.menu_takes_pick(name) for name in instances)


def highlight(ctx, instance, index):
    while (current := ctx.cursor_index(instance)) != index:
        ctx.tap_key(instance, DOWN_KEY if current < index else UP_KEY)
        ctx.wait_for('the selection moves', lambda: ctx.cursor_index(instance) != current, 5)


def close_menus(ctx):
    for name in ('client', ''):
        for menu in ('BarterMenu', 'Training Menu'):
            menus.close(ctx, name, menu)
    ctx.close_dialogues()
    ctx.wait_for('both players regain gameplay input', lambda: all(ctx.takes_input(name) for name in ('', 'client')), 10)


@contextlib.contextmanager
def checkpoint(ctx):
    name = 'DialogueCheck' + uuid.uuid4().hex[:10]
    home = ctx.player_cell('')
    hour = ctx.calendar('')['hour']
    save = ctx.save_game('', name)
    ctx.wait_for('the dialogue fixture checkpoint is written', ctx.saves_written(save), 15)
    completed = False
    try:
        yield
        completed = True
    finally:
        if completed:
            later = (hour + 5) % 24
            ctx.console_on('', f'set gamehour to {later}')
            ctx.wait_for('both games move beyond the saved fixture time', lambda:
                         all(abs(ctx.calendar(n)['hour'] - later) < 0.1 for n in ('', 'client')), 15)
            ctx.console_on('', f'load {name}')
            ctx.wait_for('both games restore the pre-dialogue world and input', lambda:
                         all(ctx.player_cell(n) == home and ctx.takes_input(n) and ctx.game_state(n).get('online')
                             and abs(ctx.calendar(n)['hour'] - hour) < 0.1 for n in ('', 'client'))
                         and ctx.both_see_each_other(), 90)
        save.unlink(missing_ok=True)
        save.with_suffix('.skse').unlink(missing_ok=True)


@contextlib.contextmanager
def speaker(ctx, base, folder):
    npc, _, copy = ctx.spawn_host_npc(base, restrained=True)
    failed = False
    try:
        ctx.place_before('', npc, 110, 90)
        ctx.place_before('client', copy, 110, 270)
        yield npc, copy
    except BaseException:
        failed = True
        with contextlib.suppress(Exception):
            evidence.capture(ctx, folder, 'failure')
        raise
    finally:
        def cleanup():
            close_menus(ctx)
            ctx.remove_host_npc(npc)
        if failed:
            with contextlib.suppress(Exception):
                cleanup()
        else:
            cleanup()


def competing_picks(ctx, double_click=False):
    with checkpoint(ctx):
        _competing_picks(ctx, double_click)


def _competing_picks(ctx, double_click):
    folder = evidence.evidence_folder('double-click' if double_click else 'competing-picks')
    purse = ctx.party_gold()
    try:
        ctx.set_party_gold(max(purse, 10))
        ctx.console_on('', 'set gamehour to gamehour + 5')
        with speaker(ctx, BRENUIN, folder) as (npc, copy):
            ctx.activate_on('client', copy)
            ctx.wait_for('the guest starts the beggar conversation', lambda: ctx.talking_to('client', copy), 10)
            ctx.activate_on('', npc)
            ctx.wait_for('the host joins the beggar conversation', lambda: ctx.talking_to('', npc), 10)
            ctx.wait_for('both beggar lists take input', lambda: ready(ctx), 30)
            ctx.client_shows_hosts_list('before competing input')
            options = ctx.dialogue('client')['options']
            index = next(i for i, option in enumerate(options) if option['topic'] == GIVE_COIN_TOPIC)
            highlight(ctx, 'client', index)
            ctx.wait_for('both players highlight the coin', lambda: ctx.cursor_index('') == index, 5)
            balance = ctx.party_gold()
            targets = {}
            if double_click:
                scale = mouse.calibrate(ctx, 'client')
                mouse.hover(ctx, 'client', index, scale)
            else:
                other = next(i for i in range(len(options)) if i != index)
                for name, choice in [('', other), ('client', index)]:
                    scale = mouse.calibrate(ctx, name)
                    targets[name] = mouse.target(ctx, name, choice, scale)
            _, mark = ctx.log_trail('', 1 << 62)
            _, guest_mark = ctx.log_trail('client', 1 << 62)
            if double_click:
                ctx.click('client')
                ctx.click('client')
            else:
                with ThreadPoolExecutor(max_workers=2) as pool:
                    futures = [pool.submit(mouse_racing_pick, ctx, name, targets[name]['point']) for name in ('', 'client')]
                    for future in futures:
                        future.result()
            accepted = ctx.wait_for('the host accepts a competing choice', lambda: picks(ctx, mark), 10)
            winner = accepted[0]
            wanted = next((option for option in options if option['topic'] == winner['topic'] and option['info'] == winner['info']),
                          next(option for option in options if option['topic'] == winner['topic']))
            ctx.wait_for('every menu shows the winning choice rather than its rejected local click', lambda: all(
                ctx.menu_var(name, 'Dialogue Menu', '_root.DialogueMenu_mc.TopicListHolder.TextCopy_mc.textField.text') == wanted['text']
                for name in ('', 'client')), 3)
            requested = {name or 'host': [row for row in ctx.log_trail(name, since)[0]
                                         if 'Dialogue: picks option ' in row['text']]
                         for name, since in [('', mark), ('client', guest_mark)]}
            evidence.capture(ctx, folder, 'winning', accepted=winner, requested=requested)
            cost = int(winner['topic'] == GIVE_COIN_TOPIC)
            ctx.wait_party_gold('the party pays for exactly one accepted choice', balance - cost)
            ctx.wait_for('both lists recover after the accepted choice', lambda: ready(ctx), 45)
            accepted = picks(ctx, mark)
            evidence.capture(ctx, folder, 'recovered', accepted=accepted, gold_before=balance, gold_after=ctx.party_gold())
            if len(accepted) != 1 or (double_click and accepted[0]['topic'] != GIVE_COIN_TOPIC):
                raise ctx.failure(f'competing input said more than one choice: {accepted}')
            ctx.leave_dialogue('client')
            ctx.leave_dialogue('')
            ctx.wait_for('the recovered conversation can end normally', lambda: all(ctx.takes_input(n) for n in ('', 'client')), 10)
    finally:
        for name in ('', 'client'):
            ctx.console_on(name, f'player.dispel {GIFT_OF_CHARITY:X}')
        ctx.set_party_gold(purse)


def immediate_reopen(ctx):
    folder = evidence.evidence_folder('immediate-reopen')
    with speaker(ctx, LYDIA, folder) as (first, first_copy):
        with speaker(ctx, AMREN, folder) as (second, second_copy):
            for index, (npc, copy) in enumerate([(first, first_copy), (second, second_copy), (first, first_copy)]):
                ctx.place_before('client', copy, 110)
                ctx.activate_on('client', copy)
                ctx.wait_for('the newly selected NPC opens without a host join', lambda: ctx.talking_to('client', copy), 10)
                ctx.wait_for('the newly opened list accepts a choice', lambda: ctx.menu_takes_pick('client'), 30)
                if ctx.dialogue('')['menu']:
                    raise ctx.failure('the host unexpectedly joined during close/reopen')
                evidence.capture(ctx, folder, f'open-{index}', npc=npc)
                ctx.tap_key('client', TAB_KEY)
                # The next activation follows the local close, without waiting for the host's close acknowledgement.
                ctx.wait_for('the guest menu closes before the next activation', lambda: ctx.menu_closed('client'), 10)
            ctx.wait_for('both engines release the final speaker',
                         lambda: not ctx.dialogue('')['speaker'] and not ctx.dialogue('client')['speaker'], 10)


def same_topic_mouse(ctx):
    with checkpoint(ctx):
        _same_topic_mouse(ctx)


def _same_topic_mouse(ctx):
    folder = evidence.evidence_folder('same-topic-mouse')
    purse = ctx.party_gold()
    if any(ctx.item_count(name, 0x14, GOLDEN_CLAW) for name in ('', 'client')):
        raise ctx.failure('the same-topic fixture needs both players to start without the golden claw')
    try:
        with evidence.lucan(ctx, folder):
            ctx.console_on('client', f'player.additem {GOLDEN_CLAW:X} 1')
            guest_copy = ctx.player_copy('')
            ctx.wait_for('the host sees the guest holding the claw', lambda: ctx.item_count('', guest_copy, GOLDEN_CLAW) == 1, 10)
            ctx.pick_with_keys('client', LUCAN_TROUBLE_TOPIC)
            ctx.wait_for('the two distinct claw lines are offered',
                         lambda: NO_CLAW_LINE in ctx.topic_infos('client', CLAW_TOPIC)
                         and bool(set(ctx.topic_infos('client', CLAW_TOPIC)) & CLAW_FOUND_LINES), 90)
            ctx.wait_for('both claw lists take input', lambda: ready(ctx), 30)
            ctx.client_shows_hosts_list('before same-topic mouse choices')
            options = ctx.dialogue('client')['options']
            forbidden = next(i for i, option in enumerate(options) if option['topic'] == CLAW_TOPIC and option['info'] == NO_CLAW_LINE)
            owned = next(i for i, option in enumerate(options) if option['topic'] == CLAW_TOPIC and option['info'] in CLAW_FOUND_LINES)
            scale = mouse.calibrate(ctx, 'client')
            mouse.hover(ctx, 'client', forbidden, scale)
            if mouse.value(ctx, 'client', 'selectedEntry.text') != options[forbidden]['text']:
                raise ctx.failure('the forbidden claw row renders the wrong text')
            _, mark = ctx.log_trail('', 1 << 62)
            _, guest_mark = ctx.log_trail('client', 1 << 62)
            ctx.click('client')
            ctx.wait_for('the guest receives a local refusal for the other player\'s claw INFO', lambda: any(
                f'(topic {CLAW_TOPIC:X}) is another player\'s: not sent' in row['text']
                for row in ctx.log_trail('client', guest_mark)[0]), 10)
            ctx.wait_for('refusing another player\'s same-topic line restores both usable lists', lambda: ready(ctx), 10)
            if picks(ctx, mark) or ctx.item_count('client', 0x14, GOLDEN_CLAW) != 1:
                raise ctx.failure('the forbidden same-topic mouse choice was executed')
            evidence.capture(ctx, folder, 'refused', requested=options[forbidden])
            mouse.hover(ctx, 'client', owned, scale)
            if mouse.value(ctx, 'client', 'selectedEntry.text') != options[owned]['text']:
                raise ctx.failure('the owned claw row renders the wrong text')
            ctx.click('client')
            wanted = options[owned]
            ctx.wait_for('the host accepts exactly the highlighted claw INFO', lambda: any(
                choice['topic'] == wanted['topic'] and choice['info'] == wanted['info'] for choice in picks(ctx, mark)), 10)
            ctx.wait_for('the host actually says the guest\'s claw line', lambda: ctx.dialogue('')['current_info'] == wanted['info'], 10)
            evidence.capture(ctx, folder, 'accepted', wanted=wanted, accepted=picks(ctx, mark))
            if ctx.item_count('', 0x14, GOLDEN_CLAW):
                raise ctx.failure('the host was given the guest\'s claw')
            ctx.wait_for('the claw answer finishes before teardown',
                         lambda: ctx.line_over('', LUCAN) and ctx.line_over('client', LUCAN), 90)
    finally:
        count = ctx.item_count('client', 0x14, GOLDEN_CLAW)
        if count:
            ctx.console_on('client', f'player.removeitem {GOLDEN_CLAW:X} {count}')
        ctx.set_party_gold(purse)
