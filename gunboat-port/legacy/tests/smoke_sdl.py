"""Exercise the real SDL host with dummy video/audio, including keyboard input."""
import json
import os
from pathlib import Path
import subprocess
from PIL import Image

PORT = Path(__file__).resolve().parents[2]
binary = PORT / "build/gunboat_legacy.exe"


def run(name, world, keys, seconds):
    directory = PORT / "out" / name
    directory.mkdir(parents=True, exist_ok=True)
    state = directory / "state.json"
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
               GB_SNAPSHOT_DIR=str(directory), GB_KEYS=keys)
    subprocess.run([str(binary), "--game-dir", str(PORT.parent / "Original DOS version"),
                    "--world", str(world), "--practice", "--seconds", str(seconds),
                    "--state", str(state), "--screenshot", str(directory / "final.bmp")], env=env, check=True, timeout=30)
    for path in directory.glob("*.bmp"):
        Image.open(path).save(path.with_suffix(".png"))
    return json.loads(state.read_text())


for world in range(1, 5):
    state = run(f"sdl-world{world}", world,
                "0.2:1c,0.3:11p,1.4:11r,1.5:39p,2:39r,2.1:3f,2.2:20p,2.7:20r", 4.3)
    assert state["world"] == world and state["time"] > 3
    assert state["shots"] >= 3 and not state["paused"] and not state["cockpit"]
    assert state["hull"] == 100
    print(f"World {world}: SDL input, firing, movement, chase view OK", flush=True)

state = run("sdl-switch-map-pause", 1, "0.2:3d,1:1c,1.2:32,2.3:01", 4.3)
assert state["world"] == 3 and state["paused"] and state["map"]
assert .7 < state["time"] < 2
print("World switch, chart and pause OK")
