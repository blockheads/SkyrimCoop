"""Read meh321 Address Library formats 2 and 5 with optional runtime validation."""
from pathlib import Path
import struct


def load(path, expected_version=None):
    data = Path(path).read_bytes()
    pos = 0

    def read(fmt):
        nonlocal pos
        value = struct.unpack_from("<" + fmt, data, pos)[0]
        pos += struct.calcsize("<" + fmt)
        return value

    try:
        fmt = read("i")
        version = ".".join(str(read("I")) for _ in range(4))
        if expected_version and version != expected_version:
            raise ValueError(f"runtime {version} does not match {expected_version}")
        if fmt == 5:
            name = data[pos:pos + 64].split(b"\0", 1)[0]
            pos += 64
            pointer_size, data_format, count = read("i"), read("i"), read("i")
            if name != b"SkyrimSE.exe" or pointer_size != 8 or data_format != 0 or count <= 0 or len(data) != pos + 4 * count:
                raise ValueError("invalid v5 header or offset count")
            return {i: row[0] for i, row in enumerate(struct.iter_unpack("<I", data[pos:])) if row[0]}
        if fmt != 2:
            raise ValueError(f"unsupported Address Library format {fmt}")
        length = read("i")
        if not 0 < length <= 256 or data[pos:pos + length] != b"SkyrimSE.exe":
            raise ValueError("invalid module name")
        pos += length
        pointer_size, count = read("i"), read("i")
        if pointer_size != 8 or count <= 0 or count > len(data) - pos:
            raise ValueError("invalid pointer size or entry count")
        previous_id = previous_offset = 0
        ids = {}

        def delta(kind, previous):
            if kind == 0: return read("Q")
            if kind == 1: return previous + 1
            if kind in (2, 3, 4, 5):
                difference = read("B" if kind < 4 else "H")
                return previous + (difference if kind % 2 == 0 else -difference)
            if kind in (6, 7): return read("H" if kind == 6 else "I")
            raise ValueError("invalid delta kind")

        for _ in range(count):
            kind = read("B")
            ident = delta(kind & 15, previous_id)
            scale = pointer_size if kind & 128 else 1
            offset = delta((kind >> 4) & 7, previous_offset // scale) * scale
            if ident < 0 or offset < 0 or ident in ids:
                raise ValueError("invalid or duplicate record")
            if offset:
                ids[ident] = offset
            previous_id, previous_offset = ident, offset
        return ids
    except (struct.error, IndexError, KeyError) as error:
        raise ValueError(f"{path}: truncated or invalid address library") from error
