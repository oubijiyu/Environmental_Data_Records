#include "key.h"
#include "delay.h"
#include "oled.h"
#include "W25Q64.h"

/* ==================== 按键与日志浏览 ==================== */

/* 按键引脚：PB12 = 按键1(上一条)，PB13 = 按键2(下一条)
   和实物接反时只交换下面两行 */
#define BTN_PREV_PIN            GPIO_Pin_12
#define BTN_NEXT_PIN            GPIO_Pin_13

/* 时间参数：都以 5ms 扫描周期为单位 */
#define KEY_TICK_MS             5                       /* 扫描周期 5ms */
#define KEY_DEBOUNCE            3                       /* 15ms 消抖 */
#define KEY_LONG_TICKS          (500 / KEY_TICK_MS)     /* 500ms 长按 */
#define KEY_REPEAT_TICKS        (120 / KEY_TICK_MS)     /* 连发 120ms 一条 */
#define KEY_FAST_TICKS          (60  / KEY_TICK_MS)     /* 连发 10 次后 60ms */
#define KEY_FAST_AFTER          10
#define KEY_DOUBLE_TICKS        (300 / KEY_TICK_MS)     /* 双击窗口 300ms */

#define BTN_PREV_DOWN()         (GPIO_ReadInputDataBit(GPIOB, BTN_PREV_PIN) == Bit_RESET)
#define BTN_NEXT_DOWN()         (GPIO_ReadInputDataBit(GPIOB, BTN_NEXT_PIN) == Bit_RESET)

typedef void (*KeyActFn)(void);

typedef struct {
    u8       level;         /* 消抖后的电平：1=按下 */
    u8       raw_cnt;       /* 消抖计数 */
    u16      hold;          /* 按住时长(tick) */
    u8       repeat;        /* 是否已进入连发 */
    u8       repeat_cnt;    /* 已连发次数 */
    u8       wait_double;   /* 等待第二次按下(双击) */
    u16      click_tick;    /* 上次松开时刻 */
    KeyActFn act_single;    /* 单击/连发时执行 */
    KeyActFn act_double;    /* 双击时执行 */
} KeyState;

static u16      key_tick      = 0;
static u16      view_index    = 0;      /* 当前查看第几条(0 起) */
static u8       follow_latest = 1;      /* 1 = 跟随最新一条 */
static KeyState key1, key2;

/* 用空格清掉一整行，避免旧字符残留 */
static void OLED_ClearLine(u8 Line)
{
    u8 i;
    for (i = 1; i <= 16; i++) {
        OLED_ShowChar(Line, i, ' ');
    }
}

/* 按 view_index / follow_latest 刷新第 3、4 行 */
static void LogView_Refresh(void)
{
    u16   cnt;
    u16   idx;
    LogRec r;

    cnt = Log_Count();

    if (cnt == 0) {
        follow_latest = 1;
        view_index = 0;
        OLED_ShowString(3, 1, "SAVE:");
        OLED_ShowNum(3, 6, 0, 4);
        OLED_ClearLine(4);
        OLED_ShowString(4, 1, "NO DATA");
        return;
    }

    if (follow_latest) {
        idx = (u16)(cnt - 1);
    } else {
        idx = view_index;
        if (idx >= cnt) idx = (u16)(cnt - 1);
    }
    view_index = idx;

    OLED_ShowString(3, 1, "SAVE:");
    OLED_ShowNum(3, 6, (u32)idx + 1, 4);

    if (Log_Read(idx, &r) == 0) {
        OLED_ShowString(4, 1, "F:");
        OLED_ShowNum(4, 3, r.temp, 2);
        OLED_ShowString(4, 5, " H:");
        OLED_ShowNum(4, 8, r.humi, 2);
        OLED_ShowString(4, 10, " L:");
        OLED_ShowNum(4, 13, r.light, 3);
    } else {
        OLED_ClearLine(4);
        OLED_ShowString(4, 1, "READ ERR");
    }
}

/* 按键1：上一条 */
static void LogView_Prev(void)
{
    u16 cnt = Log_Count();

    if (cnt == 0) return;

    if (follow_latest) {
        follow_latest = 0;
        view_index = (u16)(cnt - 1);
    }
    if (view_index == 0) return;        /* 已经是 SAVE:0000 */
    view_index--;
    LogView_Refresh();
}

/* 按键2：下一条 */
static void LogView_Next(void)
{
    u16 cnt = Log_Count();
    u16 target;

    if (cnt == 0) return;
    if (follow_latest) return;          /* 已在最新，只显示最新 */

    target = (u16)(view_index + 1);
    if (target >= cnt) target = (u16)(cnt - 1);
    if (target == (u16)(cnt - 1)) follow_latest = 1;   /* 到最新就恢复跟随 */

    view_index = target;
    LogView_Refresh();
}

/* 按键2双击：直接跳到最新 */
static void LogView_Latest(void)
{
    follow_latest = 1;
    LogView_Refresh();
}

/* 单个按键的状态机：消抖、单击、长按连发、双击 */
static void Key_Update(KeyState *k, u8 Down)
{
    u16 Step;

    if (Down != k->level) {
        if (++k->raw_cnt < KEY_DEBOUNCE) return;   /* 还在抖 */
        k->raw_cnt = 0;
        k->level   = Down;

        if (Down) {                                /* 按下 */
            k->hold       = 0;
            k->repeat     = 0;
            k->repeat_cnt = 0;

            if (k->wait_double && (u16)(key_tick - k->click_tick) <= KEY_DOUBLE_TICKS) {
                k->wait_double = 0;
                k->act_double();
            } else {
                k->wait_double = 0;
                k->act_single();
            }
        } else {                                   /* 松开 */
            if (!k->repeat && k->hold < KEY_LONG_TICKS) {
                k->wait_double = 1;                /* 短按，等可能的第二击 */
                k->click_tick  = key_tick;
            }
        }
    } else {
        k->raw_cnt = 0;
    }

    if (!k->level) return;

    if (!k->repeat) {                              /* 还没到长按时间 */
        if (++k->hold >= KEY_LONG_TICKS) {
            k->repeat     = 1;
            k->hold       = 0;
            k->repeat_cnt = 0;
            k->act_single();                       /* 进入连发，先走一步 */
        }
        return;
    }

    Step = (k->repeat_cnt < KEY_FAST_AFTER) ? KEY_REPEAT_TICKS : KEY_FAST_TICKS;
    if (++k->hold >= Step) {
        k->hold = 0;
        if (k->repeat_cnt < 255) k->repeat_cnt++;
        k->act_single();
    }
}

/* 每 5ms 扫描一次（Key_WaitMs 内部调用） */
static void Key_Scan(void)
{
    key_tick++;
    Key_Update(&key1, (u8)(BTN_PREV_DOWN() ? 1 : 0));
    Key_Update(&key2, (u8)(BTN_NEXT_DOWN() ? 1 : 0));
}

/* ---------------- 对外接口 ---------------- */

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStruct.GPIO_Pin   = BTN_PREV_PIN | BTN_NEXT_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    key_tick      = 0;
    view_index    = 0;
    follow_latest = 1;

    key1.level       = BTN_PREV_DOWN() ? 1 : 0;
    key1.raw_cnt     = 0;
    key1.hold        = 0;
    key1.repeat      = 0;
    key1.repeat_cnt  = 0;
    key1.wait_double = 0;
    key1.click_tick  = 0;
    key1.act_single  = LogView_Prev;
    key1.act_double  = LogView_Prev;

    key2.level       = BTN_NEXT_DOWN() ? 1 : 0;
    key2.raw_cnt     = 0;
    key2.hold        = 0;
    key2.repeat      = 0;
    key2.repeat_cnt  = 0;
    key2.wait_double = 0;
    key2.click_tick  = 0;
    key2.act_single  = LogView_Next;
    key2.act_double  = LogView_Latest;
}

void Key_WaitMs(u16 ms)
{
    u16 step;

    while (ms) {
        step = (ms > KEY_TICK_MS) ? KEY_TICK_MS : ms;
        delay_ms(step);
        Key_Scan();
        ms -= step;
    }
}

void Key_LogViewUpdate(void)
{
    if (follow_latest) {
        LogView_Refresh();
    }
}

/* 上电时同时按住两个键 3 秒：清空日志，SAVE 变 0000 */
void Key_BootCheckClear(void)
{
    u16 i;

    if (!(BTN_PREV_DOWN() && BTN_NEXT_DOWN())) return;   /* 没同时按，直接跳过 */

    OLED_ShowString(2, 1, "CLEAR? HOLD 3S");

    for (i = 0; i < 300; i++) {                          /* 300 × 10ms = 3s */
        delay_ms(10);
        if (!(BTN_PREV_DOWN() && BTN_NEXT_DOWN())) {     /* 中途松手，取消 */
            OLED_ClearLine(2);
            Key_Init();
            return;
        }
    }

    while (BTN_PREV_DOWN() && BTN_NEXT_DOWN());          /* 等松手 */

    if (Log_Clear() == 0) {
        follow_latest = 1;
        view_index    = 0;
        OLED_ShowString(2, 1, "LOG CLEARED");
        LogView_Refresh();                               /* 立即显示 SAVE:0000 */
    } else {
        OLED_ShowString(2, 1, "CLEAR FAIL");
    }

    delay_ms(1000);
    OLED_ClearLine(2);

    Key_Init();      /* 重新同步按键状态，避免刚松手被当成一次短按 */
}

