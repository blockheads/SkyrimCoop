"""Watches one actor's voice line (mod.voice) in the background: each sound its voice handle takes is a line started.

Sampled, not an engine record: two starts between reads show only the last, and a skip between reads could look like
an overlap (docs/re/systems/dialogue.md, "A guest leaving mid-line").
"""
import threading
import time

# Voice time the line before still had at its last read, above which a new sound counts as layered over it.
LAYERED_SECONDS = 0.5


class VoiceWatch:
    def __init__(self, read, interval=0.05):
        """read(): the actor's voice now ({sound, seconds, ...}); may raise while its game is busy."""
        self._read = read
        self._interval = interval
        self._stop = threading.Event()
        first = read()
        self.rows = [first]
        self.sounds = [first.get("sound", -1)]
        self._last = first
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.wait(self._interval):
            try:
                row = self._read()
            except Exception:  # a game busy loading or saving answers late: the next read
                continue
            if row.get("sound", -1) not in (-1, self.sounds[-1]):
                self.sounds.append(row["sound"])
                self.rows.append({**row, "at": time.monotonic(), "before_left": self._last.get("seconds", -1)})
            self._last = row

    def stop(self):
        """Stops watching and waits for the watcher: rows and sounds are final after it."""
        self._stop.set()
        self._thread.join()
        return {"sounds": self.sounds, "rows": self.rows}


def layered(rows):
    """The lines started while the one before still had voice time left."""
    return [row for row in rows[1:] if row.get("before_left", -1) > LAYERED_SECONDS]
