#include "task.h"
#include "idt.h"
#include "gdt.h"
#include "tss.h"
#include "kheap.h"
#include <stddef.h>

#define TASK_FRAME_WORDS_KERNEL 20
#define TASK_FRAME_WORDS_USER 22
#define TASK_FRAME_RIP_INDEX 17
#define TASK_FRAME_CS_INDEX 18
#define TASK_FRAME_RFLAGS_INDEX 19
#define TASK_FRAME_RSP_INDEX 20
#define TASK_FRAME_SS_INDEX 21

#define USER_STACK_SIZE 16384

#define RFLAGS_IF (1ULL << 9)
#define RFLAGS_RESERVED_BIT1 (1ULL << 1)

static task_t task_pool[MAX_TASKS];
static uint32_t task_count = 0;
static uint32_t next_pid = 1;
static uint32_t scheduler_ticks = 0;
static task_t *current_task = NULL;
static task_t *selected_task = NULL;

void task_init(void)
{
    task_count = 0;
    next_pid = 1;
    scheduler_ticks = 0;

    task_t *kernel_task = &task_pool[0];

    kernel_task->id = 0;

    kernel_task->rsp = 0;

    kernel_task->state = TASK_RUNNING;

    kernel_task->entry_point = NULL;
    kernel_task->is_user = false;
    kernel_task->user_stack = NULL;
    kernel_task->user_stack_size = 0;
    kernel_task->next = kernel_task;

    current_task = kernel_task;
    selected_task = kernel_task;

    task_count = 1;
}

static task_t *task_alloc_slot(void (*entry_point)(void)) {
    if (entry_point == NULL || task_count >= MAX_TASKS) {
        return NULL;
    }

    task_t *t = &task_pool[task_count];
    t->id = next_pid++;
    t->state = TASK_READY;
    t->entry_point = entry_point;
    t->is_user = false;
    t->user_stack = NULL;
    t->user_stack_size = 0;

    t->next = current_task;
    task_t *tail = current_task;
    while (tail->next != current_task) {
        tail = tail->next;
    }
    tail->next = t;
    task_count++;

    return t;
}

task_t *task_create(void (*entry_point)(void)) {
    task_t *t = task_alloc_slot(entry_point);
    if (!t) return NULL;

    uint64_t *sp = (uint64_t *)(&t->stack[TASK_STACK_SIZE]);
    sp = (uint64_t *)((uint64_t)sp & ~0xFULL);
    sp -= TASK_FRAME_WORDS_KERNEL;

    for (int i = 0; i < TASK_FRAME_WORDS_KERNEL; i++) {
        sp[i] = 0;
    }

    sp[TASK_FRAME_RIP_INDEX] = (uint64_t)entry_point;
    sp[TASK_FRAME_CS_INDEX] = idt_get_kernel_cs();
    sp[TASK_FRAME_RFLAGS_INDEX] = RFLAGS_IF | RFLAGS_RESERVED_BIT1;

    t->rsp = (uint64_t)sp;

    return t;
}

task_t *task_create_user(void (*entry_point)(void)) {
    task_t *t = task_alloc_slot(entry_point);
    if (!t) return NULL;

    t->is_user = true;
    t->user_stack = (uint8_t *)kmalloc(USER_STACK_SIZE);
    if (!t->user_stack) {
        task_count--;
        next_pid--;
        return NULL;
    }
    t->user_stack_size = USER_STACK_SIZE;

    uint64_t user_sp = (uint64_t)(t->user_stack + USER_STACK_SIZE) & ~0xFULL;
    uint64_t *sp = (uint64_t *)(&t->stack[TASK_STACK_SIZE]);
    sp = (uint64_t *)((uint64_t)sp & ~0xFULL);
    sp -= TASK_FRAME_WORDS_USER;

    for (int i = 0; i < TASK_FRAME_WORDS_USER; i++) {
        sp[i] = 0;
    }

    sp[TASK_FRAME_RIP_INDEX] = (uint64_t)entry_point;
    sp[TASK_FRAME_CS_INDEX] = GDT_USER_CODE_SELECTOR | 3;
    sp[TASK_FRAME_RFLAGS_INDEX] = RFLAGS_IF | RFLAGS_RESERVED_BIT1;
    sp[TASK_FRAME_RSP_INDEX] = user_sp;
    sp[TASK_FRAME_SS_INDEX] = GDT_USER_DATA_SELECTOR | 3;

    t->rsp = (uint64_t)sp;

    return t;
}

void task_exit_current(void) {
    if (!current_task) return;
    current_task->state = TASK_TERMINATED;
}

uint64_t task_schedule(uint64_t current_rsp)
{
    scheduler_ticks++;

    if (current_task == NULL || task_count <= 1) {
        return current_rsp;
    }

    current_task->rsp = current_rsp;
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }

    task_t *candidate = current_task->next;
    task_t *next = current_task;

    for (uint32_t i = 0; i < task_count; i++) {
        if (candidate->state == TASK_READY) {
            next = candidate;
            break;
        }
        candidate = candidate->next;
    }
    
    if (next->state == TASK_TERMINATED) {
        return current_rsp;
    }

    next->state = TASK_RUNNING;
    current_task = next;
    selected_task = next;

    tss_set_rsp0((uint64_t)&next->stack[TASK_STACK_SIZE]);

    return current_task->rsp;
}

task_t *scheduler_current(void) {
    return current_task;
}

task_t *scheduler_selected(void) {
    return selected_task;
}


uint32_t scheduler_task_count(void) {
    return task_count;
}


uint32_t scheduler_tick_count(void) {
    return scheduler_ticks;
}