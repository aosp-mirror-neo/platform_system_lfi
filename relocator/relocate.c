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

#include <elf.h>
#include <stdint.h>
#include <stddef.h>

#ifndef DT_RELR
#define DT_RELR 36
#endif

#ifndef DT_RELRSZ
#define DT_RELRSZ 35
#endif

#if defined(__aarch64__)
#define R_RELATIVE R_AARCH64_RELATIVE
#elif defined(__x86_64__)
#define R_RELATIVE R_X86_64_RELATIVE
#else
#error "Unsupported architecture"
#endif

void relocate_loader(uintptr_t bias, Elf64_Dyn* dynamic) {
    if (bias == 0) return;

    Elf64_Rel* rel = NULL;
    size_t rel_sz = 0;
    Elf64_Rela* rela = NULL;
    size_t rela_sz = 0;
    Elf64_Relr* relr = NULL;
    size_t relr_sz = 0;
    size_t rel_ent = 0;
    size_t rela_ent = 0;

    for (Elf64_Dyn* d = dynamic; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_REL:      rel = (Elf64_Rel*)(bias + d->d_un.d_ptr); break;
            case DT_RELSZ:    rel_sz = d->d_un.d_val; break;
            case DT_RELENT:   rel_ent = d->d_un.d_val; break;
            case DT_RELA:     rela = (Elf64_Rela*)(bias + d->d_un.d_ptr); break;
            case DT_RELASZ:   rela_sz = d->d_un.d_val; break;
            case DT_RELAENT:  rela_ent = d->d_un.d_val; break;
            case DT_RELR:
            case DT_ANDROID_RELR:
                relr = (Elf64_Relr*)(bias + d->d_un.d_ptr);
                break;
            case DT_RELRSZ:
            case DT_ANDROID_RELRSZ:
                relr_sz = d->d_un.d_val;
                break;
            default:
                // This include RELRENT because it is fixed-size
                break;
        }
    }

    // Process RELA
    if (rela && rela_sz > 0 && rela_ent > 0) {
        size_t count = rela_sz / rela_ent;
        for (size_t i = 0; i < count; ++i) {
            Elf64_Rela* entry = &rela[i];
            if (ELF64_R_TYPE(entry->r_info) == R_RELATIVE) {
                uintptr_t* target = (uintptr_t*)(bias + entry->r_offset);
                *target = bias + entry->r_addend;
            }
        }
    }

    // Process REL
    if (rel && rel_sz > 0 && rel_ent > 0) {
        size_t count = rel_sz / rel_ent;
        for (size_t i = 0; i < count; ++i) {
            Elf64_Rel* entry = &rel[i];
            if (ELF64_R_TYPE(entry->r_info) == R_RELATIVE) {
                uintptr_t* target = (uintptr_t*)(bias + entry->r_offset);
                *target += bias;
            }
        }
    }

    // Process RELR
    if (relr && relr_sz > 0) {
        size_t count = relr_sz / sizeof(Elf64_Relr);
        uintptr_t* where = 0;
        for (size_t i = 0; i < count; ++i) {
            Elf64_Relr entry = relr[i];
            if ((entry & 1) == 0) {
                where = (uintptr_t*)(bias + entry);
                *where++ += bias;
            } else {
                for (size_t bit = 0; bit < 63; ++bit) {
                    if ((entry >> 1) & (1ULL << bit)) {
                        where[bit] += bias;
                    }
                }
                where += 63;
            }
        }
    }
}
