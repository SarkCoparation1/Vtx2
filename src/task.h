#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_TASKS 16
#define TASK_STACK_SIZE 8192

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_TERMINATED
} task_state_t;

typedef struct task task_t;

struct task {
    uint32_t id;
    uint64_t rsp;
    task_state_t state;
    void (*entry_point)(void);
    bool is_user;
    uint8_t *user_stack;
    uint64_t user_stack_size;
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    task_t *next;
};


void task_init(void);
task_t *task_create(void (*entry_point)(void));
task_t *task_create_user(void (*entry_point)(void));
void task_exit_current(void);
uint64_t task_schedule(uint64_t current_rsp);
task_t *scheduler_current(void);
task_t *scheduler_selected(void);
uint32_t scheduler_task_count(void);
uint32_t scheduler_tick_count(void);

#endif