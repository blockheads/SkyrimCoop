"""A player knocked down or frozen (Ice Form's paralysis) gets vanilla's down camera (BleedoutCameraState 50714): its own clock at 0.8 and the Slow Time loop. The shared clock carried that 0.8 to the other game for as long as the player lay, re-pitching its sound (kTimeSensitiveSoundCategory, 35125). Each game's player pushed down in turn: its clock slows, the other's stays at 1.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261124-232625-guest-down-keeps-host-clock.jsonl, 3 calls (scripts/check_scenarios.py)
NAME = 'down-camera-clock-stays-own'
TAGS = ['clock', 'physics']
DOC = "A player knocked down or frozen (Ice Form's paralysis) gets vanilla's down camera (BleedoutCameraState 50714): its own clock at 0.8 and the Slow Time loop. The shared clock carried that 0.8 to the other game for as long as the player lay, re-pitching its sound (kTimeSensitiveSoundCategory, 35125). Each game's player pushed down in turn: its clock slows, the other's stays at 1."


def run(ctx):
    try:
        ctx.down_camera_clock_stays_own(instance='client')
        ctx.down_camera_clock_stays_own(instance='')
    finally:
        ctx.unstage()
