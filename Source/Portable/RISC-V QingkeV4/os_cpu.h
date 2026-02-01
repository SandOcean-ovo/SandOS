#ifndef __OS_CPU_H
#define __OS_CPU_H

#include "os_common.h"
#include "ch32v20x.h"


#define MSTATUS_VALUE 0x00001880 // mstatus值的初始状态：开启中断，特权模式
#ifndef SysTick_CTLR_SWIE
#define SysTick_CTLR_SWIE (1 << 31)
#endif

extern const uint8_t OS_MapTable[256];

/** @addtogroup Porting 移植接口
 *  @{
 */

/**
 * @brief  初始化任务栈
 * @details 根据 RISC-V 架构的调用约定，初始化任务栈帧。
 *          栈帧包含 x1(ra), x5-x31 等寄存器的初始值。
 * @param  task_function      任务入口函数地址
 * @param  task_param         任务函数的参数
 * @param  stack_init_address 栈底（高地址）或 栈数组起始地址（取决于具体实现，通常是栈数组名）
 * @param  stack_depth        栈深度（单位：字/元素个数）
 * @return uint32_t*          初始化完成后的栈顶指针 (SP)
 */
uint32_t* OS_StackInit(OS_TaskFunc_t task_function, void* task_param, uint32_t *stack_init_address, uint32_t stack_depth);

/**
 * @brief  初始化 SysTick
 * @details 配置 SysTick 时钟源和重装载值，以产生系统节拍。
 */
void OS_Init_Timer(void);

/**
 * @brief  复位 SysTick
 */
void OS_Tick_Reset(void);

/**
 * @brief  触发软件中断 (SWI)
 * @details 用于在任务级触发 PendSV 或类似的软件中断以执行上下文切换。
 */
void OS_Trigger_SWI(void);

/**
 * @brief  开启全局中断
 */
void OS_Enable_IRQ(void);

/**
 * @brief  关闭全局中断
 */
void OS_Disable_IRQ(void);

/**
 * @brief  获取最高优先级
 * @param  PrioMap 优先级位图
 * @return uint8_t 最高优先级的数值
 */
uint8_t OS_GetTopPrio(uint32_t PrioMap);

/** @} */ // end of group Porting

#endif /* __OS_CPU_H */