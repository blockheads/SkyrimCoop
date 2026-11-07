"""Playthrough flows (docs/design/playthrough-flows.md): one module per flow, with NAME, DOC, BOOT and STEPS."""
import importlib
import pkgutil


def flows():
    """{name: module} of every flow."""
    found = {}
    for info in pkgutil.iter_modules(__path__):
        if info.name != "flow":
            module = importlib.import_module(f"{__name__}.{info.name}")
            found[module.NAME] = module
    return found
