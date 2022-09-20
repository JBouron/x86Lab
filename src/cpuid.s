# Implementation of the _cpuid helper function.

.intel_syntax   noprefix
.code64

#extern "C" void _cpuid(u32 const eax, CpuidResult * const dest);
.global _cpuid
_cpuid:
    push    rbp
    mov     rbp, rsp
    push    rbx

    mov     eax, edi
    cpuid
    mov     [rsi + 0x0], eax
    mov     [rsi + 0x4], ebx
    mov     [rsi + 0x8], ecx
    mov     [rsi + 0xC], edx

    pop     rbx
    leave
    ret
