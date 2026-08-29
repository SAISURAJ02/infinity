#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

struct registers;
void syscall_handler(struct registers *regs);
extern void syscall_entry(void);

#endif
