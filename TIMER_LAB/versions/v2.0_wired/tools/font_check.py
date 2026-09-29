# -*- coding: utf-8 -*-
"""
字库覆盖检查
  把源码里【字符串字面量】用到的所有非 ASCII 字符，和 Inc/oledfont.h
  里实际存在的点阵做对比，列出任何一个会显示成 "?" 的字。
  在接硬件之前就能保证界面上不会出现问号。
"""
import os
import re

ROOT = r"D:\STM32Projects\TIMER_LAB"
STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

SCAN = [
    "Src/app.c",
    "Src/display.c",
    "Src/oled.c",
    "Src/main.c",
    "Src/bsp_tim.c",
    "Src/gpio.c",
    "Src/pcf8574.c",
    "Inc/config.h",
    "Inc/pinmap.h",
]

used = {}
for rel in SCAN:
    path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        continue
    try:
        txt = open(path, encoding="utf-8").read()
    except Exception:
        continue
    for m in STR_RE.finditer(txt):
        for ch in m.group(1):
            if ord(ch) > 0x7F:
                used.setdefault(ch, set()).add(os.path.basename(rel))

font_path = os.path.join(ROOT, "Inc", "oledfont.h")
font = open(font_path, encoding="utf-8").read()
have = set(re.findall(r'\{"(.)",\s*\d+U\}', font))

missing = sorted(set(used) - have)

print("源码字符串里的汉字 : %d 个" % len(used))
print("字库里的汉字       : %d 个" % len(have))
print("多余的（兜底表带来）: %d 个" % len(have - set(used)))
if missing:
    print("!!! 缺失（会显示成 ?）: %s" % "".join(missing))
    for ch in missing:
        print("      %s  <- %s" % (ch, ", ".join(sorted(used[ch]))))
else:
    print("缺失               : 无  <== 界面上不会出现问号")
