"""Failures retain the original checkpoint and evidence before fixture restoration."""
import time

class TestFailure(RuntimeError):
    def __init__(self, *args):
        super().__init__(*args)
        self.time = time.time()
        self.status = {}
        self.phase = None
        self.checkpoint = None
