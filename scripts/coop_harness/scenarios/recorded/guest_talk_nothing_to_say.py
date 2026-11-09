"""A guest talks to an NPC with nothing to say (Hilde in Riverwood: a hello, no topics): the host's game starts the ask, its engine opens no conversation, and the guest is told so instead of waiting on an answer that never comes (2026-11-08 playtest: the guest's asks went unanswered).

The host's own talk opens nothing with her either: vanilla, she only says hello."""
# hand-written: understood in the 2026-11-08 duo session (talk to Hilde timed out on both games, the host's game
# logged a start and nothing after); the check reads the guest's chat, which a recording cannot replay.
NAME = 'guest-talk-nothing-to-say'
TAGS = ['dialogue', 'scene']
DOC = "A guest talks to an NPC with nothing to say (Hilde in Riverwood): the host's game opens no conversation, and the guest is told so (2026-11-08 playtest: the guest's asks went unanswered)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.stage({'players': {'guest': {'near': 'hilde', 'gap': 110, 'bearing': 90},
                               'host': {'near': 'hilde', 'gap': 300, 'bearing': 270}}})
        ctx.activate_ref('client', ctx.resolve('hilde', 'guest'), 'hilde')
        ctx.wait_for('[client] the guest is told Hilde has nothing to say',
                     lambda: 'They have nothing to say.' in ctx.system_chat('client'), 10)
    finally:
        ctx.unstage()
