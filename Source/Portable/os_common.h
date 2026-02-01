#ifndef __OS_COMMON_H
#define __OS_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* 宏定义 ----------------------------------------------------------- */
#define TRUE 1
#define FALSE 0

/**
 * @brief  断言宏
 * @details 如果表达式 x 为假 (0)，则调用 OS_AssertFailed 停止系统运行。
 */
#define OS_ASSERT(x)   if( (x) == 0 ) { OS_AssertFailed(__FILE__, __LINE__); }

/* 类型定义 ---------------------------------------------------------- */

/**
 * @brief  任务函数指针类型
 * @param  p_arg 传递给任务的参数指针
 */
typedef void (*OS_TaskFunc_t)(void *p_arg);

/* 函数声明 ---------------------------------------------------------- */

/**
 * @brief  断言失败处理函数
 * @param  file 发生断言的文件名
 * @param  line 发生断言的行号
 */
void OS_AssertFailed(const char *file, int line);

#endif
