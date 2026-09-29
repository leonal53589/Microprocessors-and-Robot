# -*- coding: utf-8 -*-
"""按 bsp_tim.c 里的公式复核定时器参数（防止再出现 1Hz 那种算式错误）"""

APB1_TIM = 90000000
APB2_TIM = 180000000

def show(tag, ftim, psc, arr, ccr=None):
    f = ftim / (psc + 1) / (arr + 1)
    s = "%-26s f_TIM=%-10d PSC=%-6d ARR=%-6d -> f=%.4f Hz" % (tag, ftim, psc, arr, f)
    if ccr is not None:
        s += "  duty=%.3f%% (CCR=%d)" % (ccr / (arr + 1) * 100.0, ccr)
    print(s)
    return f

print("=== TIM6 1ms 基准 (HAL_InitTick) ===")
show("TIM6", APB1_TIM, APB1_TIM // 1000000 - 1, 1000000 // 1000 - 1)

print()
print("=== TIM3 三色 PWM 1kHz ===")
RGB_FULL = 1000
RGB_FREQ = 1000
psc = APB1_TIM // (RGB_FREQ * RGB_FULL) - 1
show("TIM3", APB1_TIM, psc, RGB_FULL - 1)

print()
print("=== TIM1 N次闪光 PWM 1kHz (APB2 侧) ===")
show("TIM1", APB2_TIM, APB2_TIM // (1000 * 1000) - 1, 999, 500)
for n in (1, 3, 9):
    print("    N=%d -> RCR=%d" % (n, n - 1))

print()
print("=== TIM4 自测仪输出（复刻 BSP_SigOut_Set）===")
for f, duty in ((100, 25), (1000, 50), (10000, 75)):
    p = APB1_TIM // (10000 * f)
    if p > 0: p -= 1
    tick = APB1_TIM // (p + 1)
    arr = tick // f
    if arr == 0: arr = 1
    arr -= 1
    ccr = (arr + 1) * duty // 100
    actual_f = tick / (arr + 1)
    print("  目标 %6dHz/%-3d%%  PSC=%-4d ARR=%-6d CCR=%-6d -> 实际 %.4f Hz / %.4f%%  误差 %+.4f%%"
          % (f, duty, p, arr, ccr, actual_f, ccr / (arr + 1) * 100,
             (actual_f - f) / f * 100))

print()
print("=== TIM3_CH2 输入捕获量程 ===")
cap_tick = APB1_TIM // (APB1_TIM // 1000000)     # -> 1MHz
print("  计数频率 = %d Hz (分辨率 %.3f us)" % (cap_tick, 1e6 / cap_tick))
print("  16 位计数器最大周期 = 65535 us -> 最低可测频率 = %.3f Hz" % (1e6 / 65535))
for f in (100, 1000, 10000):
    n = cap_tick // f
    print("  %6d Hz -> 周期计数 %6d -> 1us 量化带来的频率误差约 %.4f%%"
          % (f, n, 1.0 / n * 100))
