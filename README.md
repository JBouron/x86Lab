# x86Lab

A playground for x86 assembly programming.

## What is it?
x86Lab is an application that allows you to execute an x86 assembly snippet
step-by-step while showing in real time the effect each instruction has on the
state of the CPU and memory.

The main goal is to make it easy to explore and experiment with x86 assembly
programming. This is a tool that can be useful as part of learning x86
programming, reverse-engineering, or simply to quickly check the behaviour of a
particular instruction.

![Alt text](/screenshots/screenshot1.png?raw=true "x86Lab")

> Disclaimer: This project is still in a Work-In-Progress state. While most of
> the basic functionalities are implemented, the UI is still rough around the
> edges, the implementation is a bit of a mess and there are still a few bugs to
> squash. Despite this, x86Lab still managed to prove itself useful to me, hence
> I am planning on continuing its development.

## Why?
Imagine you have a small snippet of x86 assembly, maybe this is some code that
you found while reverse-engineering, or maybe this is some code you came up with
yourself and that you want to check for correctness. You are interested in
understanding how this code works and how each instruction it contains affects
the state of the CPU registers and memory.

x86Lab allows you to do just that in an easy and fast manner.

Prior to x86Lab the solution typically involved compiling the snippet with NASM
(or other assembler) and running it under `gdb` in order to be able to execute
the code step-by-step. You would then be able to analyze the state of the CPU
registers and memory using `gdb`'s interactive command line. This setup,
however, has a lot of downsides:
- You need to write the snippet in such a way that it can be assembled into an
  ELF executable, i.e. you need to define a global `_start` label, a `.text`
  section, ...
- For this particular use case, `gdb`'s interface is a bit clunky. You have to
  manually input the `x` and `info registers` commands in order to get the state
  of the CPU/memory after every step.
- `gdb` does not provide backward step-by-step execution, at least not by
  default.
- Since the assembly code is running in a user-space process, the code cannot
  execute any privileged instructions. It also cannot execute 16-bit/real-mode
  code.

The motivation behind x86Lab was to make this process as painless as possible:
you input the assembly snippet and you can immediately start executing it
step-by-step forward and backward, all while seeing in real time the effect each
instruction has on the CPU and memory. On top of this, x86Lab also supports
running 16, 32 and 64 bit code at any privilege level.

## How does it work?
x86Lab takes a path to an assembly snippet as argument, this snippet must be
compatible with the [Netwide Assembler (NASM)](https://www.nasm.us/) however it
does not need to define any section or even a `_start` label, the file can
simply contain a listing of instructions. x86Lab assembles the assembly snippet
using NASM and loads the resulting machine code into a small KVM virtual
machine. The user-interface then allows you to execute the assembled code
step-by-step forward and backward all while showing the current state of the CPU
and memory.

Because the code is executed within a KVM virtual machine, it can execute
privileged instructions and switch to real-mode, 32-bit mode or 64-bit mode.

Assembly snippets run within x86Lab are expected to be typically small,
therefore the KVM virtual machine instantiated by x86Lab is very light: a single
virtual core with a few kilobytes of memory.

## Features
The main features currently implemented are:
- Forward and backward step-by-step execution.
- Running the VM in 16, 32 and 64 bit mode.
- Display the state of the current stack.
- Display the content of the memory (physical and virtual addressing mode) with
  multiple formatting options (bytes, words, dwords, qwords, floats and
  doubles).
- Display the state of the CPU general purpose registers, MMX/SSE/AVX registers
  (including AVX-512/zmm), control registers, segment registers.
- Display the state of the GDT and IDT pointed by the GDTR and IDTR
  respectively.
- Display the state of the page-table structure currently loaded into CR3.

A few features that I plan on eventually adding (non-exhaustive list):
- Add a text editor to input the snippet instead of having to load a file from
  disk. This would make the experience much nicer.
- Display the state of the APIC registers.
- Display the state of the x87 floating point registers (this would be a
  formatting option in the MMX tab).
- A way to modify a register's value or a memory location through the UI.
- Display the state of the CPU's Task Register.

Before introducing new features, the code needs a good clean-up and the UI some
refinements.

## Building

The program has a few dependencies:
- SDL2 for the UI, install `libsdl2-dev` on Ubuntu or `sdl2` on Arch Linux.
- Capstone for the disassembler, install `libcapstone-dev` on Ubuntu or
  `capstone` on Arch Linux.

Compiling is as simple as running `make`. It is recommended that you also run
the tests using `make test`.

## Usage
The program takes a single argument, the path to the file that contains the
assembly code to be assembled and analyzed.

The `example/` directory contains an assembly snippet that starts in real-mode
and jumps into protected mode and then 64-bit mode. You can execute it as
follows:
```
./x86lab examples/jumpToProtectedAndLongModes.asm
```
