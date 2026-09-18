"""Encode Blender-rendered frames as a portable GIF; requires Pillow."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
source = ROOT / 'Saved/Art/Whip/Frames'
paths = [source / f'{i:04d}.png' for i in range(1, 50, 2)]
images = [Image.open(path).convert('RGB') for path in paths]
assert len(images) == 25 and all(im.size == (800, 800) for im in images)
out = ROOT / 'SourceArt/Props/Whip/Previews/Whip_Attack.gif'
# GIF timing has 10 ms resolution; alternate 60/70/70 ms for 15 fps.
images[0].save(out, save_all=True, append_images=images[1:],
               duration=[60 if i % 3 == 0 else 70 for i in range(25)],
               loop=0, disposal=2, optimize=False)
with Image.open(out) as check:
    assert check.n_frames == 25
print(out.relative_to(ROOT))
