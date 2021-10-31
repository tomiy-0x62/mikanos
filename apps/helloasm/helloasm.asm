section .data
    msg db      "Hello, World!", 0x0a

section .text
    global _start
_start:
    mov     rax, 1
    mov     rdi, 1
    mov     rsi, msg
    mov     rdx, 14 
    syscall
    mov    rax, 0xe7 ; exit_group 
    mov    rdi, 0
    syscall
