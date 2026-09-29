/**
  ******************************************************************************
  * @file    bsp_delay.c
  * @brief   DWT 周期计数器实现的忙等延时
  ******************************************************************************
  */

#include "bsp_delay.h"

void BSP_DelayInit(void)
{
  /* TRCENA 必须先置位，否则 DWT 的寄存器不响应写操作 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void BSP_DelayUs(uint32_t us)
{
  uint32_t start;
  uint32_t ticks;

  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
  {
    BSP_DelayInit();      /* 兜底：忘了调用也不会死等 */
  }

  start = DWT->CYCCNT;
  ticks = us * (SystemCoreClock / 1000000U);

  /* 用无符号差值比较，CYCCNT 回绕（约 23.8s @180MHz）也能正确工作 */
  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

void BSP_DelayMs(uint32_t ms)
{
  while (ms-- > 0U)
  {
    BSP_DelayUs(1000U);
  }
}
