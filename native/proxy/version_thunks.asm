; Generic x64 forwarding preserves register and stack arguments, including the
; undocumented GetFileVersionInfoByHandle ABI. Only first calls enter the resolver.
EXTERN g_real:QWORD
EXTERN ep_ResolveVersion:PROC

FORWARD MACRO name, index
LOCAL ready
name PROC FRAME
    ; Entry RSP is 8 mod 16. Reserve shadow space and volatile argument registers.
    sub rsp, 0A8h
    .allocstack 0A8h
    .endprolog
    mov rax, qword ptr [g_real + index*8]
    test rax, rax
    jnz ready
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    movdqu [rsp+60h], xmm2
    movdqu [rsp+70h], xmm3
    mov ecx, index
    call ep_ResolveVersion
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    mov r8, [rsp+30h]
    mov r9, [rsp+38h]
    movdqu xmm0, [rsp+40h]
    movdqu xmm1, [rsp+50h]
    movdqu xmm2, [rsp+60h]
    movdqu xmm3, [rsp+70h]
ready:
    add rsp, 0A8h
    jmp rax
name ENDP
ENDM

.code
FORWARD ep_GetFileVersionInfoA, 0
FORWARD ep_GetFileVersionInfoByHandle, 1
FORWARD ep_GetFileVersionInfoExA, 2
FORWARD ep_GetFileVersionInfoExW, 3
FORWARD ep_GetFileVersionInfoSizeA, 4
FORWARD ep_GetFileVersionInfoSizeExA, 5
FORWARD ep_GetFileVersionInfoSizeExW, 6
FORWARD ep_GetFileVersionInfoSizeW, 7
FORWARD ep_GetFileVersionInfoW, 8
FORWARD ep_VerFindFileA, 9
FORWARD ep_VerFindFileW, 10
FORWARD ep_VerInstallFileA, 11
FORWARD ep_VerInstallFileW, 12
FORWARD ep_VerQueryValueA, 13
FORWARD ep_VerQueryValueW, 14

END
