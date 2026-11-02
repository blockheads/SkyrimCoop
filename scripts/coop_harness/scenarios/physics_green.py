"""S2 watching-game probes in Sleeping Giant Inn and historical Vilemyr controls."""
import contextlib
import json
import math
import subprocess
import threading
import time
import uuid

from ..common import TEST_HOME, WINDOWS
from ..comparisons import first_body
from ..utilities import log
from .dialogue_regression import checkpoint

INN = 'IvarsteadVilemyrInn'
KETTLE, WINE, BASSIANUS = 0x9EF53, 0x9E194, 0x19E08


class VisualCapture:
    def __init__(self, ctx, label, target, camera_targets=None):
        self.ctx, self.target = ctx, target
        self.camera_targets = camera_targets or {}
        self.folder = TEST_HOME / 'green-flash' / (time.strftime('%Y%m%d-%H%M%S') + '-' + label + '-' + uuid.uuid4().hex[:4])
        self.processes, self.events = [], []

    def __enter__(self):
        if WINDOWS:
            raise self.ctx.failure('this high-rate X11 capture probe currently requires Linux')
        self.folder.mkdir(parents=True)
        try:
            for game in self.ctx.games:
                name = game.name or 'host'
                path = self.folder / f'{name}.mkv'
                errors = (self.folder / f'{name}-ffmpeg.log').open('w')
                process = subprocess.Popen([
                    'ffmpeg', '-y', '-hide_banner', '-nostats', '-thread_queue_size', '256',
                    '-f', 'x11grab', '-framerate', '120', '-video_size', '1280x720',
                    '-i', f'{game.display}+4,30', '-an', '-c:v', 'libx264rgb', '-crf', '0',
                    '-preset', 'ultrafast', '-threads', '1', str(path)],
                    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=errors, text=True)
                self.processes.append((process, errors, path))
                self.ctx.player_command(game.name, {'cmd': 'body_trace', 'refs': [self.target]})
            self.ctx.wait_for('both lossless screen recordings have frames', lambda:
                              all(process.poll() is None and path.exists() and path.stat().st_size > 4096
                                  for process, _, path in self.processes), 10)
            self.mark('before grab')
        except BaseException:
            self.stop_recordings()
            raise
        log(f'S2 lossless 120 fps capture: {self.folder}')
        return self

    def mark(self, label):
        event = {'time': time.time(), 'label': label, 'games': {}}
        for game in self.ctx.games:
            name = game.name or 'host'
            event['games'][name] = {
                'frame': self.ctx.game_frame(game.name), 'player': self.ctx.local_player(game.name),
                'body': self.ctx.body_of(game.name, self.target),
                'camera': self.ctx.player_command(game.name, {'cmd': 'camera', 'target': self.camera_targets.get(game.name, BASSIANUS)}),
                'effects': self.ctx.player_command(game.name, {'cmd': 'reference_effects', 'form': self.target}),
                'trace': self.ctx.player_command(game.name, {'cmd': 'body_trace'})}
        self.events.append(event)
        (self.folder / 'events.json').write_text(json.dumps(self.events, indent=2) + '\n')

    def __exit__(self, kind, value, traceback):
        try:
            with contextlib.suppress(Exception):
                self.mark('failure' if kind else 'complete')
            for game in self.ctx.games:
                with contextlib.suppress(Exception):
                    data = self.ctx.player_command(game.name, {'cmd': 'body_trace', 'stop': True})
                    (self.folder / f'{game.name or "host"}-final-trace.json').write_text(json.dumps(data))
                self.ctx.collect_logs(self.folder, _game=game)
        finally:
            self.stop_recordings()

    def stop_recordings(self):
        for process, errors, _ in self.processes:
            try:
                process.communicate('q', timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
            errors.close()

    @contextlib.contextmanager
    def watch_tankard(self, watcher):
        """Sample throughout the aimed hold: a single screenshot can fall between flashes."""
        from PIL import Image
        from analyze_green_flash import green_pixels

        path = self.folder / f'{watcher or "host"}-tankard.png'
        counts, errors, samples = [], [], []
        with (self.folder / 'appearance-ffmpeg.log').open('w') as log_file:
            process = subprocess.Popen([
                'ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error',
                '-f', 'x11grab', '-framerate', '60', '-video_size', '96x96',
                '-i', f'{self.ctx.game(watcher).display}+596,342',
                '-threads', '1', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'],
                stdout=subprocess.PIPE, stderr=log_file)

            def read():
                try:
                    while data := process.stdout.read(96 * 96 * 3):
                        if len(data) != 96 * 96 * 3:
                            break
                        shot = Image.frombytes('RGB', (96, 96), data)
                        count = green_pixels(shot, red_excess=10, blue_excess=3, minimum=20)
                        if count > max(counts, default=0):
                            shot.save(path)
                        counts.append(count)
                        samples.append({"time": time.time(), "green_pixels": count})
                except Exception as error:
                    errors.append(str(error))

            reader = threading.Thread(target=read)
            reader.start()
            try:
                self.ctx.wait_for('the tankard appearance capture starts', lambda: counts or errors, 5)
                yield
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                reader.join(timeout=5)
                process.stdout.close()
        result = {'watcher': watcher or 'host', 'frames': len(counts), 'green_pixels': max(counts, default=0),
                  'flashing_frames': sum(c > 128 for c in counts), 'errors': errors, 'samples': samples}
        (self.folder / 'appearance.json').write_text(json.dumps(result))
        if errors or len(counts) < 60:
            raise self.ctx.failure(f'incomplete tankard appearance capture: {result}')
        if result['flashing_frames']:
            raise self.ctx.failure(f'S2: watching {watcher or "host"} sees green on the metal tankard: '
                                   f'{result["flashing_frames"]}/{len(counts)} frames, max {result["green_pixels"]} pixels; {path}')


def frames(ctx, instance, count):
    start = ctx.game_frame(instance)
    ctx.wait_for(f'[{instance or "host"}] observes {count} rendered frames',
                 lambda: ctx.game_frame(instance) >= start + count, 15)


def look_pitch(ctx, instance, goal):
    """Move the actual mouse, calibrating its sign/sensitivity from observed player rotation."""
    def pitch():
        return math.degrees(ctx.local_player(instance)['rotation'][0])
    ratio = None
    for _ in range(160):
        before = pitch()
        if abs(goal - before) < 2:
            return
        dy = -8 if ratio is None else max(-12, min(12, round((goal - before) / ratio)))
        if not dy:
            dy = 1 if (goal - before) / ratio > 0 else -1
        ctx.player_command(instance, {'cmd': 'mouse', 'dx': 0, 'dy': dy})
        ctx.wait_for(f'[{instance or "host"}] mouse changes camera pitch', lambda: abs(pitch() - before) > .02, 3)
        ratio = (pitch() - before) / dy
    raise ctx.failure(f'mouse pitch did not reach {goal}: at {pitch()}')


def aim_at(ctx, instance, position):
    camera = ctx.player_command(instance, {'cmd': 'camera'})['position']
    dx, dy, dz = [position[i] - camera[i] for i in range(3)]
    ctx.console_on(instance, f'player.setangle z {math.degrees(math.atan2(dx, dy))}')
    ctx.console_on(instance, f'player.setangle x {-math.degrees(math.atan2(dz, math.hypot(dx, dy)))}')


def grab(ctx, instance, target, gap=90):
    ctx.place_before(instance, target, gap)
    aim_at(ctx, instance, ctx.body_of(instance, target)['position'])
    ctx.wait_for('the crosshair identifies the prop before grabbing', lambda:
                 ctx.player_command(instance, {'cmd': 'camera'}).get('crosshair') == target, 3)
    ctx.press(instance, 'activate')
    try:
        ctx.wait_for(f'[{instance or "host"}] grabs the reported object',
                     lambda: ctx.local_player(instance).get('grabbed') == target, 4)
    except BaseException:
        ctx.console_on(instance, 'player.setangle x -80')
        raise
    finally:
        ctx.release(instance, 'activate')


def release(ctx, instance):
    if ctx.local_player(instance).get('grabbed'):
        ctx.tap(instance, 'activate')
    ctx.wait_for(f'[{instance or "host"}] releases the object',
                 lambda: not ctx.local_player(instance).get('grabbed'), 4)


def probe(ctx, holder='', target=KETTLE, handoff=False):
    """PASS means the recorded input sequence completed; video review decides whether S2 occurred."""
    with checkpoint(ctx):
        ctx.both_go(INN)
        ctx.player_command('', {'cmd': 'move_actor', 'actor': BASSIANUS, 'position': [1369, -43, 0]})
        ctx.wait_for('Bassianus is loaded in the reported inn', lambda: ctx.host_npc_state(BASSIANUS).get('session_id', -1) >= 0, 15)
        ctx.console_on('', f'{BASSIANUS:X}.setrestrained 1')
        copy = ctx.copy_of('client', ctx.host_npc_state(BASSIANUS)['session_id'])
        try:
            for axis, value in zip('xyz', [1510, -100, 30]):
                ctx.console_on('', f'{target:X}.setpos {axis} {value}')
            ctx.wait_for('both games have the original prop',
                         lambda: all(ctx.body_of(name, target) for name in ('', 'client')), 10)
            other = 'client' if not holder else ''
            ctx.place_before(other, copy if other else BASSIANUS, 240, bearing=0)
            label = f'{target:X}-{holder or "host"}' + ('-handoff' if handoff else '-head')
            with VisualCapture(ctx, label, target) as capture:
                grab(ctx, holder, target)
                capture.mark('held before contact')
                look_pitch(ctx, holder, -20)
                held = ctx.body_of(holder, target)['position']
                feet = ctx.my_position(holder)[2]
                ctx.player_command('', {'cmd': 'move_actor', 'actor': BASSIANUS, 'position': [held[0], held[1], feet]})
                ctx.wait_for('the NPC is below the held prop', lambda:
                             math.dist(ctx.host_npc_state(BASSIANUS)['position'][:2], held[:2]) < 20, 5)
                ctx.wait_on('client', 'the NPC copy is below the prop', lambda actors:
                            math.dist(actors.get(str(copy), {}).get('actual', [1e9, 1e9])[:2], held[:2]) < 30, 5)
                ctx.face(other, copy if other else BASSIANUS)
                for goal in (-10, 0, 10, -10, 5):
                    look_pitch(ctx, holder, goal)
                    if ctx.local_player(holder).get('grabbed') != target:
                        raise ctx.failure('the mouse sweep dropped the prop before the contact probe completed')
                    capture.mark(f'contact pitch {goal}')
                    frames(ctx, holder, 300 if goal == 0 else 45)
                for dx in (-8, 8, 8, -8):
                    for _ in range(10):
                        before = ctx.local_player(holder)['rotation'][2]
                        ctx.player_command(holder, {'cmd': 'mouse', 'dx': dx, 'dy': 0})
                        ctx.wait_for('the real mouse sweeps the held prop sideways', lambda:
                                     abs(ctx.local_player(holder)['rotation'][2] - before) > .0003, 3)
                    capture.mark(f'sideways mouse sweep {dx}')
                    if ctx.local_player(holder).get('grabbed') != target:
                        raise ctx.failure('the sideways sweep dropped the prop')
                release(ctx, holder)
                capture.mark('release')
                if handoff:
                    grab(ctx, other, target)
                    capture.mark('other player grabbed after release')
                    for goal in (0, -12, 8, 0):
                        look_pitch(ctx, other, goal)
                        frames(ctx, other, 30)
                    release(ctx, other)
                    capture.mark('other player released')
                frames(ctx, holder, 90)
        finally:
            for name in ('', 'client'):
                with contextlib.suppress(Exception):
                    release(ctx, name)
            ctx.console_on('', f'{BASSIANUS:X}.setrestrained 0')
        log('S2 input probe complete; inspect both videos before assigning a visual result.')


def observer_probe(ctx, holder='', fast=False):
    """The watching game observes a normal grab in Sleeping Giant Inn, in either direction."""
    with checkpoint(ctx):
        ctx.both_go('RiverwoodSleepingGiantInn')
        host, guest = ctx.world_state(''), ctx.world_state('client')
        resting = ctx.resting_clutter_on_both(host, guest)
        target = ctx.pick_reference(host, 'MISC', 0, lambda ref: resting(ref)
                                    and ref.get('base') == 0x319E3
                                    and 'tankard' in (first_body(ref).get('node') or '').lower())
        watcher = 'client' if not holder else ''
        log(f'S2 observer probe: {holder or "host"} holds {target:X}, base {host[target]["base"]:X}')
        ctx.place_before(holder, target, 145 if fast else 90, bearing=270)
        ctx.place_before(watcher, target, 190, bearing=0)
        camera_targets = {name: ctx.player_copy(name) for name in ('', 'client')}
        aim_at(ctx, watcher, ctx.body_of(watcher, target)['position'])
        try:
            with VisualCapture(ctx, f'observer-{holder or "host"}-{target:X}' + ('-fast' if fast else ''), target, camera_targets) as capture:
                grab(ctx, holder, target, gap=145 if fast else 90)
                capture.mark('first grab, inspect watching game')
                aim_at(ctx, watcher, ctx.body_of(watcher, target)['position'])
                look_pitch(ctx, holder, 0)
                aim_at(ctx, watcher, ctx.body_of(watcher, target)['position'])
                capture.mark('held at eye height')
                with capture.watch_tankard(watcher):
                    frames(ctx, holder, 300)
                    for dx in ((180, -300, 180, -60) if fast else (-8, 8, 8, -8)):
                        for _ in range(1 if fast else 8):
                            before = ctx.local_player(holder)['rotation'][2]
                            ctx.player_command(holder, {'cmd': 'mouse', 'dx': dx, 'dy': 0})
                            ctx.wait_for('the holder moves the object with the mouse', lambda:
                                         abs(ctx.local_player(holder)['rotation'][2] - before) > .0003, 3)
                        aim_at(ctx, watcher, ctx.body_of(watcher, target)['position'])
                        capture.mark(f'held mouse movement {dx}')
                        if ctx.local_player(holder).get('grabbed') != target:
                            raise ctx.failure('the holder dropped the prop during the observer probe')
                        if fast:
                            frames(ctx, holder, 45)
                    frames(ctx, holder, 300)
                release(ctx, holder)
                capture.mark('released')
                frames(ctx, watcher, 120)
        finally:
            with contextlib.suppress(Exception):
                release(ctx, holder)
        log('Observer probe recorded; inspect the watching game before assigning the visual result.')
