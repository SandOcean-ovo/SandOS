/**
 ******************************************************************************
 * @file    os_core.c
 * @author  SandOcean
 * @version V1.0
 * @date    2025-12-14
 * @brief   RTOS 内核核心逻辑实现
 *
 * 本文件包含 RTOS 的独立于硬件的逻辑实现：
 * - OS 初始化与启动逻辑 (OS_Init, OS_Start)
 * - 任务调度器算法 (Scheduler) 实现
 * - SysTick 时钟节拍处理 (Timebase management)
 * - 阻塞延时处理 (osDelay) 与就绪表管理
 *
 ******************************************************************************
 */

#include "os_core.h"

/* 变量定义 ------------------------------------------------------ */

volatile uint32_t g_SystemTickCount = 0; // 系统心跳计数器

volatile uint32_t g_CriticalNesting = 0; // 临界区嵌套计数器

volatile uint32_t g_PrioMap = 0; // 任务位图

volatile uint8_t g_OSRunning = FALSE; // 任务启动标志位

OS_List ReadyList[OS_MAX_PRIO]; // 任务就绪链表
OS_List DelayList;              // 有序延时链表

OS_TCB *CurrentTCB = NULL;
OS_TCB *NextTCB = NULL;

OS_TCB IdleTaskTCB;
uint32_t IdleTaskStack[IDLE_STACK_SIZE];

/* 私有函数定义 ------------------------------------------------------ */
void OS_CheckStackOverflow(void)
{
    if(CurrentTCB == NULL) return;

    if(*(CurrentTCB->stackLimit) != OS_STACK_MAGIC_VAL || CurrentTCB->stackPtr <= CurrentTCB->stackLimit)
    {
        OS_Disable_IRQ();
        OS_ASSERT(0);
    }
}

void IdleTask(void *param)
{
    for (;;)
        ;
}

void List_Init(OS_List *list)
{
    OS_ASSERT(list != NULL);
    list->Head = NULL;
    list->Tail = NULL;
}

void List_InsertTail(OS_List *list, OS_TCB *tcb)
{
    OS_ASSERT(list != NULL && tcb != NULL);
    tcb->Next = NULL;
    if (list->Head == NULL) // 如果之前链表是空的
    {
        list->Head = tcb;
        list->Tail = tcb; // 链表头尾都是tcb
        tcb->Prev = NULL; // tcb前面没东西
    }
    else // 如果之前链表有东西
    {
        list->Tail->Next = tcb;
        tcb->Prev = list->Tail;
        list->Tail = tcb;
    }
}

void List_Remove(OS_List *list, OS_TCB *tcb)
{
    OS_ASSERT(list != NULL && tcb != NULL);
    if (tcb->Prev == NULL) // 处理前向节点
    {
        list->Head = tcb->Next;
    }
    else
    {
        tcb->Prev->Next = tcb->Next;
    }
    if (tcb->Next == NULL)
    {
        list->Tail = tcb->Prev;
    }
    else
    {
        tcb->Next->Prev = tcb->Prev;
    }

    tcb->Prev = NULL;
    tcb->Next = NULL;
}

OS_TCB *List_PopHead(OS_List *list)
{
    OS_ASSERT(list != NULL);
    OS_TCB *head = list->Head;
    if (list->Head != NULL)
        List_Remove(list, head);
    return head;
}

static void OS_ReadyListAdd(OS_TCB *tcb)
{
    OS_ASSERT(tcb != NULL);
    g_PrioMap |= (1U << tcb->Priority);
    List_InsertTail(&ReadyList[tcb->Priority], tcb);
}

static void OS_ReadyListRemove(OS_TCB *tcb)
{
    OS_ASSERT(tcb != NULL);
    List_Remove(&ReadyList[tcb->Priority], tcb);
    if (ReadyList[tcb->Priority].Head == NULL)
        g_PrioMap &= ~(1U << tcb->Priority);
}

OS_TCB *FindNextTask(void)
{
    OS_ASSERT(g_PrioMap != 0);

    uint8_t TopPrio = OS_GetTopPrio(g_PrioMap);

    OS_TCB *next_task = ReadyList[TopPrio].Head;
    OS_ASSERT(next_task != NULL);
    return next_task;
}

/* 函数实现 ----------------------------------------------------------- */

OS_Status OS_TaskCreate(OS_TCB *tcb, OS_TaskFunc_t task_function, void *task_param, uint32_t *stack_init_address, uint32_t stack_depth, uint8_t priority)
{
    if (tcb == NULL || task_function == NULL || stack_init_address == NULL || priority > OS_MAX_PRIO - 1)
        return OS_ERR_PARAM;
    tcb->stackPtr = OS_StackInit(task_function, task_param, stack_init_address, stack_depth);
    
    tcb->stackLimit = stack_init_address;
    *(tcb->stackLimit) = OS_STACK_MAGIC_VAL;

    tcb->DelayTicks = 0;
    tcb->State = TASK_READY;
    tcb->Priority = priority;
    tcb->OriginalPrio = priority;

    OS_ReadyListAdd(tcb);
    return OS_OK;
}

void OS_Init(void)
{
    // 1. 初始化全局变量
    g_OSRunning = FALSE;
    g_SystemTickCount = 0;
    g_CriticalNesting = 0;
    g_PrioMap = 0; // 清空位图

    // 2. 初始化就绪链表
    for (int i = 0; i < OS_MAX_PRIO; i++)
    {
        List_Init(&ReadyList[i]);
    }

    // 3. 初始化延时链表
    List_Init(&DelayList);

    // 4. 创建空闲任务
    OS_TaskCreate(&IdleTaskTCB, IdleTask, NULL, IdleTaskStack, IDLE_STACK_SIZE, OS_MAX_PRIO - 1);
}

void OS_StartScheduler(void)
{
    extern void OS_StartFirstTask(void); // From os_cpu_a.S

    // 设置 CurrentTCB 为第一个要运行的任务
    CurrentTCB = FindNextTask();

    // 初始化 SysTick (开启时间片，开始 1ms 中断)
    OS_Init_Timer();

    g_OSRunning = TRUE;

    // 触发 SWI，开始第一次切换！
    OS_StartFirstTask();

    // 应该永远不会执行到这里
    while (1)
        ;
}

void OS_Tick_Handler(void)
{
    if (g_OSRunning != TRUE)
        return;

    // 1. 安全检查
    OS_ASSERT(CurrentTCB != NULL);

    OS_CheckStackOverflow(); // 栈溢出检测

    // 2. 更新系统时间
    g_SystemTickCount++;

    if (DelayList.Head != NULL)
    {
        if (DelayList.Head->DelayTicks > 0)
            DelayList.Head->DelayTicks--;
        while (DelayList.Head->DelayTicks == 0 && DelayList.Head != NULL)
        {
            OS_TCB *tcb_to_wake = List_PopHead(&DelayList);
            tcb_to_wake->State = TASK_READY;
            OS_ReadyListAdd(tcb_to_wake);
        }
    }

    OS_List *ls = &ReadyList[CurrentTCB->Priority];

    if (CurrentTCB->State == TASK_READY && ls->Head != ls->Tail)
    {
        List_Remove(ls, CurrentTCB);
        List_InsertTail(ls, CurrentTCB);
    }

    // 4. 核心调度逻辑
    NextTCB = FindNextTask();

    if (NextTCB != CurrentTCB)
    {
        OS_Trigger_SWI();
    }
}

void OS_Delay(uint32_t ticks)
{
    OS_EnterCritical();

    CurrentTCB->State = TASK_BLOCKED;
    OS_ReadyListRemove(CurrentTCB);

    if (DelayList.Head == NULL)
    {
        // 情况A: 列表为空，直接作为头部
        CurrentTCB->DelayTicks = ticks;
        List_InsertTail(&DelayList, CurrentTCB);
    }
    else
    {
        OS_TCB *iter = DelayList.Head;

        // 寻找插入位置：减去前面的时间，直到剩余 ticks 小于当前节点
        while (iter != NULL && ticks >= iter->DelayTicks)
        {
            ticks -= iter->DelayTicks;
            iter = iter->Next;
        }

        // 此时 ticks 为相对于前一个节点的剩余时间
        CurrentTCB->DelayTicks = ticks;

        if (iter == NULL)
        {
            // 情况B: 插入到链表尾部 (比列表中所有任务延时都长)
            List_InsertTail(&DelayList, CurrentTCB);
        }
        else if (iter == DelayList.Head)
        {
            // 情况C: 插入到链表头部 (比当前头部还要快唤醒)
            iter->DelayTicks -= ticks; // 修正原头部的相对时间

            // 链表头插操作
            CurrentTCB->Next = iter;
            iter->Prev = CurrentTCB;
            CurrentTCB->Prev = NULL;
            DelayList.Head = CurrentTCB;
        }
        else
        {
            // 情况D: 插入到链表中间
            iter->DelayTicks -= ticks; // 修正后继节点的相对时间

            // 插入到 iter 之前
            CurrentTCB->Next = iter;
            CurrentTCB->Prev = iter->Prev;
            iter->Prev->Next = CurrentTCB;
            iter->Prev = CurrentTCB;
        }
    }

    NextTCB = FindNextTask();

    OS_Trigger_SWI();

    OS_ExitCritical(); /* 修改成我们的进入退出临界区函数 */
}

void OS_EnterCritical(void)
{
    OS_Disable_IRQ();
    g_CriticalNesting++;
}

void OS_ExitCritical(void)
{
    OS_ASSERT(g_CriticalNesting != 0);

    g_CriticalNesting--;
    if (g_CriticalNesting == 0)
    {
        OS_Enable_IRQ();
    }
}

OS_Status OS_SemInit(OS_Sem *p_sem)
{
    if (p_sem == NULL)
        return OS_ERR_PARAM;
    List_Init(&p_sem->WaitList);
    return OS_OK;
}

OS_Status OS_SemWait(OS_Sem *p_sem)
{
    if (p_sem == NULL)
        return OS_ERR_PARAM;
    OS_EnterCritical();
    if (p_sem->count > 0) // 原本就有信号量
    {
        p_sem->count--;
        OS_ExitCritical();
        return OS_OK; // 成功返回
    }
    else // 原本没信号量，我睡觉去了，直到信号量来了
    {
        CurrentTCB->State = TASK_BLOCKED; // 设置当前任务状态

        OS_ReadyListRemove(CurrentTCB);

        List_InsertTail(&p_sem->WaitList, CurrentTCB);

        NextTCB = FindNextTask();
        OS_Trigger_SWI();
        OS_ExitCritical();

        return OS_OK;
    }
}

OS_Status OS_SemPost(OS_Sem *p_sem)
{
    if (p_sem == NULL)
        return OS_ERR_PARAM;
    OS_EnterCritical();
    if (p_sem->WaitList.Head == NULL)
    {
        p_sem->count++;
        OS_ExitCritical();
        return OS_OK;
    }
    else
    {
        OS_TCB *TaskToWake = List_PopHead(&p_sem->WaitList);
        TaskToWake->State = TASK_READY;

        OS_ReadyListAdd(TaskToWake);

        NextTCB = FindNextTask();
        OS_Trigger_SWI();
        OS_ExitCritical();

        return OS_OK;
    }
}

OS_Status OS_MutexInit(OS_Mutex *p_mutex)
{
    if (p_mutex == NULL)
        return OS_ERR_PARAM;
    p_mutex->Owner = NULL;
    p_mutex->NestCount = 0;
    p_mutex->OriginalPrio = OS_MAX_PRIO - 1;
    List_Init(&p_mutex->WaitList);
    return OS_OK;
}

OS_Status OS_MutexPend(OS_Mutex *p_mutex)
{
    if (p_mutex == NULL)
        return OS_ERR_PARAM;

    OS_EnterCritical();

    if (p_mutex->Owner == NULL)
    {
        p_mutex->Owner = CurrentTCB;
        p_mutex->NestCount = 1;
        OS_ExitCritical();
        return OS_OK;
    }
    else if (p_mutex->Owner == CurrentTCB)
    {
        p_mutex->NestCount++;
        OS_ExitCritical();
        return OS_OK;
    }
    else
    {
        if (CurrentTCB->Priority < p_mutex->Owner->Priority)
        {
            if (p_mutex->Owner->State == TASK_READY)
            {
                OS_ReadyListRemove(p_mutex->Owner);
                p_mutex->Owner->Priority = CurrentTCB->Priority;
                OS_ReadyListAdd(p_mutex->Owner);
            }
            else
            {
                p_mutex->Owner->Priority = CurrentTCB->Priority;
            }
        }
        CurrentTCB->State = TASK_BLOCKED;
        OS_ReadyListRemove(CurrentTCB);
        if (p_mutex->WaitList.Head == NULL)
        {
            List_InsertTail(&p_mutex->WaitList, CurrentTCB);
        }
        else
        {
            if (p_mutex->WaitList.Head->Priority > CurrentTCB->Priority)
            {
                CurrentTCB->Next = p_mutex->WaitList.Head;
                p_mutex->WaitList.Head->Prev = CurrentTCB;
                CurrentTCB->Prev = NULL;
                p_mutex->WaitList.Head = CurrentTCB;
            }
            else
            {
                OS_TCB *iter = p_mutex->WaitList.Head;
                while (iter->Next != NULL && iter->Next->Priority <= CurrentTCB->Priority)
                {
                    iter = iter->Next;
                }
                CurrentTCB->Next = iter->Next;
                CurrentTCB->Prev = iter;
                if (iter->Next != NULL) // 如果 iter 不是最后一个节点
                {
                    iter->Next->Prev = CurrentTCB; // 让后面的人指向我
                }
                else
                {
                    p_mutex->WaitList.Tail = CurrentTCB;
                }
                iter->Next = CurrentTCB;
            }
        }
        NextTCB = FindNextTask();
        OS_Trigger_SWI();
        OS_ExitCritical();
        return OS_OK;
    }
}

OS_Status OS_MutexPost(OS_Mutex *p_mutex)
{
    if (p_mutex == NULL)
        return OS_ERR_PARAM;

    OS_EnterCritical();

    if (p_mutex->Owner != CurrentTCB)
    {
        OS_ExitCritical();
        return OS_ERR_NOT_OWNER;
    }

    p_mutex->NestCount--;

    if (p_mutex->NestCount > 0)
    {
        OS_ExitCritical();
        return OS_OK;
    }

    if (p_mutex->NestCount == 0)
    {
        if (CurrentTCB->Priority != CurrentTCB->OriginalPrio)
        {
            OS_ReadyListRemove(CurrentTCB);
            CurrentTCB->Priority = CurrentTCB->OriginalPrio;
            OS_ReadyListAdd(CurrentTCB);
        }
        if (p_mutex->WaitList.Head == NULL)
        {
            p_mutex->Owner = NULL;
            OS_ExitCritical();
            return OS_OK;
        }
        OS_TCB *TaskToWake = List_PopHead(&p_mutex->WaitList);
        p_mutex->Owner = TaskToWake;
        p_mutex->NestCount = 1;
        OS_ReadyListAdd(TaskToWake);
        NextTCB = FindNextTask();

        OS_Trigger_SWI();
        OS_ExitCritical();
        return OS_OK;
    }
}

void OS_QueueInit(OS_Queue *p_queue, void *buffer, uint16_t msg_size, uint16_t queue_size)
{
    if ((p_queue == NULL) || (buffer == NULL) || (msg_size == 0) || (queue_size == 0))
        return;

    p_queue->Buffer = buffer;
    p_queue->MsgSize = msg_size;
    p_queue->QSize = queue_size;
    p_queue->MsgCount = 0;
    p_queue->Head = 0;
    p_queue->Tail = 0;
    List_Init(&p_queue->WaitReadList);
}

OS_Status OS_QueueSend(OS_Queue *p_queue, void *p_msg)
{
    if (p_queue == NULL || p_msg == NULL)
        return OS_ERR_PARAM;

    OS_EnterCritical();

    if (p_queue->MsgCount >= p_queue->QSize) // 队列满
    {
        OS_ExitCritical();
        return OS_ERR_Q_FULL;
    }
    /* 处理写入地址 */
    uint8_t *WriteAddr = (uint8_t *)p_queue->Buffer + ((p_queue->Head) * (p_queue->MsgSize));
    /* 拷贝 */
    memcpy(WriteAddr, p_msg, p_queue->MsgSize);
    /* 处理写指针 Head = (Head + 1) % QSize 防止回绕 */
    p_queue->Head = (p_queue->Head + 1) % p_queue->QSize;
    /* 消息数 + 1 */
    p_queue->MsgCount++;

    if (p_queue->WaitReadList.Head != NULL) // 如果有人在等，直接触发任务切换
    {
        OS_TCB *TaskToWake = List_PopHead(&p_queue->WaitReadList);
        TaskToWake->State = TASK_READY;
        OS_ReadyListAdd(TaskToWake);
        NextTCB = FindNextTask();
        OS_Trigger_SWI();
    }
    OS_ExitCritical();

    return OS_OK;
}

OS_Status OS_QueueReceive(OS_Queue *p_queue, void *p_msg_buffer)
{
    if (p_queue == NULL || p_msg_buffer == NULL)
        return OS_ERR_PARAM;

    OS_EnterCritical();

    while (p_queue->MsgCount == 0) // 队列里无数据
    {
        /* 当前任务进入阻塞态，等待下一次切回来 */
        CurrentTCB->State = TASK_BLOCKED;
        OS_ReadyListRemove(CurrentTCB);
        List_InsertTail(&p_queue->WaitReadList, CurrentTCB);
        NextTCB = FindNextTask();
        OS_Trigger_SWI();
        OS_ExitCritical();

        /* 回来了，此时重新查看队列里是否有数据 */
        OS_EnterCritical();
    }

    uint8_t *ReadAddr = (uint8_t *)p_queue->Buffer + ((p_queue->Tail) * (p_queue->MsgSize));
    memcpy(p_msg_buffer, ReadAddr, p_queue->MsgSize);
    p_queue->Tail = (p_queue->Tail + 1) % p_queue->QSize;
    p_queue->MsgCount--;

    OS_ExitCritical();
    return OS_OK;
}

void OS_MemInit(OS_Mem *p_mem, void *start_addr, uint32_t blocks, uint32_t block_size)
{
    if(p_mem == NULL || start_addr == NULL || blocks == 0 || (block_size < sizeof(void*))) return;

    p_mem->Addr = start_addr;
    p_mem->FreeList = start_addr;
    p_mem->BlockSize = block_size;
    p_mem->TotalBlocks = blocks;
    p_mem->FreeBlocks = blocks;
    List_Init(&p_mem->WaitList);

    void **pp_link;
    uint8_t *p_block = (uint8_t *)start_addr; 
    for(uint32_t i = 0; i < (blocks - 1); ++i)
    {
        /* 取出当前块的起始地址 */
        pp_link = (void **)p_block;

        /* 计算下一个块的起始地址 */
        uint8_t *p_next = p_block + block_size;

        /* 在当前块的首地址写入下一块的首地址 */
        *pp_link = (void *)p_next;

        /* 移动到下一个块 */
        p_block = p_next;
    }

    /* 最后一个块写 NULL */
    pp_link = (void **)p_block;
    *pp_link = NULL;
}

void *OS_MemGet(OS_Mem *p_mem)
{
    if(p_mem == NULL) return NULL;

    OS_EnterCritical();

    while(p_mem->FreeBlocks == 0)
    {
        CurrentTCB->State = TASK_BLOCKED;
        OS_ReadyListRemove(CurrentTCB);
        List_InsertTail(&p_mem->WaitList, CurrentTCB);

        NextTCB = FindNextTask();
        OS_Trigger_SWI();
        OS_ExitCritical();

        OS_EnterCritical();
    }

    void *ret = p_mem->FreeList;
    p_mem->FreeList = *(void **)ret;
    p_mem->FreeBlocks--;
    OS_ExitCritical();

    return ret;
}

OS_Status OS_MemPut(OS_Mem *p_mem, void *p_block)
{
    if(p_mem == NULL || p_block == NULL) return OS_ERR_PARAM;

    OS_EnterCritical();

    /* 安全检查 */
    uint8_t *start_addr = (uint8_t *)p_mem->Addr;
    uint8_t *block_addr = (uint8_t *)p_block;
    uint32_t total_size = p_mem->TotalBlocks * p_mem->BlockSize;

    if (block_addr < start_addr || block_addr >= (start_addr + total_size))
    {
        OS_ExitCritical();
        return OS_ERR_INVALID_ADDR;
    }

    if(((uint32_t)(block_addr - start_addr) % p_mem->BlockSize) != 0)
    {
        OS_ExitCritical();
        return OS_ERR_NOT_ALIGN;
    }

    // 将当前的 FreeList (旧链表头) 存入要释放的块中
    *(void **)p_block = p_mem->FreeList;
    // 更新 FreeList 指向当前块 (新链表头)
    p_mem->FreeList = p_block;
    p_mem->FreeBlocks++;

    if (p_mem->WaitList.Head != NULL)
    {
        OS_TCB *TaskToWake = List_PopHead(&p_mem->WaitList);
        TaskToWake->State = TASK_READY;
        OS_ReadyListAdd(TaskToWake);
        
        // 触发调度
        NextTCB = FindNextTask();
        OS_Trigger_SWI();
    }

    OS_ExitCritical();
    return OS_OK;
}

void OS_AssertFailed(const char *file, int line)
{
    OS_Disable_IRQ();
    // printf("OS ASSERT FAILED !!!\r\n");
    // printf("File: %s\r\n", file);
    // printf("Line: %d\r\n", line);
    while (1)
    {
        // LED_Toggle();
        for (volatile int i = 0; i < 1000000; ++i)
            ;
    }
}