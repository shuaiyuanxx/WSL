.section .rodata
msg: .ascii "hello world from [assembly]\n"
len = . - msg
.section .text
.globl _start
_start:
    mov $1, %rax
    mov $1, %rdi
    lea msg(%rip), %rsi
    mov $len, %rdx
    syscall
    mov $60, %rax
    xor %rdi, %rdi
    syscall
