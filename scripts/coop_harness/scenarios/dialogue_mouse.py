"""Mouse input for Skyrim's recentered dialogue list; clip slots are not option indices."""

LIST = '_root.DialogueMenu_mc.TopicList.'


def value(ctx, instance, path):
    return ctx.menu_var(instance, 'Dialogue Menu', LIST + path)


def rows(ctx, instance):
    result = []
    for slot in range(9):
        prefix = f'Entry{slot}.'
        if value(ctx, instance, prefix + '_visible') and (index := value(ctx, instance, prefix + 'itemIndex')) is not None:
            result.append({'slot': slot, 'index': index,
                           **{key: value(ctx, instance, prefix + key)
                              for key in ('_xmouse', '_ymouse', '_width', '_height', '_currentframe', 'textField.text')}})
    return result


def calibrate(ctx, instance):
    samples = []
    for point in [(100, 100), (132, 132)]:
        ctx.move_cursor(instance, point)
        ctx.held_a_frame(instance)
        samples.append([value(ctx, instance, 'Entry4.' + axis) for axis in ('_xmouse', '_ymouse')])
    if any(v is None for sample in samples for v in sample):
        raise ctx.failure(f'dialogue mouse coordinates unavailable: {samples}')
    delta = [samples[1][axis] - samples[0][axis] for axis in range(2)]
    if any(abs(v) < 1 for v in delta):
        raise ctx.failure(f'dialogue movie did not observe cursor movement: {samples}')
    return [32 / v for v in delta]


def hover(ctx, instance, index, scale, settle=True):
    visible = rows(ctx, instance)
    row = next((row for row in visible if row['index'] == index), None)
    if row is None:
        raise ctx.failure(f'dialogue option {index} has no visible clip: {visible}')
    cursor = ctx.game_state(instance)['cursor']
    local = [row['_xmouse'], row['_ymouse']]
    size = [row['_width'], row['_height']]
    point = [round(cursor[axis] + (size[axis] / 2 - local[axis]) * scale[axis]) for axis in range(2)]
    ctx.move_cursor(instance, point)
    if settle:
        ctx.wait_for(f'[{instance or "host"}] mouse highlights option {index}',
                     lambda: ctx.cursor_index(instance) == index, 5)
    return {'row': row, 'point': point, 'selected': ctx.cursor_index(instance)}
