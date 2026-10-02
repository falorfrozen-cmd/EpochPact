; version.dll export thunks: each jumps to the real System32 function, so every
; signature is forwarded unchanged. g_real is filled by version_proxy.cpp.
EXTERN g_real:QWORD

.code
ep_GetFileVersionInfoA PROC
    jmp qword ptr [g_real + 0]
ep_GetFileVersionInfoA ENDP

ep_GetFileVersionInfoByHandle PROC
    jmp qword ptr [g_real + 8]
ep_GetFileVersionInfoByHandle ENDP

ep_GetFileVersionInfoExA PROC
    jmp qword ptr [g_real + 16]
ep_GetFileVersionInfoExA ENDP

ep_GetFileVersionInfoExW PROC
    jmp qword ptr [g_real + 24]
ep_GetFileVersionInfoExW ENDP

ep_GetFileVersionInfoSizeA PROC
    jmp qword ptr [g_real + 32]
ep_GetFileVersionInfoSizeA ENDP

ep_GetFileVersionInfoSizeExA PROC
    jmp qword ptr [g_real + 40]
ep_GetFileVersionInfoSizeExA ENDP

ep_GetFileVersionInfoSizeExW PROC
    jmp qword ptr [g_real + 48]
ep_GetFileVersionInfoSizeExW ENDP

ep_GetFileVersionInfoSizeW PROC
    jmp qword ptr [g_real + 56]
ep_GetFileVersionInfoSizeW ENDP

ep_GetFileVersionInfoW PROC
    jmp qword ptr [g_real + 64]
ep_GetFileVersionInfoW ENDP

ep_VerFindFileA PROC
    jmp qword ptr [g_real + 72]
ep_VerFindFileA ENDP

ep_VerFindFileW PROC
    jmp qword ptr [g_real + 80]
ep_VerFindFileW ENDP

ep_VerInstallFileA PROC
    jmp qword ptr [g_real + 88]
ep_VerInstallFileA ENDP

ep_VerInstallFileW PROC
    jmp qword ptr [g_real + 96]
ep_VerInstallFileW ENDP

ep_VerQueryValueA PROC
    jmp qword ptr [g_real + 104]
ep_VerQueryValueA ENDP

ep_VerQueryValueW PROC
    jmp qword ptr [g_real + 112]
ep_VerQueryValueW ENDP

END
