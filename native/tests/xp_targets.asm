; A stand-in for ExperienceTracker.GainExpFromEnemyOrMote(long) for xp_test.exe: the same
; IL2CPP calling convention (self in rcx, amount in rdx, MethodInfo* in r8) and the same
; first instruction (mov [rsp+20h], rbx), followed by an IL2CPP-style flag compare. It
; records what it was called with.

PUBLIC g_gainSelf, g_gainAmount, g_gainMethod, g_gainCalls

.data
g_gainSelf   QWORD 0
g_gainAmount QWORD 0
g_gainMethod QWORD 0
g_gainCalls  QWORD 0
g_initFlag   BYTE  1

.code

t_gain PROC
    mov qword ptr [rsp+20h], rbx
    push rdi
    sub rsp, 30h
    cmp byte ptr [g_initFlag], 0
    mov qword ptr [g_gainSelf], rcx
    mov qword ptr [g_gainAmount], rdx
    mov qword ptr [g_gainMethod], r8
    inc qword ptr [g_gainCalls]
    add rsp, 30h
    pop rdi
    mov rbx, qword ptr [rsp+20h]
    ret
t_gain ENDP

END
