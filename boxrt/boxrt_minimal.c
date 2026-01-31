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

#include <stdlib.h>
#include <unistd.h>

#define LFI_SYS_pause 1024

__attribute__((noreturn)) static void *
lfi_pause(void *arg)
{
    (void) arg;
    syscall(LFI_SYS_pause);
    while (1) {
    }
}

void *
_lfi_malloc(size_t size)
{
    return malloc(size);
}

void *
_lfi_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void *
_lfi_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void
_lfi_free(void *ptr)
{
    free(ptr);
}

void
_lfi_ret(void)
{
    // We avoid using actual assembly instructions to prevent them from being
    // rewritten.
    asm volatile (
#if defined(__aarch64__)
        ".byte 0x7e, 0x03, 0x5e, 0xf8\n" // ldur x30, [x27, #-32]
        ".byte 0xc0, 0x03, 0x3f, 0xd6\n" // blr x30
#elif defined(__x86_64__)
        ".byte 0x4c, 0x8d, 0x1d, 0x04, 0x00, 0x00, 0x00\n" // lea 0x4(%rip), %r11
        ".byte 0x41, 0xff, 0x66, 0xe0\n"                   // jmp *-0x20(%r14)
#elif defined(__riscv) && (__riscv_xlen == 64)
        ".byte 0x83, 0xb0, 0x0a, 0xfe\n" // ld ra, -32(x21)
        ".byte 0xe7, 0x80, 0x00, 0x00\n" // jalr ra
#else
#error "invalid architecture"
#endif
    );
}

void *symbols[] = {
    &_lfi_malloc,
    &_lfi_realloc,
    &_lfi_calloc,
    &_lfi_free,
    &_lfi_ret,
};

void
_start()
{
    lfi_pause(NULL);
}
