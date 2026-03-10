# SandOS - A Lightweight Embedded RTOS

**SandOS** 是一个为嵌入式学习设计的实时操作系统内核，具有抢占式调度、低资源占用等特点。
🚀 **支持架构**:
 - **RISC-V QingKe V4** (如CH32V203)

## ✨ 核心特性

 - **调度器**: 基于位图的 $O(1)$ 优先级查找算法，支持最多 32 个优先级。
 - **任务管理**: 支持任务创建、挂起、延时及栈溢出检测。
 - **同步互斥**:
   - **信号量**: 计数型信号量。
   - **互斥锁**: 支持**优先级继承** 机制，防止优先级翻转；支持递归上锁。
 - **通信机制**: 消息队列，支持结构体数据传输。
 - **内存管理**: 固定块大小的静态内存池，无碎片化风险，$O(1)$ 分配与释放。
 - **时基管理**: 基于 SysTick 的时间片轮转与有序延时链表。

## 🛠️ 目录结构

 - `os_core.c/h`: 内核核心逻辑。
 - `os_cpu.c/h`: 硬件抽象层接口定义及实现。
 - `Portable/`: 特定架构的移植实现。

## ⚡ 快速上手

```c
// 示例：创建一个闪烁 LED 的任务
OS_TCB TCB_Blink;
uint32_t Stack_Blink[128];

void Task_Blink(void *param) {
    while(1) {
        LED_Toggle();
        OS_Delay(500); // 阻塞延时 500ms
    }
}
int main(void) {
    OS_Init(); // 1. 初始化内核
     
    // 2. 创建任务
    OS_TaskCreate(&TCB_Blink, Task_Blink, NULL, Stack_Blink, 128, 5);
     
    // 3. 启动调度器
    OS_StartScheduler(); 
}
```
