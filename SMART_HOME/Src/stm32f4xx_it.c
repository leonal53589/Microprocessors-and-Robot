/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   中断服务函数
  *
  *  本工程用到的中断很少，这是刻意的设计：
  *      SysTick       —— HAL 的 1ms 时基，所有任务调度都基于它（非阻塞）
  *      DMA2_Stream1  —— DCMI 搬运摄像头一行/一帧数据
  *  温度、湿度、电压、按键、红灯闪烁全部在主循环里按时间戳处理，
  *  不占用中断，符合"中断里只做最少的事"的原则。
  ******************************************************************************
  */

#include "stm32f4xx_it.h"
#include "bsp_dcmi.h"

/* ---- Cortex-M4 内核异常 ---- */

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  /* 出错就停在这里，方便调试器直接看到现场（PC/LR 指向出错点） */
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/**
  * @brief 1ms 系统滴答：只做计数，交给 HAL 维护 uwTick
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/**
  * @brief DCMI 帧结束中断（帧搬完由 HAL 打开这个中断来通知应用层）
  */
void DCMI_IRQHandler(void)
{
  BSP_DCMI_IRQHandler();
}

/**
  * @brief DCMI 的 DMA 传输中断（DMA2 Stream1 Channel1）
  *        只把中断转给 HAL，帧完成/出错的具体处理在 bsp_dcmi.c 的回调里
  */
void DMA2_Stream1_IRQHandler(void)
{
  BSP_DCMI_DmaIRQHandler();
}
