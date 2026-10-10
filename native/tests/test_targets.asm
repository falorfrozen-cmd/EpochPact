; Functions with known first instructions for hook_test.exe: each one exercises one thing
; the hook engine must move (or refuse to move) when it copies a function's first bytes.

PUBLIC g_value, g_flag

.data
g_value DWORD 40
g_flag  BYTE  0

.code

; x + 1, behind the register-save prologue IL2CPP methods open with (5 + 1 + 4 bytes).
t_plain PROC
    mov qword ptr [rsp+8], rbx
    push rdi
    sub rsp, 20h
    lea eax, [rcx+1]
    add rsp, 20h
    pop rdi
    ret
t_plain ENDP

; x + g_value: the first instruction reads memory relative to RIP (8B 05 disp32).
t_riprel PROC
    mov eax, dword ptr [g_value]
    add eax, ecx
    ret
t_riprel ENDP

; IL2CPP's "method initialised?" check: cmp byte ptr [rip+x], 0 is 7 bytes on its own.
t_cmp_flag PROC
    cmp byte ptr [g_flag], 0
    jne flag_set
    mov eax, ecx
    ret
flag_set:
    lea eax, [rcx+100]
    ret
t_cmp_flag ENDP

; A short conditional jump inside the first 5 bytes: test (2) + jz rel8 (2) + mov (5).
t_short_jcc PROC
    test ecx, ecx
    jz was_zero
    mov eax, 7
    ret
was_zero:
    mov eax, 3
    ret
t_short_jcc ENDP

t_helper PROC
    mov eax, 1000
    ret
t_helper ENDP

; The first instruction is a relative call: helper() + x.
t_call PROC
    call t_helper
    add eax, ecx
    ret
t_call ENDP

; Too short: returns within 5 bytes. The engine must refuse.
t_tiny PROC
    xor eax, eax
    ret
t_tiny ENDP

; Starts with a VEX instruction the decoder does not cover. The engine must refuse.
t_vex PROC
    vzeroupper
    xor eax, eax
    ret
t_vex ENDP

END
