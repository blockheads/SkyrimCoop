"""Scenarios exported from repro MCP sessions (docs/design/repro-mcp.md): one module per bug, NAME/TAGS/DOC and run."""
import importlib
import pkgutil


def modules():
    for info in pkgutil.iter_modules(__path__):
        yield importlib.import_module(f"{__name__}.{info.name}")
