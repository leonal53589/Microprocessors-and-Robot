# -*- coding: utf-8 -*-
"""
字库生成器：为 7 寸 800x480 RGB LCD 生成 C 点阵字库。

  * 小号：ASCII 12x24 + 汉字 24x24   （正文、数值）
  * 大号：ASCII 16x32 + 汉字 32x32   （标题）

字体来源：Windows 自带宋体 C:\\Windows\\Fonts\\simsun.ttc
  宋体在 24px 时 ASCII 恰为 12x24、汉字恰为 24x24；
  在 32px 时 ASCII 恰为 16x32、汉字恰为 32x32（点阵内嵌，无需手工描点）。

汉字来源（自动收集，所以改了界面文字或姓名不用手工维护字库）：
  1) 扫描工程 Inc/*.h 与 Src/*.c 的【字符串字面量】中的非 ASCII 字符
     （只扫字符串、不扫注释，避免把注释里的汉字也塞进字库）
  2) 再加上 ui_strings.txt 里的全部非 ASCII 字符

点阵打包方式（与 bsp_font.c 的取字顺序严格对应）：
  按行打包、每字节高位在左（MSB first），每行占 ceil(宽/8) 字节。
      12 宽 -> 2 字节/行   24 行 -> 48 字节/字（ASCII 小号）
      16 宽 -> 2 字节/行   32 行 -> 64 字节/字（ASCII 大号）
      24 宽 -> 3 字节/行   24 行 -> 72 字节/字（汉字 小号）
      32 宽 -> 4 字节/行   32 行 -> 128 字节/字（汉字 大号）

用法:
    py gen_font.py <输出头文件> [ui_strings.txt] [工程根目录]
        例：py gen_font.py Inc/lcdfont.h tools/ui_strings.txt .

输出是【原子写】：先写 .tmp，成功后 os.replace 覆盖，
所以脚本中途失败也不会把已存在的 lcdfont.h 弄坏。
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
    "智能家居中控实验微处理器与机器人"
    "温度湿度电压阈值上限越限正常报警故障"
    "正常状态失联无效有效更新采样等待连接"
    "摄像头预览首页页面切换重试按键返回"
    "姓名学号班级计软学院"
    "摄氏度百分比伏特原始值换算"
    "欢迎系统就绪请插入检查接线"
)


def load_font(px):
    return ImageFont.truetype(SIMSUN, px)


def bitmap(ch, font, w, h):
    """把单个字符渲染成 w*h 的 0/1 位图（True = 亮点）"""
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((0, 0), ch, fill=255, font=font)
    px = img.load()
    return [[1 if px[x, y] > 100 else 0 for x in range(w)] for y in range(h)]


def pack_rows(bm, w, h):
    """按行打包，MSB 在左，每行 ceil(w/8) 字节"""
    bpr = (w + 7) // 8
    out = []
    for y in range(h):
        for b in range(bpr):
            byte = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < w and bm[y][x]:
                    byte |= (1 << (7 - bit))
            out.append(byte)
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


def gen_ascii(px, w, h):
    font = load_font(px)
    data = []
    for code in range(ASCII_FIRST, ASCII_LAST + 1):
        data.extend(pack_rows(bitmap(chr(code), font, w, h), w, h))
    return data


def gen_cn(chars, size):
    font = load_font(size)
    index = []
    data = []
    for ch in chars:
        index.append((ch, len(data)))
        data.extend(pack_rows(bitmap(ch, font, size, size), size, size))
    return index, data


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "Inc/lcdfont.h"
    ui_file = sys.argv[2] if len(sys.argv) > 2 else ""

    # 工程根目录：显式给了就用；没给就从脚本位置推（tools/ 的上一级）
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

    asc_small = gen_ascii(24, 12, 24)
    asc_large = gen_ascii(32, 16, 32)
    idx_small, cn_small = gen_cn(cn_chars, 24)
    idx_large, cn_large = gen_cn(cn_chars, 32)

    tmp = out_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fp:
        fp.write("/**\n")
        fp.write("  ******************************************************************************\n")
        fp.write("  * @file    %s\n" % os.path.basename(out_path))
        fp.write("  * @brief   RGB LCD 点阵字库（自动生成，请勿手工修改）\n")
        fp.write("  *\n")
        fp.write("  *  生成工具 : tools/gen_font.py  （make 时自动重生）\n")
        fp.write("  *  字体来源 : Windows 宋体 simsun.ttc\n")
        fp.write("  *  打包方式 : 按行、MSB 在左，每行 ceil(宽/8) 字节\n")
        fp.write("  *  小号     : ASCII 12x24 (%d 个) / 汉字 24x24 (%d 个)\n"
                 % (ASCII_LAST - ASCII_FIRST + 1, len(cn_chars)))
        fp.write("  *  大号     : ASCII 16x32 (%d 个) / 汉字 32x32 (%d 个)\n"
                 % (ASCII_LAST - ASCII_FIRST + 1, len(cn_chars)))
        fp.write("  ******************************************************************************\n")
        fp.write("  */\n\n")
        fp.write("#ifndef __LCDFONT_H\n#define __LCDFONT_H\n\n#include \"main.h\"\n\n")
        fp.write("#define LCD_FONT_ASCII_FIRST  0x%02X\n#define LCD_FONT_ASCII_LAST   0x%02X\n\n"
                 % (ASCII_FIRST, ASCII_LAST))
        fp.write("#define LCD_CN_COUNT_SMALL    %dU\n#define LCD_CN_COUNT_LARGE    %dU\n\n"
                 % (len(idx_small), len(idx_large)))

        fp.write("/* ---- 小号 ASCII 12x24 ---- */\n")
        fp.write(fmt_array("lcd_asc_1224", asc_small))
        fp.write("\n\n/* ---- 大号 ASCII 16x32 ---- */\n")
        fp.write(fmt_array("lcd_asc_1632", asc_large))
        fp.write("\n\n/* ---- 小号汉字 24x24 ---- */\n")
        fp.write(fmt_array("lcd_cn_2424", cn_small))
        fp.write("\n\n/* ---- 大号汉字 32x32 ---- */\n")
        fp.write(fmt_array("lcd_cn_3232", cn_large))

        fp.write("\n\n/* 汉字索引（按 UTF-8 字节串匹配，便于直接写字面量） */\n")
        fp.write("typedef struct { const char *utf8; uint32_t offset; } LcdCnItem;\n")
        for name, idx, count in (("lcd_cn_index_small", idx_small, "LCD_CN_COUNT_SMALL"),
                                 ("lcd_cn_index_large", idx_large, "LCD_CN_COUNT_LARGE")):
            fp.write("const LcdCnItem %s[] = {\n" % name)
            for ch, off in idx:
                fp.write('    {"%s", %dU},\n' % (ch, off))
            fp.write("};\n")
            fp.write("/* %s = %d */\n" % (count, len(idx)))

        fp.write("\n#endif /* __LCDFONT_H */\n")

    os.replace(tmp, out_path)

    print("generated: %s" % out_path)
    print("  ascii small : %d bytes" % len(asc_small))
    print("  ascii large : %d bytes" % len(asc_large))
    print("  cn chars    : %d" % len(cn_chars))
    print("  cn small    : %d bytes" % len(cn_small))
    print("  cn large    : %d bytes" % len(cn_large))


if __name__ == "__main__":
    main()
