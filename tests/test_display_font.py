"""验证回答预览使用的小写字形与大写字形可区分。"""
from pathlib import Path
import ast
import re

root = Path(__file__).resolve().parents[1]
source = ast.parse((root / "tools/generate_display_font.py").read_text())
patterns = next(ast.literal_eval(node.value) for node in source.body
                if isinstance(node, ast.Assign) and any(
                    isinstance(target, ast.Name) and target.id == "pixel_rows" for target in node.targets))
header = (root / "components/display/color_font.h").read_text()
glyphs = {int(code): [int(value, 16) for value in values.split(", ")]
          for values, code in re.findall(r"\{([^{}]+)\}, /\* ASCII (\d+) \*/", header)}
for code in range(ord("a"), ord("z") + 1):
    pattern = patterns[chr(code)]
    expected = [0] + [int(pattern[i:i + 2], 16) for i in range(0, len(pattern), 2)]
    expected += [0] * (9 - len(expected))
    assert glyphs[code] == expected
    assert glyphs[code] != glyphs[ord(chr(code).upper())]
print("PASS: all lowercase ASCII glyphs preserve case and match the font generator")
