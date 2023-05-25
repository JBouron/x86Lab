; Sample program showing CPU mode initialization typically found in bootloader
; or operating system code.

; >>>>>>>>>> TIP <<<<<<<<<<
; Make sure to select the "Start CPU mode" in the menu bar above to "16-bit"
; before starting executing. Otherwise the machine will most certainly
; triple-fault.
; >>>>>>>>>> TIP <<<<<<<<<<
;
; Basic short-cuts:
;   's' to step one instruction forward
;   'r' to step one instruction backward
;   'q' to quit

BITS 16

; Set the code segment to 0x0.
jmp     0x0:rmTarget

rmTarget:
; We are currently in 16-bit real-address mode. Setup the GDT and enable 32-bit
; protected mode.
xor     ax, ax
mov     ds, ax

cli

; Setup the GDTR to point to the GDT that will be used in this example. After
; this instruction, if you look at the "GDT" tab, you can see a description of
; each entry in the GDT.
lgdt    [gdtDesc]

; Set Protected-Mode (PM) bit in CR0.
mov     eax, cr0
or      eax, 1
mov     cr0, eax

; Jump to 32-bit protected mode.
jmp     0x8:pmTarget

BITS 32
pmTarget:
; We are now in 32-bit Protected Mode. Perform the necessary setup to prepare
; and jump to 64-bit mode.

; Set the other segment registers to the 32-bit data segment.
mov     ax, 0x10
mov     ds, ax
mov     es, ax
mov     fs, ax
mov     gs, ax
mov     ss, ax

; In x86, writing to the SS register inhibits the interrupts for the next
; instruction. As a result the instruction followin the `mov ss` is skipped in
; the step-by-step execution. This is unfortunately a limitation that we cannot
; overcome (unless we resort to emulation).
; Here we put a `nop` so that the "missed" instruction is not very important.
nop

; Setup IDT. This example does not make use of interrupts, and this IDT is
; filled with bogus values. The purpose of this IDT is to showcase the "IDT"
; tab, if look in this tab you should see the details of all IDT entries.
lidt    [pmIdtDesc]

; Now ready to enable 64-bit.

; Prepare the page-tables for 64-bit mode. Now this is a usually complex step,
; however, to keep things simple and short here we are using a neat trick:
; recursive page-table entries. We use a recursive entry for linear address 0x0,
; thus creating an identity map for address 0x0.
; If you don't quite understand what this does, just step until the `mov cr3` a
; few instructions below and look at the "Page Table" tab. This should help (and
; serve as a nice showcase of the "Page Table" tab!).
xor     ebx, ebx
mov     DWORD [ebx], 0x3
mov     DWORD [ebx+4], 0x0

; Enable physical-address-extension (PAE) bit in CR4
mov     eax, cr4
or      eax, (1 << 5)
mov     cr4, eax

; Load CR3 with PML4 address.
mov     cr3, ebx

; Enable LME bit in EFER.
mov     ecx, 0xC0000080
rdmsr
or      eax, (1 << 8)
wrmsr

; Enable paging.
mov     eax, cr0
or      eax, (1 << 31)
mov     cr0, eax

; We are now in a 32-bit segment in 64-bit mode. Jump to a 64-bit segment.
jmp     0x28:entryLongMode

BITS    64
entryLongMode:
; Finally in 64-bit Long-Mode, we can now use full 64-bit registers.
mov     rax, 0xdeadbeefcafebabe

; Setup an IDT. Once again this is only to showcase the "IDT" tab.
lidt    [lmIdtDesc]

dead:
nop
jmp     dead


; Some data structures used in the example. Note that we don't really have
; sections here, instead we simply mix code and data.

; Global Descriptor Table for the first stage, e.g when jumping from real-mode
; to protected mode.
gdt:
; NULL entry.
dq 0x0000000000000000
; Flat 32 bit code segment.
dq 0x00cf9a000000ffff
; Flat 32 bit data segment.
dq 0x00cf92000000ffff
; Flat 16 bit code segment. Limit = 64KiB.
dq 0x000f9a000000ffff
; Flat 16 bit data segment. Limit = 64KiB.
dq 0x000f92000000ffff
; 64 bit code segment.
dq 0x00af9a000000ffff
; 64 bit data segment.
dq 0x00af92000000ffff
gdtEnd:

; The GDT descriptor that will be loaded in the GDTR.
gdtDesc:
dw  (gdtEnd - gdt) - 1
dd   gdt

; An example protected mode IDT with various entry types. The addresses are of
; course bogus and only here as place holders.
pmIdt:
dq ((0xdeadbeef & 0xffff0000) << 32) | \
    (1 << 47) | (0b01110 << 40) | (0x8 << 16) | (0xdeadbeef & 0xffff)
dq ((0xdeadbeef & 0xffff0000) << 32) | \
    (0 << 47) | (0b01110 << 40) | (0x8 << 16) | (0xdeadbeef & 0xffff)
dq ((0xcafebabe & 0xffff0000) << 32) | \
    (1 << 47) | (0b00101 << 40) | (0x8 << 16) | (0xcafebabe & 0xffff)
dq ((0xbeefbabe & 0xffff0000) << 32) | \
    (1 << 47) | (0b00111 << 40) | (0x8 << 16) | (0xbeefbabe & 0xffff)
pmIdtEnd:

; The IDT descriptor that will be loaded in the IDTR.
pmIdtDesc:
dw  (pmIdtEnd - pmIdt) - 1
dd  pmIdt

; A 64-bit mode IDT, to showcase the IDT tab this time in 64-bit mode.
lmIdt:
dd (0x8 << 16) | (0xcafebabe & 0xffff)
dd (0xcafebabe & 0xffff0000) | (1 << 15) | (3 << 13) | (7 << 8) | 4
dd 0xdeadbeef
dd 0x0

dd (0x8 << 16) | (0xcafebabe & 0xffff)
dd (0xcafebabe & 0xffff0000) | (0 << 15) | (3 << 13) | (7 << 8) | 4
dd 0xdeadbeef
dd 0x0
lmIdtEnd:

; The IDT descriptor that will be loaded in the IDTR.
lmIdtDesc:
dw  (lmIdtEnd - lmIdt) - 1
dq  lmIdt
