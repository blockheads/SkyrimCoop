"""A physics object's motion as numbers: a flight reads clean, a teleport and a shake show."""
import unittest

from coop_harness import collision_metrics as metrics


def flight(speed=120.0, frames=60, dt=1 / 60):
    """Slides along x at `speed`, then rests."""
    path = [(i * dt, [i * dt * speed, 0.0, 0.0], speed) for i in range(frames)]
    end = path[-1][1]
    return path + [((frames + i) * dt, end, 0.0) for i in range(10)]


class Measure(unittest.TestCase):
    def test_a_flight_has_no_jump(self):
        m = metrics.measure(flight())
        self.assertAlmostEqual(m["moved"], 118.0, delta=0.5)
        self.assertLess(m["jump"], 0.01)
        self.assertEqual(m["turns"], 0)
        self.assertAlmostEqual(m["peak"], 120.0)

    def test_two_havok_steps_in_one_frame_are_no_jump(self):
        path = flight()
        path[10] = (path[10][0], path[11][1], path[10][2])  # a frame with two steps
        self.assertLess(metrics.measure(path)["jump"], 0.01)

    def test_a_rest_placed_elsewhere_is_a_jump(self):
        path = flight()
        last = path[-1]
        path.append((last[0] + 1 / 60, [last[1][0] + 13.6, 0.0, 0.0], 0.0))
        self.assertAlmostEqual(metrics.measure(path)["jump"], 13.6, delta=0.1)

    def test_back_and_forth_counts_turns(self):
        path = [(i / 60, [(i % 2) * 2.0, 0.0, 0.0], 120.0) for i in range(10)]
        self.assertEqual(metrics.measure(path)["turns"], 8)


    def test_a_keyframed_body_moving_evenly_with_no_speed_read_has_no_jump(self):
        path = [(t, p, 0.0) for t, p, _ in flight()]
        m = metrics.measure(path)
        self.assertLess(m["jump"], 0.01)
        self.assertAlmostEqual(m["peak"], 120.0, delta=1.0)

    def test_a_snap_among_even_steps_is_still_a_jump(self):
        path = [(t, p, 0.0) for t, p, _ in flight()]
        path = path[:30] + [(t, [p[0] + 12.0, p[1], p[2]], s) for t, p, s in path[30:]]
        self.assertGreater(metrics.measure(path)["jump"], 7.0)


class Group(unittest.TestCase):
    def test_a_pile_reads_as_one_and_keeps_its_worst_jump(self):
        snapped = flight(110.0)
        snapped.append((snapped[-1][0] + 1 / 60, [snapped[-1][1][0] + 10.0, 0.0, 0.0], 0.0))
        still = [(i / 60, [0.0, 0.0, 0.0], 0.0) for i in range(10)]
        group = metrics.measure_group([metrics.measure(flight()), metrics.measure(snapped), metrics.measure(still), None])
        self.assertEqual(group["moved"], 2)
        self.assertAlmostEqual(group["jump"], 10.0, delta=0.1)


class Envelope(unittest.TestCase):
    def test_a_run_inside_vanillas_range_passes_and_a_snap_fails(self):
        runs = [metrics.measure(flight(speed)) for speed in (100.0, 120.0, 140.0)]
        ranges = metrics.envelope(runs)
        self.assertEqual(metrics.outside(metrics.measure(flight(110.0)), ranges), [])
        snapped = flight(110.0)
        snapped.append((snapped[-1][0] + 1 / 60, [snapped[-1][1][0] + 10.0, 0.0, 0.0], 0.0))
        self.assertTrue(any(line.startswith("jump") for line in metrics.outside(metrics.measure(snapped), ranges)))


class Rounds(unittest.TestCase):
    def setUp(self):
        self.ranges = {"0": metrics.envelope([metrics.measure(flight(speed)) for speed in (100.0, 120.0, 140.0)])}
        self.limit = metrics.limits(self.ranges)

    def test_one_odd_round_of_three_is_havok(self):
        rounds = [metrics.measure(flight(s)) for s in (110.0, 400.0, 120.0)]
        self.assertEqual(metrics.compare(rounds, self.ranges["0"], self.limit), [])

    def test_a_habit_fails(self):
        rounds = [metrics.measure(flight(s)) for s in (400.0, 410.0, 120.0)]
        self.assertTrue(any(line.startswith("peak") for line in metrics.compare(rounds, self.ranges["0"], self.limit)))

    def test_one_snap_in_any_round_fails(self):
        snapped = flight(110.0)
        snapped.append((snapped[-1][0] + 1 / 60, [snapped[-1][1][0] + 10.0, 0.0, 0.0], 0.0))
        rounds = [metrics.measure(flight()), metrics.measure(snapped), metrics.measure(flight())]
        self.assertTrue(any(line.startswith("jump") for line in metrics.compare(rounds, self.ranges["0"], self.limit)))


if __name__ == "__main__":
    unittest.main()


class SleptMoving(unittest.TestCase):
    @staticmethod
    def frames(*states):
        return [{"id": 7, "bodies": [{"awake": awake, "velocity": [speed, 0.0, 0.0]}]} for awake, speed in states]

    def test_a_falling_wheel_put_to_sleep_counts(self):
        self.assertEqual(metrics.slept_moving(self.frames((True, 0.0), (True, 90.0), (False, 0.0)), 7), 1)

    def test_havok_sleeping_a_still_body_does_not(self):
        self.assertEqual(metrics.slept_moving(self.frames((True, 40.0), (True, 1.0), (False, 0.0)), 7), 0)


class ObjectName(unittest.TestCase):
    def test_every_key_a_trial_reports_has_words(self):
        objects = ["64B33 FF000805", "64B33 FF000813"]
        self.assertEqual(metrics.object_name("released", objects), "the let go object")
        self.assertEqual(metrics.object_name("1", objects), "object 1 (64B33 FF000813)")
        self.assertEqual(metrics.object_name("later", objects), "later")
