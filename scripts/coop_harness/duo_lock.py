"""The shared duo's lock (docs/design/testing.md, The duo lock): one hold, a FIFO queue, freed when its process ends.

duo.json (under the duo.lock mutex) is the truth; DUO.md is a view written from it. Each hold, join and queue place
owns a token file it keeps flock'ed while its process lives: a dead process's token is free, so its place is stale,
and waiters block on the token ahead of them (the kernel wakes them) instead of polling."""
import contextlib
import json
import os
import re
import select
import signal
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

try:
    import fcntl
except ImportError:  # Windows: no shared duo, no lock (ReproActions.duo_lock)
    fcntl = None

IDLE_MINUTES = float(os.environ.get("COOP_DUO_IDLE_MINUTES", 30))
TAKEOVER_ENV = "COOP_DUO_TAKEOVER"  # set for the stop a takeover runs: it may stop a stale holder's games

RULES = """1. Claiming and freeing are automatic (`scripts/coop_harness/duo_lock.py`): `coop_test.py duo`, `scenario`,
   `reload`, `repro`, `launch`, `play`, `profile`, `soak`, `quest-walk`, `affected --run` and the MCP's
   `duo_start` wait their turn in a FIFO queue, hold the duo while they run, and free it when they end.
   `scripts/duo.py run -- <command>` holds it around any command. Never edit this file: it is a view of `duo.json`.
2. One checkout holds it at a time; that checkout's other commands join the hold. Games left running
   (`duo --keep-running`) keep it held by a keeper until `coop_test.py stop`, the games end, or
   it idles (no command from the holder for 30 minutes: the keeper stops the games).
3. A holder whose process is gone is stale: the next in line stops its games and takes over. Nobody asks the user.
4. `scripts/duo.py status` shows the holder, the queue and their ages. Free the duo when Skyrim is not
   needed (bug-fix flow, CLAUDE.md): `coop_test.py stop`, or the MCP's `duo_stop`."""

TEMPLATE = "# Duo reservation\n\nThe two test Skyrims, shared by every agent and worktree.\n\n## Rules\n\n\n## Now\n\nfree\n\n" \
           "## Waiting\n\n\n## Log\n\n"


class DuoBusy(Exception):
    """The duo did not come free in time (or could not be taken over); the message says who holds it."""


def section(text, name):
    """The body of a `## name` section and its span in the text."""
    match = re.search(rf"^## {re.escape(name)}\n(.*?)(?=^## |\Z)", text, re.S | re.M)
    return (match.group(1).strip(), match.span(1)) if match else (None, None)


def with_section(text, name, body):
    _, span = section(text, name)
    if span is None:
        return text.rstrip("\n") + f"\n\n## {name}\n\n{body}\n\n"
    return text[:span[0]] + f"\n{body}\n\n" + text[span[1]:]


def with_log(text, line):
    """The file with a line first under Log."""
    if section(text, "Log")[1] is None:
        text = with_section(text, "Log", "")
    _, span = section(text, "Log")
    body = text[span[0]:span[1]].strip("\n")
    return text[:span[0]] + f"\n- {line}\n" + (f"\n{body}\n\n" if body else "\n") + text[span[1]:]


class Token:
    """A file this process keeps flock'ed: free once the process ends, however it ends (O_CLOEXEC: children never
    inherit it)."""

    def __init__(self, folder):
        folder.mkdir(parents=True, exist_ok=True)
        self.path = folder / f"{uuid.uuid4().hex}.lock"
        self.fd = os.open(self.path, os.O_CREAT | os.O_RDWR | os.O_CLOEXEC, 0o664)
        fcntl.flock(self.fd, fcntl.LOCK_EX)

    def close(self):
        if self.fd is not None:
            with contextlib.suppress(FileNotFoundError):
                self.path.unlink()
            os.close(self.fd)
            self.fd = None


def alive(path):
    """Whether a token's process still holds it."""
    if not path:
        return False
    try:
        fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
    except FileNotFoundError:
        return False
    try:
        fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
        return False
    except BlockingIOError:
        return True
    finally:
        os.close(fd)


def wait_any(paths, timeout):
    """Blocks until one of these tokens is freed, or the timeout: True when one was."""
    done = threading.Event()

    def watch(path):
        try:
            fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
        except FileNotFoundError:
            done.set()
            return
        try:
            fcntl.flock(fd, fcntl.LOCK_SH)
        finally:
            os.close(fd)
            done.set()

    for path in paths:
        threading.Thread(target=watch, args=(path,), daemon=True).start()
    return done.wait(timeout) if paths else True


def stamp(seconds):
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(seconds))


def age(seconds):
    minutes = int((time.time() - seconds) // 60)
    return f"{minutes // 60}h{minutes % 60:02d}m" if minutes >= 60 else f"{minutes}m"


def run_stop(command, worktree):
    """The stop path of a holder's checkout (coop_test.py stop), allowed to stop a stale holder's games."""
    if not command:
        return True
    try:
        result = subprocess.run(command, cwd=worktree if Path(worktree).is_dir() else None, timeout=300,
                                env={**os.environ, TAKEOVER_ENV: "1"}, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


class DuoLock:
    """One process's side of the lock: its hold, join or queue place (role) and the token behind it."""

    def __init__(self, home, worktree, name, stop_command=None, stop=run_stop, out=sys.stderr):
        self.home, self.worktree, self.name = Path(home), str(worktree), name
        self.state_file, self.mutex_file = self.home / "duo.json", self.home / "duo.lock"
        self.tokens, self.view = self.home / "duo-tokens", self.home / "DUO.md"
        self.stop_command = stop_command or []
        self.stop, self.out = stop, out
        self.token, self.role, self.id = None, None, None

    # --- State ---

    @contextlib.contextmanager
    def _state(self, write=True):
        self.home.mkdir(parents=True, exist_ok=True)
        fd = os.open(self.mutex_file, os.O_CREAT | os.O_RDWR | os.O_CLOEXEC, 0o664)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
            try:
                state = json.loads(self.state_file.read_text())
            except (FileNotFoundError, ValueError):
                state = {}
            state.setdefault("holder", None)
            state.setdefault("queue", [])
            state.setdefault("log", [])
            self._prune(state)
            yield state
            if write:
                self._write(self.state_file, json.dumps({**state, "log": []}, indent=1))
                self._render(state)
        finally:
            os.close(fd)

    @staticmethod
    def _write(path, text):
        target = path.resolve()
        temp = target.with_name(f".{target.name}.{os.getpid()}")
        temp.write_text(text)
        os.replace(temp, target)

    def _render(self, state):
        """DUO.md from the state: Rules, Now and Waiting rewritten, new Log lines first."""
        try:
            text = self.view.read_text()
        except FileNotFoundError:
            text = TEMPLATE
        text = with_section(text, "Rules", RULES)
        text = with_section(text, "Now", self.describe(state["holder"]) if state["holder"] else "free")
        text = with_section(text, "Waiting", "\n".join(f"{index}. {self.describe(entry)}"
                                                       for index, entry in enumerate(state["queue"], 1)))
        for line in state["log"]:
            text = with_log(text, line)
        state["log"] = []
        self._write(self.view, text)

    @staticmethod
    def describe(entry):
        joined = len(entry.get("joined", []))
        return (f"{entry['name']} (worktree {entry['worktree']}), pid {entry['pid']}, since {stamp(entry['since'])}"
                f" ({age(entry['since'])}), {entry['what']}" + (" [keeper]" if entry.get("keeper") else "")
                + (f" +{joined} joined" if joined else ""))

    @staticmethod
    def hold_tokens(holder):
        return [holder["token"]] + [join["token"] for join in holder.get("joined", [])]

    def hold_alive(self, holder):
        return any(alive(token) for token in self.hold_tokens(holder))

    @staticmethod
    def _prune(state):
        state["queue"] = [entry for entry in state["queue"] if alive(entry["token"])]
        if holder := state["holder"]:
            holder["joined"] = [join for join in holder.get("joined", []) if alive(join["token"])]

    def _entry(self, what):
        return {"id": self.id, "name": self.name, "worktree": self.worktree, "pid": os.getpid(),
                "since": time.time(), "what": what, "token": str(self.token.path), "stop": self.stop_command}

    def _log(self, state, holder, summary):
        state["log"].append(f"{stamp(holder['since'])} to {time.strftime('%H:%M')} {holder['name']} "
                            f"({holder['worktree']}): {holder['what']}. {summary}")

    # --- Queue, hold, join ---

    def status(self):
        """The holder and the queue, each with whether its process lives."""
        with self._state(write=False) as state:
            holder = state["holder"]
            return {"holder": holder and {**holder, "alive": self.hold_alive(holder)},
                    "queue": [{**entry, "alive": alive(entry["token"])} for entry in state["queue"]]}

    def acquire(self, what, wait=True, timeout=None, keep_place=False):
        """Holds the duo, or joins this checkout's hold: "holder" or "joined". Waits its turn in the queue up to
        timeout seconds (None: no limit); then DuoBusy, keeping its place if keep_place (a later call goes on)."""
        if self.role in ("holder", "joined"):
            self.touch()
            return self.role
        if self.token is None:
            self.token, self.id = Token(self.tokens), uuid.uuid4().hex[:12]
        deadline = None if timeout is None else time.monotonic() + timeout
        shown = None
        while True:
            failure = None
            with self._state() as state:
                holder, queue = state["holder"], state["queue"]
                place = next((index for index, entry in enumerate(queue) if entry["id"] == self.id), None)
                held = holder and self.hold_alive(holder)
                if held and holder["worktree"] == self.worktree:
                    state["queue"] = [entry for entry in queue if entry["id"] != self.id]
                    holder.setdefault("joined", []).append({"id": self.id, "pid": os.getpid(), "what": what,
                                                            "token": str(self.token.path)})
                    holder["last_used"] = time.time()
                    self.role = "joined"
                    return self.role
                if not held and (place == 0 or (place is None and not queue)):
                    if holder and holder["worktree"] != self.worktree:
                        self.out.write(f"duo: {holder['name']}'s hold is stale (its process ended): stopping its "
                                       "games and taking over\n")
                        command = holder.get("stop") if Path(holder["worktree"]).is_dir() else self.stop_command
                        if not self.stop(command, holder["worktree"]):
                            failure = f"the stale holder's games would not stop: {self.describe(holder)}"
                        else:
                            self._log(state, holder, f"Stale (process ended); games stopped, taken over by {self.name}")
                    elif holder:
                        self._log(state, holder, "Process ended; this checkout took it back")
                    if not failure:
                        state["queue"] = [entry for entry in queue if entry["id"] != self.id]
                        state["holder"] = {**self._entry(what), "joined": [], "last_used": time.time()}
                        self.role = "holder"
                        return self.role
                if place is None:
                    queue.append(self._entry(what))
                    place = len(queue) - 1
                ahead = self.hold_tokens(holder) if place == 0 and holder else [queue[place - 1]["token"]] \
                    if place else []
                self.role = "queued"
                message = (f"duo: held by {self.describe(holder)}" if holder else "duo: free, others first") + \
                          f"; {self.name} is number {place + 1} in the queue"
            if failure:
                self.leave()
                raise DuoBusy(failure)
            remaining = None if deadline is None else deadline - time.monotonic()
            if not wait or (remaining is not None and remaining <= 0):
                if not keep_place:
                    self.leave()
                raise DuoBusy(message)
            if message != shown:
                self.out.write(message + (f", waiting up to {remaining:.0f} s\n" if remaining is not None else "\n"))
                self.out.flush()
                shown = message
            wait_any(ahead, remaining)

    def touch(self):
        with self._state() as state:
            if state["holder"] and state["holder"]["worktree"] == self.worktree:
                state["holder"]["last_used"] = time.time()

    def leave(self):
        """Gives up this process's queue place or join (not a hold: release)."""
        if self.role in ("queued", "joined"):
            with self._state() as state:
                state["queue"] = [entry for entry in state["queue"] if entry["id"] != self.id]
                if holder := state["holder"]:
                    holder["joined"] = [join for join in holder.get("joined", []) if join["id"] != self.id]
        self._drop()

    def release(self, summary="released"):
        """Frees this process's hold with a log line (or leaves its join or queue place)."""
        if self.role != "holder":
            return self.leave()
        with self._state() as state:
            if (holder := state["holder"]) and holder["id"] == self.id:
                self._log(state, holder, summary)
                state["holder"] = None
        self._drop()

    def release_checkout(self, summary):
        """`stop` from the holding checkout: frees a keeper's hold (ending the keeper) or a stale one. A live
        process's hold (an MCP session) stays its own. True when it freed one."""
        with self._state() as state:
            holder = state["holder"]
            if not holder or holder["worktree"] != self.worktree or \
                    (self.hold_alive(holder) and not holder.get("keeper")):
                return False
            self._log(state, holder, summary)
            state["holder"] = None
        if holder.get("keeper") and alive(holder["token"]):
            with contextlib.suppress(ProcessLookupError):
                os.kill(holder["pid"], signal.SIGTERM)
        return True

    def _drop(self):
        if self.token:
            self.token.close()
        self.token, self.role, self.id = None, None, None

    # --- Keeping a hold after its process ends (games left running) ---

    def park(self, keeper_command, timeout=30):
        """Hands this process's hold to a keeper process (keeper_command + [hold id]) and lets go. False if the
        keeper did not take it (the hold is still this process's)."""
        if self.role != "holder":
            return False
        log = (self.home / "duo-keeper.log").open("a")
        process = subprocess.Popen(keeper_command + [self.id], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=log, start_new_session=True)
        log.close()
        ready, _, _ = select.select([process.stdout], [], [], timeout)
        if not ready or process.stdout.readline().strip() != b"ready":
            process.kill()
            return False
        process.stdout.close()
        self._drop()
        return True

    def adopt(self, hold_id):
        """The keeper's side of park: takes over the hold with its own token."""
        token = Token(self.tokens)
        with self._state() as state:
            holder = state["holder"]
            if not holder or holder["id"] != hold_id:
                token.close()
                return False
            holder.update(pid=os.getpid(), token=str(token.path), keeper=True, last_used=time.time())
        self.token, self.id, self.role = token, hold_id, "holder"
        return True

    def keep(self, hold_id, games_running, check_seconds=30, idle_minutes=IDLE_MINUTES, ready=None):
        """The keeper: holds the duo while its games run, until stopped (SIGTERM), the games end, or idle."""
        if not self.adopt(hold_id):
            return False
        ended = threading.Event()
        if threading.current_thread() is threading.main_thread():
            signal.signal(signal.SIGTERM, lambda *_: ended.set())
        if ready:
            ready()
        quiet = 0
        while not ended.wait(check_seconds):
            with self._state(write=False) as state:
                holder = state["holder"]
                if not holder or holder["id"] != self.id:
                    break
                joined = any(alive(join["token"]) for join in holder.get("joined", []))
                idle = time.time() - holder.get("last_used", holder["since"])
            if joined:
                quiet = 0
            elif games_running():
                quiet = 0
                if idle > idle_minutes * 60:
                    stopped = self.stop(holder.get("stop"), holder["worktree"])
                    self.release(f"Idle {idle / 60:.0f} min; keeper {'stopped its games' if stopped else 'could not stop its games'}")
                    return True
            else:
                quiet += 1
                if quiet >= 2:
                    self.release("Its games ended; keeper freed it")
                    return True
        self.release("Stopped; keeper freed it")
        return True
