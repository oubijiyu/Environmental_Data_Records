#ifndef __KEY_H
#define __KEY_H

#include "sys.h"

/* 按键1 = PB12，按键2 = PB13，按下为低电平 */
void Key_Init(void);            /* 上电初始化一次 */
void Key_WaitMs(u16 ms);        /* 代替 delay_ms：等待期间每 5ms 扫描按键 */
void Key_LogViewUpdate(void);   /* 跟随最新时刷新第 3、4 行；浏览历史时不动 */
void Key_BootCheckClear(void);   /* 上电双键长按 3 秒清空日志 */
#endif



