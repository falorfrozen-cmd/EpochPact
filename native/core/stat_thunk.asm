; Calls BaseStats.ChangeStatModifier(SP.Movespeed, delta, INCREASED, 0, 0, 0, false)
; exactly the way the game's own CharacterMutator.UpdateSwiftness calls it (verified by
; disassembly at 0x266918E): rcx = BaseStats, dl = SP byte, xmm2 = value, r9d = ModType,
; [rsp+20h] = checkTags, [rsp+28h] = specialTag byte, [rsp+30h] = extraTag int,
; [rsp+38h] = bool, [rsp+40h] = MethodInfo, call [vtable+0x2F8].
;
; void ep_call_change_stat(void* stats, float delta, void* fn, void* method);
; C++ ABI: rcx = stats, xmm0 = delta, rdx = fn, r8 = method.
option casemap:none

.code
public ep_call_change_stat

ep_call_change_stat proc
    sub     rsp, 68h                ; 0x20 shadow + 5 outgoing slots, 16-byte aligned
    mov     r10, rdx                ; fn (rdx is needed for the SP byte)
    mov     qword ptr [rsp+40h], r8 ; MethodInfo
    mov     qword ptr [rsp+38h], 0  ; bool
    mov     qword ptr [rsp+30h], 0  ; extraTag
    mov     qword ptr [rsp+28h], 0  ; specialTag
    mov     qword ptr [rsp+20h], 0  ; checkTags
    mov     r9d, 1                  ; BaseStats.ModType.INCREASED
    mov     dl, 9                   ; SP.Movespeed
    movaps  xmm2, xmm0              ; value
    call    r10
    add     rsp, 68h
    ret
ep_call_change_stat endp

end
