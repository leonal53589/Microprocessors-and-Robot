# -*- coding: utf-8 -*-
"""
字库生成器：为正点原子 0.96' OLED (SSD1306, 128x64) 生成 C 字库。

  * ASCII  : 8x16 点阵
  * 中文   : 16x16 点阵

字体来源：Windows 自带宋体 C:\\Windows\\Fonts\\simsun.ttc
  宋体在 16px 时 ASCII 恰为 8x16、汉字恰为 16x16（点阵内嵌），
  生成出来就是标准点阵，无需手工描点。

汉字来源（自动收集，所以改了界面文字或姓名不用手工维护字库）：
  1) 扫描工程里 Inc/*.h 与 Src/*.c 的【字符串字面量】中的非 ASCII 字符
     （只扫字符串，不扫注释，避免把注释里的汉字也塞进字库）
  2) 再加上 ui_strings.txt 里的全部非 ASCII 字符

用法:
    py gen_font.py <输出头文件> [ui_strings.txt] [工程根目录]
        例：py gen_font.py Inc/oledfont.h tools/ui_strings.txt .

输出是【原子写】：先写 .tmp，成功后 os.replace 覆盖，
所以脚本中途失败也不会把已存在的 oledfont.h 弄坏。
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

SIMSUN = r"C:\Windows\Fonts\simsun.ttc"
ASCII_FIRST, ASCII_LAST = 0x20, 0x7E

# 字符串字面量匹配（用于从源码里收集界面汉字）
STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

# 兜底：即使源码扫描不到，也保证这些界面常用字存在
CN_FALLBACK = (
    "定时器实验一二三四五六七八九十"
    "姓名学号"
    "显示屏幕菜单项目选择进入返回"
    "反应速度测试抢跑最好成绩本次请按键开始等待准备"
    "三色呼吸灯同步流水警闪模式红绿蓝"
    "频率占空比设定测量误差无信号有效跳线输入输出通道"
    "次数急停已刹车确认恢复运行中停止"
    "秒毫秒请输入正确错误成功失败"
)


def load_font(size):
    return ImageFont.truetype(SIMSUN, size)


def bitmap(ch, font, w, h):
    """把单个字符渲染成 w*h 的 0/1 位图（True=亮点）"""
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((0, 0), ch, fill=255, font=font)
    px = img.load()
    return [[1 if px[x, y] > 100 else 0 for x in range(w)] for y in range(h)]


def pack(bm, w, h):
    """按 SSD1306 页格式打包：每列 8 个纵向像素一个字节，LSB 在上"""
    out = []
    for page in range(h // 8):
        for x in range(w):
            b = 0
            for bit in range(8):
                if bm[page * 8 + bit][x]:
                    b |= (1 << bit)
            out.append(b)
    return out


def collect_from_sources(root, out_path):
    """扫描 Inc/*.h 与 Src/*.c 的字符串字面量，收集其中的非 ASCII 字符"""
    chars = set()
    if not root or not os.path.isdir(root):
        return chars

    for sub in ("Inc", "Src"):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(d, name)
            if os.path.abspath(path) == os.path.abspath(out_path):
                continue                      # 不扫自己生成的文件
            try:
                raw = open(path, encoding="utf-8").read()
            except Exception:
                continue
            for m in STR_RE.finditer(raw):
                for ch in m.group(1):
                    if ord(ch) > 0x7F:
                        chars.add(ch)
    return chars


def fmt_array(name, data, width=16):
    lines = ["const uint8_t %s[] = {" % name]
    for i in range(0, len(data), width):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + width]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "oledfont.h"
    ui_file = sys.argv[2] if len(sys.argv) > 2 else ""

    # 工程根目录：显式给了就用；没给就从脚本位置推（tools/ 的上一级），
    # 这样无论在哪个目录手工调用都不会扫错地方
    if len(sys.argv) > 3 and sys.argv[3].strip():
        root = sys.argv[3]
    else:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if not os.path.isdir(os.path.join(root, "Src")):
        root = os.path.dirname(os.path.dirname(os.path.abspath(out_path)))

    chars = set(CN_FALLBACK)

    if ui_file and os.path.isfile(ui_file):
        raw = open(ui_file, encoding="utf-8").read()
        chars.update(ch for ch in raw if ord(ch) > 0x7F)

    chars.update(collect_from_sources(root, out_path))

    cn_chars = sorted(chars)
    f16 = load_font(16)

    # ---------- ASCII 8x16 ----------
    ascii_data = []
    for code in range(ASCII_FIRST, ASCII_LAST + 1):
        ascii_data.extend(pack(bitmap(chr(code), f16, 8, 16), 8, 16))

    # ---------- 中文 16x16 ----------
    cn_index = []
    cn_data = []
    for ch in cn_chars:
        cn_index.append((ch, len(cn_data)))
        cn_data.extend(pack(bitmap(ch, f16, 16, 16), 16, 16))

    # ---------- 输出头文件（原子写） ----------
    tmp = out_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fp:
        fp.write("/**\n")
        fp.write("  ******************************************************************************\n")
        fp.write("  * @file    %s\n" % os.path.basename(out_path))
        fp.write("  * @brief   SSD1306 点阵字库（自动生成，请勿手工修改）\n")
        fp.write("  *\n")
        fp.write("  *  生成工具 : tools/gen_font.py  （make 时自动重生）\n")
        fp.write("  *  字体来源 : Windows 宋体 simsun.ttc\n")
        fp.write("  *  ASCII    : 8x16  (%d 个, 0x%02X~0x%02X)\n"
                 % (ASCII_LAST - ASCII_FIRST + 1, ASCII_FIRST, ASCII_LAST))
        fp.write("  *  中文     : 16x16 (%d 个，从源码字符串字面量自动收集)\n" % len(cn_chars))
        fp.write("  ******************************************************************************\n")
        fp.write("  */\n\n")
        fp.write("#ifndef __OLEDFONT_H\n#define __OLEDFONT_H\n\n#include \"main.h\"\n\n")
        fp.write("#define OLED_ASCII_FIRST  0x%02X\n#define OLED_ASCII_LAST   0x%02X\n\n"
                 % (ASCII_FIRST, ASCII_LAST))
        fp.write("/* ---- ASCII 8x16 ---- */\n")
        fp.write(fmt_array("oled_asc2_1608", ascii_data))
        fp.write("\n\n/* ---- 中文 16x16 ---- */\n")
        fp.write(fmt_array("oled_cn_1616", cn_data))
        fp.write("\n\n/* 中文索引表（按 UTF-8 三字节匹配，便于直接写字面量） */\n")
        fp.write("typedef struct { const char *utf8; uint16_t offset; } OledCnItem;\n")
        fp.write("const OledCnItem oled_cn_index[] = {\n")
        for ch, off in cn_index:
            fp.write('    {"%s", %dU},\n' % (ch, off))
        fp.write("};\n")
        fp.write("#define OLED_CN_COUNT  %dU\n\n" % len(cn_index))
        fp.write("#endif /* __OLEDFONT_H */\n")

    os.replace(tmp, out_path)

    print("generated: %s" % out_path)
    print("  ascii chars : %d  bytes: %d" % (ASCII_LAST - ASCII_FIRST + 1, len(ascii_data)))
    print("  cn chars    : %d  bytes: %d" % (len(cn_chars), len(cn_data)))


if __name__ == "__main__":
    main()
