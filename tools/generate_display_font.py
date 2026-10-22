"""从 Noto Sans CJK SC 生成 ASCII 摩斯码显示字模（需 Pillow）。"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

face = ImageFont.truetype(sys.argv[1], 16)
# 主界面使用手工绘制的等宽 5×7 字形，避免矢量缩小时笔画不均。
pixel_rows = {
    'A': '0e11111f111111', 'B': '1e11111e11111e', 'C': '0e11101010110e',
    'D': '1e11111111111e', 'E': '1f10101e10101f', 'F': '1f10101e101010',
    'G': '0e11101711110f', 'H': '1111111f111111', 'I': '1f04040404041f',
    'J': '0702020212120c', 'K': '11121418141211', 'L': '1010101010101f',
    'M': '111b1515111111', 'N': '11191513111111', 'O': '0e11111111110e',
    'P': '1e11111e101010', 'Q': '0e11111115120d', 'R': '1e11111e141211',
    'S': '0f10100e01011e', 'T': '1f040404040404', 'U': '1111111111110e',
    'V': '11111111110a04', 'W': '11111115151b11', 'X': '11110a040a1111',
    'Y': '11110a04040404', 'Z': '1f01020408101f',
    '0': '0e11131519110e', '1': '040c040404040e', '2': '0e11010204081f',
    '3': '1e01010601011e', '4': '02060a121f0202', '5': '1f10101e01011e',
    '6': '0e10101e11110e', '7': '1f010204080808', '8': '0e11110e11110e',
    '9': '0e11110f01010e', '?': '0e110102040004', '>': '10080402040810',
    '/': '01020204080810', '-': '0000001f000000', '.': '00000006060000',
    ' ': '00000000000000',
}
rows = []
for code in range(32, 127):
    character = chr(code)
    box = face.getbbox(character)
    glyph = Image.new('L', (max(1, box[2] - box[0]), max(1, box[3] - box[1])))
    ImageDraw.Draw(glyph).text((-box[0], -box[1]), character, font=face, fill=255)
    # 每个字符放进 5×9 点阵，另留一列空白作为字符间距。
    glyph.thumbnail((5, 9), Image.Resampling.LANCZOS)
    cell = Image.new('L', (5, 9))
    cell.paste(glyph, ((5 - glyph.width) // 2, (9 - glyph.height) // 2))
    bits = [sum(1 << (4 - x) for x in range(5) if cell.getpixel((x, y)) >= 100)
            for y in range(9)]
    if character.upper() in pixel_rows:
        values = pixel_rows[character.upper()]
        bits = [0] + [int(values[i:i + 2], 16) for i in range(0, 14, 2)] + [0]
    rows.append('    {' + ', '.join(f'0x{v:02x}' for v in bits) + f'}}, /* ASCII {code} */')
header = ('/* ASCII 字模：字母、数字及主要符号为手工 5x7；其余为 Noto Sans CJK SC 子集（SIL OFL 1.1）。\n'
          ' * 由 tools/generate_display_font.py 生成：每字 5x9，ASCII 32~126。 */\n'
          '#pragma once\n#include <stdint.h>\n'
          'static const uint8_t telegraph_font[95][9] = {\n' + '\n'.join(rows) + '\n};\n')
Path(__file__).resolve().parents[1].joinpath('components/display/color_font.h').write_text(header)
