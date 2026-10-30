#ifndef __W25Q64_H
#define __W25Q64_H

#include "sys.h"

/* 驱动 */
u8   W25Q64_Init(void);                 /* 0 = ID 正确 */
u32  W25Q64_ReadID(void);               /* 期望 0xEF4017 */
void W25Q64_Read(u32 addr, u8 *buf, u16 len);
u8   W25Q64_PageProgram(u32 addr, const u8 *buf, u16 len);
u8   W25Q64_SectorErase(u32 addr);
u8   W25Q64_WaitBusy(u32 timeout_ms);
void W25Q64_WriteEnable(void);
u8   W25Q64_ReadSR1(void);
u8   W25Q64_ReadSR2(void);
u8   W25Q64_ReadSR3(void);        /* 新增 */
u8   W25Q64_ClearCMP(void);       /* 新增 */

/* 记录：16 字节，crc 在偏移 10 */
typedef struct {
    u32 ts;
    u16 light;
    u8  temp;
    u8  humi;
    u8  flags;
    u8  rsv;
    u16 crc;
    u32 rsv2;
} LogRec;

u16  CRC16(const u8 *p, u16 n);
u8   Log_Init(void);
u8   Log_Append(const LogRec *r);
u8   Log_Read(u16 index, LogRec *r);
u16  Log_Count(void);
u8   Log_Clear(void);      /* 新增：清空日志，SAVE 归零 */

#endif


