"""`play`'s follower: the game you don't control walks after your player's copy and faces it, so its screen (the watch
view) shows your player as the other game sees it (docs/design/testing.md, "To play by hand"). `decide` is the pure part."""
import math
import time
from dataclasses import dataclass, field

from .errors import TestFailure
from .utilities import log

FOLLOW_RADIUS = 200.0  # units: KeepOffsetFromActor walks inside this, stands at it
CATCH_UP = 350.0  # units: beyond this it runs (KeepOffsetFromActor's catch-up radius)
HEIGHT_GAP = 200.0  # units up or down: another floor or below a ledge, not arrived however close
FAR = 2500.0  # units: beyond this, left behind (a sprint, a jump down): placed beside it
STUCK_UNITS, STUCK_SECONDS = 60.0, 3.0  # behind and moved less than this: something is in the way
SET_OFF_SECONDS = 5.0  # a new KeepOffsetFromActor takes a package evaluation to set off (play_verbs.SET_OFF_SECONDS)
UNSTICK_TRIES = 2  # path legs round what stops it before it is placed beside it
PLACE_GAP = 150.0  # units from the copy a placement stands
STILL_UNITS, STILL_SECONDS = 15.0, 1.0  # the follower stands: it may turn to face
FACE_DEGREES = 25.0  # off by more than this, it turns to face
FACE_EVERY = 1.5  # seconds between turns (the engine's own turning gets its time)
GONE_SECONDS = 10.0  # copy missing this long: said once (the party tether brings the follower)


def bearing(here, there):
    """Degrees from `here` to `there`, 0 north (+y), 90 east: an actor's heading (angle z) in the same terms."""
    return math.degrees(math.atan2(there[0] - here[0], there[1] - here[1])) % 360


def heading_error(here, heading, there):
    """How far a heading (radians, angle z) is off facing `there`, degrees 0-180."""
    return abs((bearing(here, there) - math.degrees(heading) + 180) % 360 - 180)


@dataclass
class FollowState:
    following: bool = False  # KeepOffsetFromActor set
    since: float = -math.inf  # when it was set (or a path leg ended)
    track: list = field(default_factory=list)  # [(time, position)] of the follower
    unsticks: int = 0  # path legs since it last closed in
    faced: float = -math.inf  # when it last turned
    gone: float | None = None  # since when the copy is missing
    told_gone: bool = False


def moved(track, now, seconds):
    """Units the follower moved in its last `seconds`, or None while the track is shorter than that."""
    if not track or now - track[0][0] < seconds:
        return None
    past = [p for t, p in track if now - t >= seconds]
    return math.dist(past[-1][:2], track[-1][1][:2])


def decide(state, now, here, heading, there):
    """The follower's next step: "wait" (no copy), "follow" (set KeepOffsetFromActor), "unstick" (a path leg round
    what stops it), "place" (beside the copy: left behind or stuck), "face" (turn to it) or "stay"."""
    if there is None:
        state.gone = now if state.gone is None else state.gone
        return "wait"
    state.gone, state.told_gone = None, False
    state.track = [(t, p) for t, p in state.track if now - t <= max(STUCK_SECONDS, STILL_SECONDS) + 1] + [(now, here)]
    apart = math.dist(here[:2], there[:2])
    close = apart <= CATCH_UP and abs(here[2] - there[2]) <= HEIGHT_GAP
    if apart > FAR:
        return "place"
    if close:
        state.unsticks = 0
    elif (state.following and now - state.since >= SET_OFF_SECONDS
          and (gone := moved(state.track, now, STUCK_SECONDS)) is not None and gone < STUCK_UNITS):
        return "place" if state.unsticks >= UNSTICK_TRIES else "unstick"
    if not state.following:
        return "follow"
    still = moved(state.track, now, STILL_SECONDS)
    if (close and still is not None and still < STILL_UNITS and now - state.faced >= FACE_EVERY
            and heading_error(here, heading, there) > FACE_DEGREES):
        return "face"
    return "stay"


class Follower:
    """Drives game `instance`'s player after the other player's copy: AI-driven (Game.SetPlayerAIDriven) with
    Actor.KeepOffsetFromActor, a PathToReference leg when stuck, checked placement when left behind."""

    def __init__(self, ctx, instance):
        self.ctx, self.instance, self.state = ctx, instance, FollowState()
        self.name = "guest" if instance else "host"
        self.on = False

    def start(self):
        self.ctx.player_script_call(self.instance, "Game", "SetPlayerAIDriven", [True])
        self.state, self.on = FollowState(), True

    def stop(self):
        """Both cleanups tried; if either fails it stays on, so another stop tries again."""
        if not self.on:
            return
        failed = None
        for cleanup in (lambda: self.keep_offset(None),
                        lambda: self.ctx.player_script_call(self.instance, "Game", "SetPlayerAIDriven", [False])):
            try:
                cleanup()
            except (TestFailure, OSError) as error:
                failed = failed or error
        if failed:
            raise failed
        self.on = False

    def keep_offset(self, copy):
        call = {"cmd": "papyrus_call", "class": "Actor", "player_action": True, "self": 0x14}
        if copy is None:
            self.ctx.player_command(self.instance, {**call, "function": "ClearKeepOffsetFromActor", "args": []})
        else:
            self.ctx.player_command(self.instance, {**call, "function": "KeepOffsetFromActor",
                                                    "args": [{"form": copy}, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                             CATCH_UP, FOLLOW_RADIUS]})

    def tick(self):
        """One step; a game that does not answer (a load screen) waits for the next."""
        if not self.on:
            return None
        ctx, now = self.ctx, time.monotonic()
        try:
            player = ctx.local_player(self.instance) or {}
            here, heading = player.get("position"), (player.get("rotation") or [0, 0, 0])[2]
            copy = ctx.player_copy(self.instance)
            there = ctx.place_of(self.instance, copy) if copy and here else None
            step = decide(self.state, now, here, heading, there)
            if step == "wait":
                if now - self.state.gone > GONE_SECONDS and not self.state.told_gone:
                    log(f"[{self.name}] follow: no copy of your player here; the party tether brings it along")
                    self.state.told_gone = True
                self.state.following = False
            elif step == "follow":
                self.keep_offset(copy)
                self.state.following, self.state.since = True, now
            elif step == "unstick":
                self.state.unsticks += 1
                log(f"[{self.name}] follow: stuck {math.dist(here[:2], there[:2]):.0f} units behind; a path leg round it")
                ctx.path_leg(self.instance, copy, CATCH_UP, now + 6.0)
                self.state.track, self.state.following = [], False
            elif step == "place":
                log(f"[{self.name}] follow: {math.dist(here[:2], there[:2]):.0f} units behind after "
                    f"{self.state.unsticks} path legs; placed beside your player (checked placement)")
                ctx.place_before(self.instance, copy, PLACE_GAP)
                self.state.track, self.state.unsticks, self.state.following = [], 0, False
            elif step == "face":
                ctx.face(self.instance, copy)
                self.state.faced = now
            return step
        except (TestFailure, OSError, KeyError, TypeError) as error:
            log(f"[{self.name}] follow: {error}; trying again")
            self.state.following = False
            return None
