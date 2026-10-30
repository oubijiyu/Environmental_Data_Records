#include "w25q64.h"
#include "delay.h"
#include <stddef.h>
#include <string.h>

/* 引脚：CS=PA4  SCK=PA5  MISO=PA6  MOSI=PA7 */
#define W25_PORT    GPIOA
#define W25_CS      GPIO_Pin_4
#define W25_SCK     GPIO_Pin_5
#define W25_MISO    GPIO_Pin_6
#define W25_MOSI    GPIO_Pin_7

#define W25_CS_LOW()    GPIO_ResetBits(W25_PORT, W25_CS)
#define W25_CS_HIGH()   GPIO_SetBits(W25_PORT, W25_CS)

#define W25_SCK_H()     GPIO_SetBits(W25_PORT, W25_SCK)
#define W25_SCK_L()     GPIO_ResetBits(W25_PORT, W25_SCK)
#define W25_MOSI_H()    GPIO_SetBits(W25_PORT, W25_MOSI)
#define W25_MOSI_L()    GPIO_ResetBits(W25_PORT, W25_MOSI)
#define W25_MISO_IN()   GPIO_ReadInputDataBit(W25_PORT, W25_MISO)

#define W25_DLY()       delay_us(1)

/* 编译期检查：结构体必须是 16 字节 */
typedef char logrec_size_check[(sizeof(LogRec) == 16) ? 1 : -1];

#define LOG_CRC_OFF     ((u16)offsetof(LogRec, crc))
#define LOG_CRC_LEN     LOG_CRC_OFF

/* ---------------- 软件 SPI（模式 0） ---------------- */
static u8 SPI1_RW(u8 tx)
{
    u8 i, rx = 0;

    for (i = 0; i < 8; i++) {
        if (tx & 0x80) W25_MOSI_H();
        else           W25_MOSI_L();
        tx <<= 1;
        W25_DLY();

        W25_SCK_H();              /* 上升沿：从机采样 */
        W25_DLY();

        rx <<= 1;
        if (W25_MISO_IN()) rx |= 1;

        W25_SCK_L();              /* 下降沿：准备下一位 */
        W25_DLY();
    }
    return rx;
}

static void W25Q64_InitPins(void)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    g.GPIO_Pin   = W25_CS | W25_SCK | W25_MOSI;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(W25_PORT, &g);

    g.GPIO_Pin  = W25_MISO;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(W25_PORT, &g);

    W25_CS_HIGH();
    W25_SCK_L();                  /* 模式 0，空闲低 */
    W25_MOSI_H();
}

/* ---------------- 驱动 ---------------- */
u8 W25Q64_Init(void)
{
    W25Q64_InitPins();
    delay_ms(10);
    return (W25Q64_ReadID() == 0xEF4017) ? 0 : 1;
		if (W25Q64_ReadSR2() & 0x40) W25Q64_ClearCMP();
}

u32 W25Q64_ReadID(void)
{
    u32 id;
    W25_CS_LOW();
    SPI1_RW(0x9F);
    id  = (u32)SPI1_RW(0xFF) << 16;
    id |= (u32)SPI1_RW(0xFF) << 8;
    id |= (u32)SPI1_RW(0xFF);
    W25_CS_HIGH();
    return id;
}

void W25Q64_Read(u32 addr, u8 *buf, u16 len)
{
    u16 i;
    W25_CS_LOW();
    SPI1_RW(0x03);
    SPI1_RW((u8)(addr >> 16));
    SPI1_RW((u8)(addr >> 8));
    SPI1_RW((u8)addr);
    for (i = 0; i < len; i++) buf[i] = SPI1_RW(0xFF);
    W25_CS_HIGH();
}

u8 W25Q64_ReadSR1(void)
{
    u8 v;
    W25_CS_LOW(); SPI1_RW(0x05); v = SPI1_RW(0xFF); W25_CS_HIGH();
    return v;
}

u8 W25Q64_ReadSR2(void)
{
    u8 v;
    W25_CS_LOW(); SPI1_RW(0x35); v = SPI1_RW(0xFF); W25_CS_HIGH();
    return v;
}

void W25Q64_WriteEnable(void)
{
    W25_CS_LOW(); SPI1_RW(0x06); W25_CS_HIGH();
}

u8 W25Q64_WaitBusy(u32 timeout_ms)
{
    u8 sr;
    while (timeout_ms--) {
        sr = W25Q64_ReadSR1();
        if ((sr & 0x01) == 0) return 0;
        delay_ms(1);
    }
    return 1;
}

u8 W25Q64_PageProgram(u32 addr, const u8 *buf, u16 len)
{
    u16 i;
    if (len == 0 || len > 256) return 1;
    if ((addr & 0xFF) + len > 256) return 1;

    W25Q64_WriteEnable();
    W25_CS_LOW();
    SPI1_RW(0x02);
    SPI1_RW((u8)(addr >> 16));
    SPI1_RW((u8)(addr >> 8));
    SPI1_RW((u8)addr);
    for (i = 0; i < len; i++) SPI1_RW(buf[i]);
    W25_CS_HIGH();

    return W25Q64_WaitBusy(100);
}

u8 W25Q64_SectorErase(u32 addr)
{
    W25Q64_WriteEnable();
    W25_CS_LOW();
    SPI1_RW(0x20);
    SPI1_RW((u8)(addr >> 16));
    SPI1_RW((u8)(addr >> 8));
    SPI1_RW((u8)addr);
    W25_CS_HIGH();

    return W25Q64_WaitBusy(1000);
}

/* ---------------- 记录层 ---------------- */
#define LOG_BASE      (0x000000u)
#define LOG_SECTOR    (4096u)
#define LOG_RECSZ     (16u)
#define LOG_PER_SEC   (LOG_SECTOR / LOG_RECSZ)
#define LOG_MAX_REC   (65535u)                   /* s_count 是 u16，最多 65535 条 */

static u32 s_addr;
static u16 s_count;

u16 CRC16(const u8 *p, u16 n)
{
    u16 crc = 0xFFFF;
    u8  i;
    while (n--) {
        crc ^= (u16)(*p++) << 8;
        for (i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
    }
    return crc;
}

u8 Log_Init(void)
{
    u8  buf[LOG_RECSZ];
    u16 crc;
    u32 i;

    s_addr  = LOG_BASE;
    s_count = 0;

    /* 从 0 开始逐条扫描，跨扇区一直数到空白或 CRC 错为止 */
    for (i = 0; i < LOG_MAX_REC; i++) {
        W25Q64_Read(LOG_BASE + i * LOG_RECSZ, buf, LOG_RECSZ);

        if (buf[0] == 0xFF && buf[1] == 0xFF) break;   /* 空白区，日志结束 */

        memcpy(&crc, buf + LOG_CRC_OFF, 2);
        if (CRC16(buf, LOG_CRC_LEN) != crc) break;     /* 半条/坏记录，结束 */

        s_count++;
    }

    s_addr = LOG_BASE + (u32)s_count * LOG_RECSZ;      /* 指向下一条要写的位置 */
    return 0;
}

/* 清空日志：只擦除第一个扇区，O(1) 完成。
   之后写入会按 4KB 扇区边界逐个擦除旧扇区，不需要一次擦 1MB。 */
u8 Log_Clear(void)
{
    if (W25Q64_SectorErase(LOG_BASE)) return 1;   /* 擦除扇区 0 */

    s_count = 0;
    s_addr  = LOG_BASE;
    return 0;
}



u8 Log_Append(const LogRec *r)
{
    LogRec tmp = *r;

    if (s_count >= LOG_MAX_REC) return 4;   /* 已满，别再擦/写，防止 s_count 溢出回绕 */

    if (((s_addr - LOG_BASE) % LOG_SECTOR) == 0) {
        if (W25Q64_SectorErase(s_addr)) return 2;
    }
    tmp.crc = CRC16((const u8 *)&tmp, LOG_CRC_LEN);
    if (W25Q64_PageProgram(s_addr, (const u8 *)&tmp, LOG_RECSZ)) return 3;

    s_addr += LOG_RECSZ;
    s_count++;
    return 0;
}

u8 Log_Read(u16 index, LogRec *r)
{
    u8  buf[LOG_RECSZ];
    u16 crc;

    if (index >= s_count) return 1;
    W25Q64_Read(LOG_BASE + (u32)index * LOG_RECSZ, buf, LOG_RECSZ);
    memcpy(r, buf, LOG_RECSZ);
    memcpy(&crc, buf + LOG_CRC_OFF, 2);
    if (CRC16(buf, LOG_CRC_LEN) != crc) return 2;
    return 0;
}

u16 Log_Count(void)
{
    return s_count;
}


u8 W25Q64_ReadSR3(void)
{
    u8 v;
    W25_CS_LOW(); SPI1_RW(0x15); v = SPI1_RW(0xFF); W25_CS_HIGH();
    return v;
}
u8 W25Q64_ClearCMP(void)
{
    u8 sr2;

    W25Q64_WriteEnable();

    W25_CS_LOW();
    SPI1_RW(0x01);        /* Write Status Register */
    SPI1_RW(0x00);        /* SR1 = 0x00（BP=0, TB=0） */
    SPI1_RW(0x00);        /* SR2 = 0x00（重点是 bit6 = 0） */
    W25_CS_HIGH();

    delay_ms(50);
    sr2 = W25Q64_ReadSR2();
    return sr2;
}








