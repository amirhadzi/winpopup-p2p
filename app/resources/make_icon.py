"""Build the code-drawn, 16-colour classic messenger icon (optional: Pillow)."""
from pathlib import Path
from PIL import Image

# Original pixel artwork, deliberately sharp-edged like a 1990s Windows icon.
palette = {
    '.': (0, 0, 0, 0), 'k': '#000000', 'w': '#ffffff',
    'g': '#808080', 's': '#c0c0c0', 'n': '#000080', 'b': '#0000ff',
    'y': '#ffff00', 'o': '#808000', 'p': '#800080', 'm': '#ff00ff',
    'c': '#00ffff', 't': '#008080', 'r': '#ff0000',
}
pixels = [
    '................',
    '.....nnn........',
    '....nbwbn.......',
    '..nnbyywbn......',
    '.nnbcyyywbn.....',
    '.nbbwyyywwbn....',
    'npbbwoyywwsbn...',
    'nmpbbwoywwsnk...',
    'nmpbbbwowwnksk..',
    '.nmpbbbwwnkswk..',
    '..nmpbbbnkwssk..',
    '...nmpbnkwswsk..',
    '....npnkwsswsk..',
    '.....nkssssssk..',
    '......kkkkkkk...',
    '................',
]
im = Image.new('RGBA', (16, 16))
for y, row in enumerate(pixels):
    for x, value in enumerate(row):
        colour = palette[value]
        if isinstance(colour, str):
            colour = tuple(int(colour[i:i + 2], 16) for i in (1, 3, 5)) + (255,)
        im.putpixel((x, y), colour)
large = im.resize((256, 256), Image.Resampling.NEAREST)
large.save(Path(__file__).with_name('app.ico'), sizes=[(16, 16), (32, 32), (48, 48), (256, 256)],
           append_images=[im, im.resize((32, 32), Image.Resampling.NEAREST),
                          im.resize((48, 48), Image.Resampling.NEAREST)])
