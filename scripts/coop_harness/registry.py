"""Canonical flow registration and fixture requirements, available without running games."""
from collections.abc import Mapping
from dataclasses import dataclass
import json
from typing import Callable

from .common import ROOT


@dataclass(frozen=True)
class Flow:
    name: str
    tags: frozenset[str]
    fixture: str
    run: Callable
    doc: str


class Registry(Mapping):
    def __init__(self):
        self._flows = {}

    def add(self, name, tags, run, *, fixture=None, doc=None):
        if name in self._flows:
            raise ValueError(f"duplicate flow: {name}")
        tags = frozenset(tags)
        fixture = fixture or ("saved-story" if "late-save" in tags else "baseline")
        if fixture not in ("baseline", "saved-story"):
            raise ValueError(f"unknown fixture: {fixture}")
        self._flows[name] = Flow(name, tags, fixture, run, doc or run.__doc__ or name)

    @staticmethod
    def specs():
        paths = sorted((ROOT / "Code/tests/specs").glob("*.json"))
        if not paths:
            raise ValueError("no shared specs found")
        return [json.loads(path.read_text()) for path in paths]

    def __getitem__(self, name):
        return self._flows[name]

    def __iter__(self):
        return iter(self._flows)

    def __len__(self):
        return len(self._flows)
