from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


FRAME_RE = re.compile(
    r"tick=(?P<tick>\d+) \| P(?P<a_id>\d+) x=(?P<a_x>[-+0-9.eE]+) hp=(?P<a_hp>\d+)"
    r" \| P(?P<b_id>\d+) x=(?P<b_x>[-+0-9.eE]+) hp=(?P<b_hp>\d+)"
)


@dataclass(frozen=True)
class Player:
    player_id: int
    x: float
    hp: int


@dataclass(frozen=True)
class Frame:
    tick: int
    first: Player
    second: Player


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render the YCNet arena demo as a GIF.")
    parser.add_argument(
        "--exe",
        type=Path,
        default=Path("build/Debug/arena_demo.exe"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("docs/arena_demo.gif"),
    )
    return parser.parse_args()


def run_demo(exe: Path) -> tuple[str, list[Frame]]:
    if not exe.exists():
        raise SystemExit(f"arena demo executable not found: {exe}")

    completed = subprocess.run(
        [str(exe)],
        check=True,
        capture_output=True,
        text=True,
    )
    frames: list[Frame] = []
    for line in completed.stdout.splitlines():
        match = FRAME_RE.fullmatch(line.strip())
        if not match:
            continue
        frames.append(
            Frame(
                tick=int(match["tick"]),
                first=Player(int(match["a_id"]), float(match["a_x"]), int(match["a_hp"])),
                second=Player(int(match["b_id"]), float(match["b_x"]), int(match["b_hp"])),
            )
        )
    if not frames:
        raise SystemExit("arena demo produced no renderable tick lines")
    return completed.stdout, frames


def player_screen_x(world_x: float, left: int, right: int) -> int:
    half_extent = 10.0
    normalized = (max(-half_extent, min(half_extent, world_x)) + half_extent) / (2 * half_extent)
    return int(left + normalized * (right - left))


def draw_hp(draw: ImageDraw.ImageDraw, x: int, y: int, hp: int) -> None:
    width = 120
    height = 12
    left = x - width // 2
    draw.rectangle((left, y, left + width, y + height), outline="black", width=1)
    fill_width = int(width * max(0, min(100, hp)) / 100)
    if fill_width > 0:
        draw.rectangle((left + 1, y + 1, left + fill_width - 1, y + height - 1), fill="black")


def render_frame(frame: Frame, size: tuple[int, int] = (900, 500)) -> Image.Image:
    image = Image.new("RGB", size, "white")
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()

    draw.text((30, 25), "YCNet Authoritative Arena", fill="black", font=font)
    draw.text(
        (30, 48),
        "Reliable: join / attack / damage   |   Unreliable: input / snapshot",
        fill="black",
        font=font,
    )
    draw.text((780, 25), f"tick {frame.tick}", fill="black", font=font)

    arena_left, arena_right = 70, 830
    arena_top, arena_bottom = 120, 390
    center_x = (arena_left + arena_right) // 2
    draw.rectangle((arena_left, arena_top, arena_right, arena_bottom), outline="black", width=2)
    draw.line((center_x, arena_top, center_x, arena_bottom), fill="gray", width=1)
    draw.text((arena_left, arena_bottom + 12), "-10", fill="black", font=font)
    draw.text((center_x - 5, arena_bottom + 12), "0", fill="black", font=font)
    draw.text((arena_right - 15, arena_bottom + 12), "+10", fill="black", font=font)

    players = [(frame.first, arena_top + 90), (frame.second, arena_top + 190)]
    for player, y in players:
        x = player_screen_x(player.x, arena_left, arena_right)
        radius = 22
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), outline="black", width=3)
        label = f"P{player.player_id}  x={player.x:.2f}  HP={player.hp}"
        draw.text((x - 65, y - 50), label, fill="black", font=font)
        draw_hp(draw, x, y + 32, player.hp)
        if player.hp == 0:
            draw.line((x - 18, y - 18, x + 18, y + 18), fill="black", width=3)
            draw.line((x - 18, y + 18, x + 18, y - 18), fill="black", width=3)

    draw.text(
        (30, 462),
        "Frames are generated from arena_demo.exe output; server owns movement, attack range, HP and death state.",
        fill="black",
        font=font,
    )
    return image


def main() -> None:
    args = parse_args()
    stdout, frames = run_demo(args.exe)
    images = [render_frame(frame) for frame in frames]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(
        args.output,
        save_all=True,
        append_images=images[1:],
        duration=650,
        loop=0,
        optimize=False,
    )
    print(args.output)
    print(f"frames={len(images)}")
    print("demo_result=PASS" if "demo_result=PASS" in stdout else "demo_result=UNKNOWN")


if __name__ == "__main__":
    main()
