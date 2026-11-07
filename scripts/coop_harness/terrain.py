"""The ground around a spot as a walking player meets it, from the harness's `terrain` scan (docs/re/systems/terrain.md).

The scan gives, per grid cell, every surface a downward ray met (z, normal z, reference, layer, headroom over the
standable ones) and the navmesh drawn into the same grid. Here: which floors the player can walk to from where it
stands (a flood from its cell, a step allowed when no steeper than its controller's max slope), and two text views,
top-down and a side profile toward a target. Pure functions: the scan is plain JSON.
"""
import collections
import math

DEFAULT_SLOPE = 50.0      # degrees, when the controller is not a proxy one (scan capsule max_slope 0)
EDGE_ALLOWANCE = 10.0     # units a capsule's round bottom rides over at any slope
DEFAULT_HEADROOM = 120.0  # units a player needs above a floor, when the scan has no capsule
STANDABLE = 0.5           # a surface's normal z from which it is ground (60 degrees)
RAMP = math.tan(math.radians(8))  # a floor rising more than this a cell reads as a ramp
PIECES = "abcdefghijklmnopqrstuvwxyz"
LEGEND = ("@ player  T target  O obstacle  a b c.. walkable on navmesh piece  . walkable off the navmesh  "
          "/ walkable ramp  # wall (too steep up)  v drop (too steep down)  ^ too low to stand  "
          "A B C.. navmesh not reached  , floor not reached  ? nothing hit")


def cell_of(scan, point):
    """(row, col) of a point, or None outside the grid."""
    col = int((point[0] - scan["west"]) // scan["cell"])
    row = int((scan["north"] - point[1]) // scan["cell"])
    return (row, col) if 0 <= row < scan["rows"] and 0 <= col < scan["cols"] else None


def centre(scan, row, col):
    return (scan["west"] + (col + 0.5) * scan["cell"], scan["north"] - (row + 0.5) * scan["cell"])


def limits(scan):
    """The step a cell's floor may rise or fall to its neighbour's, and the headroom a floor needs."""
    capsule = scan.get("capsule") or {}
    slope = capsule.get("max_slope") or DEFAULT_SLOPE
    headroom = capsule.get("actor_height") or DEFAULT_HEADROOM
    return scan["cell"] * math.tan(math.radians(slope)) + EDGE_ALLOWANCE, headroom


def standable(scan, row, col, headroom):
    """The floors in that cell's column a player fits on: ground facing up with room above (-1: none within the check)."""
    return [hit[0] for hit in scan["columns"][row * scan["cols"] + col]
            if hit[1] > STANDABLE and (hit[4] < 0 or hit[4] >= headroom)]


def walk(scan, parents=None):
    """{(row, col): floor z} the player reaches from its cell, neighbour by neighbour, each step within the limit;
    and {(row, col): why} for the cells next to those it can't step onto ("wall", "drop", "low", "nothing").
    `parents` (a dict) gets each reached cell's step back toward the player."""
    step, headroom = limits(scan)
    start = cell_of(scan, scan["player"])
    reached, blocked = {}, {}
    if not start:
        return reached, blocked
    floors = standable(scan, *start, 0) or [scan["player"][2]]  # where it stands, whatever is overhead
    reached[start] = min(floors, key=lambda z: abs(z - scan["player"][2]))
    queue = collections.deque([start])
    while queue:
        row, col = queue.popleft()
        here = reached[(row, col)]
        for near in ((row - 1, col), (row + 1, col), (row, col - 1), (row, col + 1)):
            if not (0 <= near[0] < scan["rows"] and 0 <= near[1] < scan["cols"]) or near in reached:
                continue
            floors = standable(scan, *near, headroom)
            fits = [z for z in floors if abs(z - here) <= step]
            if fits:
                reached[near] = min(fits, key=lambda z: abs(z - here))
                if parents is not None:
                    parents[near] = (row, col)
                blocked.pop(near, None)
                queue.append(near)
            elif near not in blocked:
                blocked[near] = why_blocked(scan, near, here, step, floors)
    return reached, blocked


def on_navmesh(scan, cell, z):
    """Whether that reached cell's floor is the navmesh's (a triangle under it at its height)."""
    navmesh = scan.get("navmesh") or {}
    index = cell[0] * scan["cols"] + cell[1]
    pieces, navz = navmesh.get("piece") or [], navmesh.get("z") or []
    return index < len(pieces) and pieces[index] >= 0 and navz[index] is not None and abs(navz[index] - z) < 60


def route_to_navmesh(scan, inside=1):
    """The walkable way from the player to the nearest navmesh floor, `inside` cells onto it: [x, y, z] cell centres
    (the player's own cell first), or None when no navmesh is walkable from here."""
    parents = {}
    reached, _ = walk(scan, parents)
    start = cell_of(scan, scan["player"])
    order = sorted(reached, key=lambda cell: (steps(parents, cell), math.dist(cell, start)))
    goal = next((cell for cell in order if on_navmesh(scan, cell, reached[cell])
                 and all(on_navmesh(scan, near, reached[near]) for near in neighbours(cell, inside) if near in reached)), None)
    if goal is None:
        return None
    cells = [goal]
    while cells[-1] in parents:
        cells.append(parents[cells[-1]])
    return [[*centre(scan, *cell), reached[cell]] for cell in reversed(cells)]


def steps(parents, cell):
    count = 0
    while cell in parents:
        cell, count = parents[cell], count + 1
    return count


def neighbours(cell, reach):
    return [(cell[0] + dr, cell[1] + dc) for dr in range(-reach, reach + 1) for dc in range(-reach, reach + 1) if dr or dc]


def why_blocked(scan, cell, here, step, floors):
    hits = scan["columns"][cell[0] * scan["cols"] + cell[1]]
    if not hits:
        return "nothing"
    if not floors and any(abs(hit[0] - here) <= step for hit in hits if hit[1] > STANDABLE):
        return "low"
    nearest = min((hit[0] for hit in hits), key=lambda z: abs(z - here))
    return "wall" if nearest > here else "drop"


def top_view(scan):
    """North up, one character a cell (LEGEND), rows of text."""
    reached, blocked = walk(scan)
    navmesh = scan.get("navmesh") or {}
    pieces, navz = navmesh.get("piece") or [], navmesh.get("z") or []
    rows = []
    for row in range(scan["rows"]):
        line = []
        for col in range(scan["cols"]):
            index = row * scan["cols"] + col
            piece = pieces[index] if index < len(pieces) else -1
            on_mesh = piece >= 0 and navz[index] is not None
            if (row, col) in reached:
                z = reached[(row, col)]
                ramp = any(  # sloping through it: one neighbour higher, the opposite one lower
                    a in reached and b in reached and (reached[a] - z) * (z - reached[b]) > (RAMP * scan["cell"]) ** 2
                    for a, b in (((row - 1, col), (row + 1, col)), ((row, col - 1), (row, col + 1))))
                if on_mesh and abs(navz[index] - z) < 60:
                    line.append(PIECES[piece % 26])
                else:
                    line.append("/" if ramp else ".")
            elif (row, col) in blocked:
                line.append({"wall": "#", "drop": "v", "low": "^", "nothing": "?"}[blocked[(row, col)]])
            elif on_mesh:
                line.append(PIECES[piece % 26].upper())
            else:
                line.append("?" if not scan["columns"][index] else "," if standable(scan, row, col, 0) else " ")
        rows.append(line)
    marks = [(o["at"], "O") for o in scan.get("obstacles") or []]
    marks += [((scan.get("toward") or {}).get("at"), "T"), (scan["player"], "@")]
    for at, mark in marks:
        if at and (cell := cell_of(scan, at)):
            rows[cell[0]][cell[1]] = mark
    return ["".join(line) for line in rows]


def side_view(scan, samples=None, band=None):
    """The ground along the line from the player to the target: one column a cell's length, one row `band` units
    high, top row highest. '=' a floor reached, 'x' a floor not reached, '#' above a floor within its headroom."""
    toward = (scan.get("toward") or {}).get("at")
    if not toward:
        return []
    reached, _ = walk(scan)
    step, headroom = limits(scan)
    start, end = scan["player"], toward
    length = math.dist(start[:2], end[:2])
    samples = samples or max(2, int(length // scan["cell"]) + 1)
    band = band or scan["cell"] / 2
    column, last = [], start[2]
    for i in range(samples):
        t = i / (samples - 1)
        point = (start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t)
        cell = cell_of(scan, point)
        if not cell:
            column.append(None)
            continue
        if cell in reached:
            floor, kind = reached[cell], "="
        else:
            floors = standable(scan, *cell, 0) or [hit[0] for hit in scan["columns"][cell[0] * scan["cols"] + cell[1]]]
            floor, kind = (min(floors, key=lambda z: abs(z - last)), "x") if floors else (None, "?")
        hits = scan["columns"][cell[0] * scan["cols"] + cell[1]]
        ceiling = min((hit[0] for hit in hits if floor is not None and hit[0] > floor + 4), default=None)
        column.append((floor, kind, ceiling))
        last = floor if floor is not None else last
    zs = [c[0] for c in column if c and c[0] is not None] + [start[2], end[2]]
    top, bottom = max(zs) + band, min(zs) - band
    rows = []
    for level in range(int((top - bottom) // band) + 1):
        z_hi = top - level * band
        line = []
        for i, c in enumerate(column):
            mark = " "
            if c and c[0] is not None:
                floor, kind, ceiling = c
                if z_hi - band < floor <= z_hi:
                    mark = kind
                elif ceiling is not None and floor < z_hi - band and ceiling <= z_hi and ceiling > z_hi - band:
                    mark = "#"
            line.append(mark)
        rows.append(f"z {z_hi:6.0f} " + "".join(line))
    for z, mark, index in ((start[2], "@", 0), (end[2], "T", len(column) - 1)):
        level = int((top - z) // band)
        if 0 <= level < len(rows):
            prefix, body = rows[level][:9], list(rows[level][9:])
            body[index] = mark
            rows[level] = prefix + "".join(body)
    return rows


def summary(scan):
    """One line: what the player can walk to, and what stops it short of the target."""
    reached, blocked = walk(scan)
    navmesh = scan.get("navmesh") or {}
    line = f"walkable from here: {len(reached)} cells of {scan['cell']:.0f} units"
    edges = [f"{n} {why}" for why in ("wall", "drop", "low") if (n := sum(1 for w in blocked.values() if w == why))]
    if edges:
        line += " (edges: " + ", ".join(edges) + ")"
    toward = (scan.get("toward") or {}).get("at")
    if toward and (target := cell_of(scan, toward)):
        step, _ = limits(scan)
        if target in reached and abs(reached[target] - toward[2]) <= step:  # its floor, not one stacked over it
            line += "; the target is walkable from here"
        elif reached:
            near = min(reached, key=lambda cell: math.dist(centre(scan, *cell), toward[:2]))
            gap = math.dist(centre(scan, *near), toward[:2])
            line += f"; the target is not: the nearest walkable cell is {gap:.0f} units from it"
    if navmesh.get("player", -1) >= 0:
        line += f"; navmesh: player on piece {PIECES[navmesh['player'] % 26]}"
        if navmesh.get("target", -1) >= 0:
            line += f", target on {PIECES[navmesh['target'] % 26]}"
    return line
