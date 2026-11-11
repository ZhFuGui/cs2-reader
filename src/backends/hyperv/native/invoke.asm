.code
cs2_hypercall proc frame
    push rbx
    .pushreg rbx
    .endprolog
    ; hyperv-guest-memory: RCX=info，RDX/R8/R9/R10/R11=五个参数。
    mov rdx, [rcx + 8]
    mov r8,  [rcx + 16]
    mov r9,  [rcx + 24]
    mov r10, [rcx + 32]
    mov r11, [rcx + 40]
    mov rcx, [rcx]
    ; 未加载对应后端时使用普通 CPUID 叶 0；握手不会把普通返回值当成成功。
    xor eax, eax
    cpuid
    pop rbx
    ret
cs2_hypercall endp
END
