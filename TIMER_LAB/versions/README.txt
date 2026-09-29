================================================================
 versions/ —— 版本快照（不参与编译）
================================================================
这里存放的是各个阶段的稳定版快照，里面有完整的 Src/ Inc/，
是【备份/归档】，不是当前工程的源码。

⚠️ 重要：Eclipse(CubeIDE) 的托管构建会递归扫描整个工程目录，
    如果把带 .c 的目录放在工程里而没有排除，就会出现
    "multiple definition of ..." 这种重复定义错误。

    所以本工程 .cproject 的 sourceEntries 排除清单里必须包含 versions：
        excluding="Debug|build|versions|.metadata|Drivers/CMSIS/..."
    如果以后你在工程里再新建带源码的目录，记得同样加进去。

    排查方法（命令行构建不受影响，只有 IDE 会中招）：
        打开 .cproject，看 <sourceEntries> 的 excluding 属性；
        或者用这条命令模拟扫描：
            Get-ChildItem 工程根目录 -Recurse -Filter *.c

内容：
    v1.0_stable/   基础版（实验一、二已验证通过）
    v2.0_wired/    接线版（多了 WIRING.txt + 6 张接线示意图）

每个快照里都有 SNAPSHOT.txt 说明该版本的验证状态和改动。
================================================================