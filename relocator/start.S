//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#if defined(__aarch64__)

    .text
    .globl _start
    .type _start, @function
    .balign 16
_start:
    hint #34 // bti c

    // Calculate load bias.
    // We assume the binary is linked at 0 (static-pie).
    // The load bias is the address of the start of the ELF.
    // The linker provides __ehdr_start symbol.
    adrp    x0, __ehdr_start
    add     x0, x0, :lo12:__ehdr_start

    // Get address of _DYNAMIC.
    adrp    x1, _DYNAMIC
    add     x1, x1, :lo12:_DYNAMIC

    // Call relocate_loader(bias, dynamic)
    bl      relocate_loader

    // Call runtime_main()
    bl      runtime_main

    // Should not return.
    // But if it does, exit.
    mov     x8, #93 // __NR_exit
    svc     #0

    // Crash if exit fails
    brk     #1

    .size _start, . - _start

    /* Mandatory GNU Property Note for BTI/PAC */
    .section .note.gnu.property, "a", %note
    .balign 8
    .long 4
    .long 16
    .long 5
    .asciz "GNU"
    .long 0xc0000000
    .long 4
    .long 3 /* GNU_PROPERTY_AARCH64_FEATURE_1_BTI | GNU_PROPERTY_AARCH64_FEATURE_1_PAC */
    .long 0

#elif defined(__x86_64__)

    .text
    .globl _start
    .type _start, @function
    .balign 16
_start:
    // Calculate load bias (runtime address of __ehdr_start)
    lea __ehdr_start(%rip), %rdi

    // Calculate dynamic address
    lea _DYNAMIC(%rip), %rsi

    call relocate_loader

    call runtime_main

    // Exit
    mov $60, %rax // __NR_exit
    syscall
    hlt

    .size _start, . - _start

#endif
