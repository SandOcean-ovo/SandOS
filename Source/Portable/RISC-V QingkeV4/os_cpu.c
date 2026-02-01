#include "os_cpu.h"

const uint8_t OS_MapTable[256] = {
    0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
};

#define TICKS_PER_MS (SystemCoreClock / 1000)

/* 私有函数 ------------------------------------------------ */
void OS_TaskReturn(void)
{
    for (;;)
        ;
}

uint32_t *OS_StackInit(OS_TaskFunc_t task_function, void* task_param, uint32_t *stack_init_address, uint32_t stack_depth)
{
    extern void __global_pointer$;
    /* 第一步：找到栈顶 */
    uint32_t *sp = stack_init_address + stack_depth;

    /* 第二步：字节对齐 */
    sp = (uint32_t *)((uint32_t)sp & 0xFFFFFFF0); // 先把sp转成uint32_t，再把最后4位抹成0，最后转成uint32_t *

    /* 第三步：填入数据 */
    *(--sp) = (uint32_t)MSTATUS_VALUE;    // mstatus 机器模式
    *(--sp) = (uint32_t)task_function; // mepc
    *(--sp) = (uint32_t)OS_TaskReturn; // x1：返回地址 类似LR
    *(--sp) = (uint32_t)&__global_pointer$; // gp：全局指针，程序使用gp间接访问全局变量的值，这个值在程序编译完后就固定了，不能填0不然就飞了
    *(--sp) = (uint32_t)task_param; // a0：函数参数
    for(int i = 0; i < 27; ++i){
        *(--sp) = (uint32_t)0x0;           
    }
    /* 第四步：返回sp */
    return sp;
}

void OS_Init_Timer(void)
{
    SysTick->SR &= ~(1 << 0);
    SysTick->CNT = (uint64_t)0;
    SysTick->CMP = (uint64_t)TICKS_PER_MS;
    SysTick->CTLR |= ((1 << 4) | (1 << 3) | (1 << 2));
    SysTick->CTLR |= ((1 << 5) | (1 << 1) | (1 << 0));

    NVIC_SetPriority(SysTick_IRQn, (uint8_t)(0b110 << 5));
    NVIC_SetPriority(Software_IRQn, (uint8_t)(0b111 << 5));
    NVIC_EnableIRQ(SysTick_IRQn);
    NVIC_EnableIRQ(Software_IRQn);
}

void OS_Tick_Reset(void)
{
    SysTick->CTLR |= (1 << 5);
    SysTick->SR &= ~(1 << 0);
}

void OS_Trigger_SWI(void)
{
    SysTick->CTLR |= SysTick_CTLR_SWIE;
}

void OS_Enable_IRQ(void)
{
    __enable_irq();
}

void OS_Disable_IRQ(void)
{
    __disable_irq();
}

uint8_t OS_GetTopPrio(uint32_t PrioMap)
{
    if (PrioMap & 0xFF)
        return OS_MapTable[PrioMap & 0xFF];
    else if (PrioMap & 0xFF00)
        return 8 + OS_MapTable[(PrioMap >> 8) & 0xFF];
    else if (PrioMap & 0xFF0000)
        return 16 + OS_MapTable[(PrioMap >> 16) & 0xFF];
    else
        return 24 + OS_MapTable[(PrioMap >> 24) & 0xFF];
}

