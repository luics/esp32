"""从 Noto Sans CJK SC 生成中英颜色文字点阵（需 Pillow）。"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

font = ImageFont.truetype(sys.argv[1], 30)
english_font = ImageFont.truetype(sys.argv[1], 18)


def rasterize(character, width, height, face):
    glyph = Image.new('L', (width, height))
    draw = ImageDraw.Draw(glyph)
    x0, y0, x1, y1 = face.getbbox(character)
    draw.text(((width - (x1-x0)) // 2 - x0, (height - (y1-y0)) // 2 - y0),
              character, font=face, fill=255)
    return glyph


def format_rows(values, per_line):
    return '    {\n' + '\n'.join(
        '        ' + ', '.join(values[n:n+per_line]) + ','
        for n in range(0, len(values), per_line)
    ) + '\n    },'


chinese_rows = []
for character in '红绿蓝色':
    glyph = rasterize(character, 32, 32, font)
    bits = [sum((1 << (31-x)) for x in range(32) if glyph.getpixel((x, y)) >= 128)
            for y in range(32)]
    chinese_rows.append(format_rows([f'0x{value:08x}' for value in bits], 4))

english_chars = 'BDEGLNRU'
english_rows = []
for character in english_chars:
    glyph = rasterize(character, 12, 20, english_font)
    bits = [sum((1 << (11-x)) for x in range(12) if glyph.getpixel((x, y)) >= 128)
            for y in range(20)]
    english_rows.append(format_rows([f'0x{value:03x}' for value in bits], 4))

header = ('/* Noto Sans CJK SC 点阵，SIL OFL 1.1；见 fonts/OFL.txt。由 tools/generate_display_font.py 生成。 */\n'
          '#pragma once\n#include <stdint.h>\n\n'
          'static const uint32_t color_font[4][32] = {\n' + '\n'.join(chinese_rows) + '\n};\n\n'
          'static const uint16_t english_font[8][20] = {\n' + '\n'.join(english_rows) + '\n};\n')
Path(__file__).resolve().parents[1].joinpath('components/display/color_font.h').write_text(header)

preview = Image.new('RGB', (720, 320), 'black')
for i, (chinese, english, background) in enumerate(zip(
        ('红色', '绿色', '蓝色'), ('RED', 'GREEN', 'BLUE'), ('red', 'green', 'blue'))):
    panel = Image.new('RGB', (240, 320), background)
    draw = ImageDraw.Draw(panel)
    draw.text((120, 142), chinese, font=font, fill='black' if i == 1 else 'white', anchor='mm')
    draw.text((120, 178), english, font=english_font, fill='black' if i == 1 else 'white', anchor='mm')
    preview.paste(panel, (240 * i, 0))
preview.save('/tmp/esp32-display-preview.png')
