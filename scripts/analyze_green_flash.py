"""Rank green-pixel changes in every recorded frame; candidates need visual confirmation."""
import argparse
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageChops


def green_pixels(frame):
    red, green, blue = frame.split()
    excess = ImageChops.darker(ImageChops.subtract(green, red), ImageChops.subtract(green, blue))
    mask = ImageChops.darker(excess.point(lambda value: 255 if value >= 35 else 0),
                            green.point(lambda value: 255 if value >= 80 else 0))
    return mask.histogram()[255]


def analyze(path, output, keep=8):
    output.mkdir(parents=True, exist_ok=True)
    info = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_streams', '-show_frames',
        '-show_entries', 'stream=width,height,avg_frame_rate:frame=best_effort_timestamp_time',
        '-of', 'json', str(path)]))
    stream = info['streams'][0]
    width, height = stream['width'], stream['height']
    times = [float(frame['best_effort_timestamp_time']) for frame in info['frames']]
    if not times:
        raise ValueError('video has no timestamped frames to analyze')
    process = subprocess.Popen([
        'ffmpeg', '-nostdin', '-v', 'error', '-threads', '1', '-i', str(path), '-map', '0:v:0',
        '-vsync', '0', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'], stdout=subprocess.PIPE)
    rows, candidates = [], []
    previous = 0
    try:
        while data := process.stdout.read(width * height * 3):
            if len(data) != width * height * 3:
                raise RuntimeError('partial decoded video frame')
            frame = Image.frombytes('RGB', (width, height), data)
            # Keep HUD colour changes separate from the world view.
            world = frame.crop((0, round(height * .12), width, round(height * .84)))
            count = green_pixels(world)
            row = {'frame': len(rows), 'time': times[len(rows)], 'world_green_pixels': count,
                   'whole_frame_green_pixels': green_pixels(frame), 'increase': count - previous}
            rows.append(row)
            previous = count
            if count:
                candidates.append((max(count, row['increase']), row, frame))
                candidates.sort(key=lambda candidate: candidate[0], reverse=True)
                del candidates[keep:]
        if process.wait() or len(rows) != len(times):
            raise RuntimeError(f'decoded {len(rows)} of {len(times)} frames')
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.terminate()
            process.wait()
    saved = []
    for _, row, frame in candidates:
        name = f"candidate-{row['frame']:06d}.png"
        frame.save(output / name)
        saved.append({**row, 'image': name})
    result = {'video': str(path.resolve()), 'stream': stream, 'frames_analyzed': len(rows),
              'max_world_green_pixels': max((row['world_green_pixels'] for row in rows), default=0),
              'candidates': saved, 'frames': rows,
              'interpretation': 'Colour candidates only; neither a reproduction nor proof the bug is absent.'}
    (output / 'analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    return {key: value for key, value in result.items() if key != 'frames'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('video', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(analyze(args.video, args.output), indent=2))
