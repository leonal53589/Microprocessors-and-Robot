/**
  ******************************************************************************
  * @file    config.h
  * @brief   实验一在 OLED 上显示的学生信息（占位，请改成你自己的）
  *
  *  ★ 改完直接重新编译即可，不需要手工跑字库脚本 ★
  *    Makefile 里挂了依赖：只要本文件或 Src/app.c 里的中文字符串有变化，
  *    构建时会自动调用 tools/gen_font.py 重新生成 Inc/oledfont.h
  *    （生成器会扫描源码里的字符串字面量，把用到的汉字全部打进字库）
  ******************************************************************************
  */

#ifndef __CONFIG_H
#define __CONFIG_H

#define STUDENT_NAME    "xxx"          /* ← 改成你的中文姓名 */
#define STUDENT_ID      "xxx"          /* ← 改成你的学号     */

#define EXP_TITLE       "定时器实验"

#endif /* __CONFIG_H */
