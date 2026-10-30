"""Scenarios mounts for the co-op harness."""


def scenario_guest_rides_host_horse(ctx):
    ctx.mount_roundtrip()

def scenario_host_rides_horse(ctx):
    ctx.mount_roundtrip("")

def scenario_guest_rides_across_border(ctx):
    ctx.mount_roundtrip(cross_border=True)

def scenario_mounted_guest_leaves(ctx):
    ctx.mount_roundtrip(leave=True)

def scenario_party_travel_mounted_guest(ctx):
    ctx.mount_roundtrip(follow=True)
