#ifndef SEVENTHOS_PROCESS_H
#define SEVENTHOS_PROCESS_H

#include <stdint.h>
#include "idt.h"
#include "thread.h"

#define USER_REGION_BASE 0x40000000u
#define USER_SLOT_SIZE 0x00010000u
#define USER_STACK_OFFSET 0x00004000u
#define USER_STACK_PAGES 2u
#define PROCESS_MAX 4u

typedef enum
{
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_EXITED,
    PROC_FAULTED, // dibunuh kernel karena pelanggaran proteksi
    PROC_KILLED   // dibunuh lewat 'kill'
} proc_state_t;

typedef struct process
{
    uint32_t pid;
    char name[16];
    proc_state_t state;
    struct thread *thread;
    uint32_t entry;
    uint32_t code_base;
    uint32_t stack_top;
    uint8_t code_mapped;
    uint8_t stack_pages_mapped;
    int32_t exit_code;
    uint32_t fault_vector;
    uint32_t fault_addr;
} process_t;

int process_spawn(const char *program); // pid (>0) atau negatif
void process_exit(int code) __attribute__((noreturn));
void process_handle_fault(isr_frame_t *frame, const char *reason, uint32_t address)
    __attribute__((noreturn));
void process_thread_cleanup(thread_t *t); // dipanggil dari thread.c

uint32_t process_current_pid(void);
int process_is_running(uint32_t pid);
int process_program_is_background(const char *program);
void process_list_programs(void);
void process_print_table(void);
void process_print_result(uint32_t pid);
int process_get_result(uint32_t pid, proc_state_t *state, int32_t *exit_code, uint32_t *fault_vector);

#endif