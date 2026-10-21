"""从 Noto Sans CJK SC 生成显示所需的四字 32x32 点阵（需 Pillow）。"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

font = ImageFont.truetype(sys.argv[1], 30)
rows = []
preview = Image.new('RGB', (240, 320), '#ff0000')
for i, character in enumerate('红绿蓝色'):
    glyph = Image.new('L', (32, 32))
    draw = ImageDraw.Draw(glyph)
    x0, y0, x1, y1 = font.getbbox(character)
    draw.text(((32 - (x1-x0)) // 2 - x0, (32 - (y1-y0)) // 2 - y0), character, font=font, fill=255)
    bits = [sum((1 << (31-x)) for x in range(32) if glyph.getpixel((x,y)) >= 128) for y in range(32)]
    rows.append('    {\n' + '\n'.join('        ' + ', '.join(f'0x{v:08x}' for v in bits[n:n+4]) + ',' for n in range(0,32,4)) + '\n    },')
    if i in (0,3):
        preview.paste('white', (84 if i == 0 else 124,144,116 if i == 0 else 156,176), glyph)
header = '/* Noto Sans CJK SC 四字点阵，SIL OFL 1.1；见 fonts/OFL.txt。由 tools/generate_display_font.py 生成。 */\n#pragma once\n#include <stdint.h>\n\nstatic const uint32_t color_font[4][32] = {\n' + '\n'.join(rows) + '\n};\n'
Path(__file__).resolve().parents[1].joinpath('components/display/color_font.h').write_text(header)
preview.save('/tmp/esp32-display-preview.png')
