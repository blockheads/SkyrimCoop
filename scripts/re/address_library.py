"""Read meh321 Address Library format 5 with optional runtime validation."""
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
        if fmt != 5:
            raise ValueError(f"unsupported Address Library format {fmt}")
        name = data[pos:pos + 64].split(b"\0", 1)[0]
        pos += 64
        pointer_size, data_format, count = read("i"), read("i"), read("i")
        if name != b"SkyrimSE.exe" or pointer_size != 8 or data_format != 0 or count <= 0 or len(data) != pos + 4 * count:
            raise ValueError("invalid v5 header or offset count")
        return {i: row[0] for i, row in enumerate(struct.iter_unpack("<I", data[pos:])) if row[0]}
    except (struct.error, IndexError, KeyError) as error:
        raise ValueError(f"{path}: truncated or invalid address library") from error
