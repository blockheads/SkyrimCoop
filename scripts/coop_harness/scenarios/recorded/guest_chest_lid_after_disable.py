"""The guest opens a chest, then the host disables and enables it (its menu closes on its own): both games show the chest's lid at the same pose afterwards.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261118-122645-guest-chest-lid-after-disable.jsonl, 13 calls (scripts/check_scenarios.py)
NAME = 'guest-chest-lid-after-disable'
TAGS = ['menus', 'physics', 'v1']
DOC = "The guest opens a chest, then the host disables and enables it (its menu closes on its own): both games show the chest's lid at the same pose afterwards."


def run(ctx):
    try:
        ctx.name_handle('chest', {'host': 797928, 'guest': 797928})
        ctx.stage({'players': {'guest': {'near': 'chest', 'gap': 100}}})
        ctx.use('client', 'chest')
        ctx.console_on('', 'C2CE8.disable')
        # Edited: the enable waits for the guest's menu to close on its own (the lid's Close starts disabled).
        ctx.wait_for("[client] the chest's menu closes when the chest is disabled",
                     lambda: "ContainerMenu" not in ctx.menus("client"), 5)
        ctx.console_on('', 'C2CE8.enable')
        ctx.expect('chest', 'agree', within=5.0, seconds=0.0)
    finally:
        if "ContainerMenu" in ctx.menus("client"):
            ctx.player_command("client", {"cmd": "menu", "name": "ContainerMenu", "show": False})
        ctx.console_on('', 'C2CE8.enable')
        ctx.unstage()
