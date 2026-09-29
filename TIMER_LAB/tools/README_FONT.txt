================================================================
 字库说明（自动生成，一般不用手工跑）
================================================================
Inc/oledfont.h 是【自动生成】的，不要手工改。

内容
    oled_asc2_1608    ASCII 8x16，覆盖 0x20~0x7E 共 95 个字符
    oled_cn_1616      中文 16x16 点阵数据
    oled_cn_index     中文索引表（UTF-8 三字节 -> 点阵偏移）

字体来源
    Windows 自带宋体 C:\Windows\Fonts\simsun.ttc
    宋体在 16px 时 ASCII 恰为 8x16、汉字恰为 16x16（点阵内嵌），
    生成出来就是标准点阵，不需要手工描点。

汉字是怎么收集的（不用手工维护字库）
    1) 扫描 Inc/*.h 与 Src/*.c 里的【字符串字面量】中的非 ASCII 字符
       （只扫字符串，不扫注释）
    2) 再加上 tools/ui_strings.txt 里的全部非 ASCII 字符
    3) 再加一份界面常用字兜底表

什么时候会自动重生
    Makefile 里的依赖规则：
        Inc/oledfont.h: tools/gen_font.py tools/ui_strings.txt Inc/config.h Src/app.c
    所以改了 Inc/config.h 的姓名、或改了 Src/app.c 的界面文字，
    直接 make 就会自动重新生成，不需要额外操作。

手工执行（需要时）
    py tools\gen_font.py Inc\oledfont.h tools\ui_strings.txt .
        参数1 = 输出头文件
        参数2 = 额外汉字来源（UTF-8 文本文件，可为空字符串 ""）
        参数3 = 工程根目录（用于扫描源码字符串）
    输出是原子写：先写 .tmp，成功后替换，中途失败不会弄坏已有字库。

依赖
    Python 3 + Pillow；没有 PIL 时：py -m pip install pillow
    注意用 `py` 而不是 `python`（msys2 的 python 可能没装 Pillow），
    脚本里也不要加 shebang（py 启动器会因此切到别的解释器）。
================================================================