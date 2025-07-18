// Copyright 2018 Sen Han <00hnes@gmail.com>
// Modifications copyright 2025 Bruce Hill <bruce@bruce-hill.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define _GNU_SOURCE

#include "aco.h"
#include <stdio.h>
#include <stdint.h>

#ifndef public
#define public __attribute__ ((visibility ("default")))
#endif

#define aco_size_t_safe_add_assert(a,b) aco_assert((a)+(b) >= (a))

static void aco_default_protector_last_word(void*);

void* (*aco_alloc_fn)(size_t) = malloc;
void (*aco_dealloc_fn)(void*) = free;

#define aco_alloc(size)  ({ \
    void *_ptr = aco_alloc_fn(size); \
    if (aco_unlikely((_ptr) == NULL)) { \
        fprintf(stderr, "Aborting: failed to allocate memory: %s:%d:%s\n", \
            __FILE__, __LINE__, __PRETTY_FUNCTION__); \
        abort(); \
    } \
    _ptr; \
})

// aco's Global Thread Local Storage variable `co`
public __thread aco_t* aco_gtls_co;
static __thread aco_cofuncp_t aco_gtls_last_word_fp = aco_default_protector_last_word;

#ifdef __i386__
    static __thread void* aco_gtls_fpucw_mxcsr[2];
#elif  __x86_64__
    static __thread void* aco_gtls_fpucw_mxcsr[1];
#else
    #error "platform not supporteded yet"
#endif

public void aco_runtime_test(void) {
#ifdef __i386__
    _Static_assert(sizeof(void*) == 4, "require 'sizeof(void*) == 4'");
#elif  __x86_64__
    _Static_assert(sizeof(void*) == 8, "require 'sizeof(void*) == 8'");
    _Static_assert(sizeof(__uint128_t) == 16, "require 'sizeof(__uint128_t) == 16'");
#else
    #error "platform not supporteded yet"
#endif
    _Static_assert(sizeof(int) >= 4, "require 'sizeof(int) >= 4'");
    aco_assert(sizeof(int) >= 4);
    _Static_assert(sizeof(int) <= sizeof(size_t),
        "require 'sizeof(int) <= sizeof(size_t)'");
    aco_assert(sizeof(int) <= sizeof(size_t));
}

#ifdef __x86_64__
static inline void aco_fast_memcpy(void *dst, const void *src, size_t sz) {
    if (((uintptr_t)src & 0x0f) != 0
        || ((uintptr_t)dst & 0x0f) != 0
        || (sz & 0x0f) != 0x08
        || (sz >> 4) > 8) {
        memcpy(dst, src, sz);
        return;
    }

    __uint128_t xmm0,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7;
    switch (sz >> 4) {
        case 0: 
            break; 
        case 1: 
            xmm0 = *((__uint128_t*)src + 0); 
            *((__uint128_t*)dst + 0) = xmm0;
            break; 
        case 2: 
            xmm0 = *((__uint128_t*)src + 0); 
            xmm1 = *((__uint128_t*)src + 1); 
            *((__uint128_t*)dst + 0) = xmm0;
            *((__uint128_t*)dst + 1) = xmm1;
            break; 
        case 3: 
            xmm0 = *((__uint128_t*)src + 0); 
            xmm1 = *((__uint128_t*)src + 1); 
            xmm2 = *((__uint128_t*)src + 2); 
            *((__uint128_t*)dst + 0) = xmm0;
            *((__uint128_t*)dst + 1) = xmm1;
            *((__uint128_t*)dst + 2) = xmm2;
            break; 
        case 4: 
            xmm0 = *((__uint128_t*)src + 0); 
            xmm1 = *((__uint128_t*)src + 1); 
            xmm2 = *((__uint128_t*)src + 2); 
            xmm3 = *((__uint128_t*)src + 3); 
            *((__uint128_t*)dst + 0) = xmm0;
            *((__uint128_t*)dst + 1) = xmm1;
            *((__uint128_t*)dst + 2) = xmm2;
            *((__uint128_t*)dst + 3) = xmm3;
            break; 
        case 5: 
            xmm0 = *((__uint128_t*)src + 0); 
            xmm1 = *((__uint128_t*)src + 1); 
            xmm2 = *((__uint128_t*)src + 2); 
            xmm3 = *((__uint128_t*)src + 3); 
            xmm4 = *((__uint128_t*)src + 4); 
            *((__uint128_t*)dst + 0) = xmm0;
            *((__uint128_t*)dst + 1) = xmm1;
            *((__uint128_t*)dst + 2) = xmm2;
            *((__uint128_t*)dst + 3) = xmm3;
            *((__uint128_t*)dst + 4) = xmm4;
            break; 
        case 6: 
            xmm0 = *((__uint128_t*)src + 0); 
            xmm1 = *((__uint128_t*)src + 1); 
            xmm2 = *((__uint128_t*)src + 2); 
            xmm3 = *((__uint128_t*)src + 3); 
            xmm4 = *((__uint128_t*)src + 4); 
            xmm5 = *((__uint128_t*)src + 5); 
            *((__uint128_t*)dst + 0) = xmm0;
            *((__uint128_t*)dst + 1) = xmm1;
            *((__uint128_t*)dst + 2) = xmm2;
            *((__uint128_t*)dst + 3) = xmm3;
            *((__uint128_t*)dst + 4) = xmm4;
            *((__uint128_t*)dst + 5) = xmm5;
            break; 
        case 7: 
            xmm0 = *((__uint128_t*)src + 0); 
            xmm1 = *((__uint128_t*)src + 1); 
            xmm2 = *((__uint128_t*)src + 2); 
            xmm3 = *((__uint128_t*)src + 3); 
            xmm4 = *((__uint128_t*)src + 4); 
            xmm5 = *((__uint128_t*)src + 5); 
            xmm6 = *((__uint128_t*)src + 6); 
            *((__uint128_t*)dst + 0) = xmm0;
            *((__uint128_t*)dst + 1) = xmm1;
            *((__uint128_t*)dst + 2) = xmm2;
            *((__uint128_t*)dst + 3) = xmm3;
            *((__uint128_t*)dst + 4) = xmm4;
            *((__uint128_t*)dst + 5) = xmm5;
            *((__uint128_t*)dst + 6) = xmm6;
            break; 
        case 8: 
            xmm0 = *((__uint128_t*)src + 0); 
            xmm1 = *((__uint128_t*)src + 1); 
            xmm2 = *((__uint128_t*)src + 2); 
            xmm3 = *((__uint128_t*)src + 3); 
            xmm4 = *((__uint128_t*)src + 4); 
            xmm5 = *((__uint128_t*)src + 5); 
            xmm6 = *((__uint128_t*)src + 6); 
            xmm7 = *((__uint128_t*)src + 7); 
            *((__uint128_t*)dst + 0) = xmm0;
            *((__uint128_t*)dst + 1) = xmm1;
            *((__uint128_t*)dst + 2) = xmm2;
            *((__uint128_t*)dst + 3) = xmm3;
            *((__uint128_t*)dst + 4) = xmm4;
            *((__uint128_t*)dst + 5) = xmm5;
            *((__uint128_t*)dst + 6) = xmm6;
            *((__uint128_t*)dst + 7) = xmm7;
            break; 
    }
    *((uint64_t*)((uintptr_t)dst + sz - 8)) = *((uint64_t*)((uintptr_t)src + sz - 8));
}
#endif

void aco_default_protector_last_word(void*) {
    aco_t* co = aco_get_co();
    // do some log about the offending `co`
    fprintf(stderr,"error: aco_default_protector_last_word triggered\n");
    fprintf(stderr, "error: co:%p should call `aco_exit()` instead of direct "
        "`return` in co_fp:%p to finish its execution\n", co, (void*)co->fp);
    aco_assert(0);
}

public void aco_set_allocator(void* (*alloc)(size_t), void (*dealloc)(void*))
{
    aco_alloc_fn = alloc;
    aco_dealloc_fn = dealloc;
}

public void aco_thread_init(aco_cofuncp_t last_word_co_fp) {
    aco_save_fpucw_mxcsr(aco_gtls_fpucw_mxcsr);

    if ((void*)last_word_co_fp != NULL)
        aco_gtls_last_word_fp = last_word_co_fp;
}

// This function `aco_funcp_protector` should never be
// called. If it's been called, that means the offending
// `co` didn't call aco_exit(co) instead of `return` to
// finish its execution.
public void aco_funcp_protector(void) {
    if ((void*)(aco_gtls_last_word_fp) != NULL) {
        aco_gtls_last_word_fp(NULL);
    } else {
        aco_default_protector_last_word(NULL);
    }
    aco_assert(0);
}

public aco_shared_stack_t* aco_shared_stack_new(size_t sz) {
    return aco_shared_stack_new2(sz, 1);
}

public aco_shared_stack_t* aco_shared_stack_new2(size_t sz, bool guard_page_enabled) {
    if (sz == 0) {
        sz = 1024 * 1024 * 2;
    }
    if (sz < 4096) {
        sz = 4096;
    }
    aco_assert(sz > 0);

    size_t u_pgsz = 0;
    if (guard_page_enabled) {
        // although gcc's Built-in Functions to Perform Arithmetic with
        // Overflow Checking is better, but it would require gcc >= 5.0
        long pgsz = sysconf(_SC_PAGESIZE);
        // pgsz must be > 0 && a power of two
        aco_assert(pgsz > 0 && (((pgsz - 1) & pgsz) == 0));
        u_pgsz = (size_t)((unsigned long)pgsz);
        // it should be always true in real life
        aco_assert(u_pgsz == (unsigned long)pgsz && ((u_pgsz << 1) >> 1) == u_pgsz);
        if (sz <= u_pgsz) {
            sz = u_pgsz << 1;
        } else {
            size_t new_sz;
            if ((sz & (u_pgsz - 1)) != 0) {
                new_sz = (sz & (~(u_pgsz - 1)));
                aco_assert(new_sz >= u_pgsz);
                aco_size_t_safe_add_assert(new_sz, (u_pgsz << 1));
                new_sz = new_sz + (u_pgsz << 1);
                aco_assert(sz / u_pgsz + 2 == new_sz / u_pgsz);
            } else {
                aco_size_t_safe_add_assert(sz, u_pgsz);
                new_sz = sz + u_pgsz;
                aco_assert(sz / u_pgsz + 1 == new_sz / u_pgsz);
            }
            sz = new_sz;
            aco_assert((sz / u_pgsz > 1) && ((sz & (u_pgsz - 1)) == 0));
        }
    }

    aco_shared_stack_t* p = aco_alloc(sizeof(aco_shared_stack_t));
    memset(p, 0, sizeof(aco_shared_stack_t));

    if (guard_page_enabled) {
        p->real_ptr = mmap(
            NULL, sz, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0
        );
        if (aco_unlikely(p->real_ptr == MAP_FAILED)) {
            fprintf(stderr, "Aborting: failed to allocate memory: %s:%d:%s\n",
                    __FILE__, __LINE__, __PRETTY_FUNCTION__);
            abort();
        }
        p->guard_page_enabled = true;
        aco_assert(0 == mprotect(p->real_ptr, u_pgsz, PROT_READ));

        p->ptr = (void*)(((uintptr_t)p->real_ptr) + u_pgsz);
        p->real_sz = sz;
        aco_assert(sz >= (u_pgsz << 1));
        p->sz = sz - u_pgsz;
    } else {
        //p->guard_page_enabled = 0;
        p->sz = sz;
        p->ptr = aco_alloc(sz);
    }

    p->owner = NULL;
#ifdef ACO_USE_VALGRIND
    p->valgrind_stk_id = VALGRIND_STACK_REGISTER(
        p->ptr, (void*)((uintptr_t)p->ptr + p->sz)
    );
#endif
#if defined(__i386__) || defined(__x86_64__)
    uintptr_t u_p = (uintptr_t)(p->sz - (sizeof(void*) << 1) + (uintptr_t)p->ptr);
    u_p = (u_p >> 4) << 4;
    p->align_highptr = (void*)u_p;
    p->align_retptr  = (void*)(u_p - sizeof(void*));
    *((void**)(p->align_retptr)) = (void*)(aco_funcp_protector_asm);
    aco_assert(p->sz > (16 + (sizeof(void*) << 1) + sizeof(void*)));
    p->align_limit = p->sz - 16 - (sizeof(void*) << 1);
#else
    #error "platform not supporteded yet"
#endif
    return p;
}

public void aco_shared_stack_destroy(aco_shared_stack_t* sstk) {
    aco_assert(sstk != NULL && sstk->ptr != NULL);
#ifdef ACO_USE_VALGRIND
    VALGRIND_STACK_DEREGISTER(sstk->valgrind_stk_id);
#endif
    if (sstk->guard_page_enabled) {
        aco_assert(0 == munmap(sstk->real_ptr, sstk->real_sz));
        sstk->real_ptr = NULL;
        sstk->ptr = NULL;
    } else {
        if (aco_dealloc_fn != NULL) aco_dealloc_fn(sstk->ptr);
        sstk->ptr = NULL;
    }
    if (aco_dealloc_fn != NULL) aco_dealloc_fn(sstk);
}

public aco_t* aco_create(
    aco_t* main_co, aco_shared_stack_t* shared_stack,
    size_t saved_stack_sz, aco_cofuncp_t fp, void* arg
) {
    aco_t* p = aco_alloc(sizeof(aco_t));
    memset(p, 0, sizeof(aco_t));

    if (main_co != NULL) { // non-main co
        aco_assertptr(shared_stack);
        p->shared_stack = shared_stack;
#ifdef __i386__
        // POSIX.1-2008 (IEEE Std 1003.1-2008) - General Information - Data Types - Pointer Types
        // http://pubs.opengroup.org/onlinepubs/9699919799.2008edition/functions/V2_chap02.html#tag_15_12_03
        p->reg[ACO_REG_IDX_RETADDR] = (void*)fp;
        // push retaddr
        p->reg[ACO_REG_IDX_SP] = p->shared_stack->align_retptr;
        #ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
            p->reg[ACO_REG_IDX_FPU] = aco_gtls_fpucw_mxcsr[0];
            p->reg[ACO_REG_IDX_FPU + 1] = aco_gtls_fpucw_mxcsr[1];
        #endif
#elif  __x86_64__
        p->reg[ACO_REG_IDX_RETADDR] = (void*)fp;
        p->reg[ACO_REG_IDX_SP] = p->shared_stack->align_retptr;
        #ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
            p->reg[ACO_REG_IDX_FPU] = aco_gtls_fpucw_mxcsr[0];
        #endif
#else
        #error "platform not supporteded yet"
#endif
        p->main_co = main_co;
        p->arg = arg;
        p->fp = fp;
        if (saved_stack_sz == 0) {
            saved_stack_sz = 64;
        }
        p->saved_stack.ptr = aco_alloc(saved_stack_sz);
        p->saved_stack.sz = saved_stack_sz;
#if defined(__i386__) || defined(__x86_64__)
        p->saved_stack.valid_sz = 0;
#else
        #error "platform not supporteded yet"
#endif
        return p;
    } else { // main co
        p->main_co = NULL;
        p->arg = arg;
        p->fp = fp;
        p->shared_stack = NULL;
        p->saved_stack.ptr = NULL;
        return p;
    }
    aco_assert(0);
}

public aco_attr_no_asan
void aco_resume(aco_t* resume_co) {
    aco_assert(resume_co != NULL && resume_co->main_co != NULL
        && !resume_co->is_finished
    );
    if (resume_co->shared_stack->owner != resume_co) {
        if (resume_co->shared_stack->owner != NULL) {
            aco_t* owner_co = resume_co->shared_stack->owner;
            aco_assert(owner_co->shared_stack == resume_co->shared_stack);
#if defined(__i386__) || defined(__x86_64__)
            aco_assert(
                (
                    (uintptr_t)(owner_co->shared_stack->align_retptr)
                    >=
                    (uintptr_t)(owner_co->reg[ACO_REG_IDX_SP])
                )
                &&
                (
                    (uintptr_t)(owner_co->shared_stack->align_highptr)
                    -
                    (uintptr_t)(owner_co->shared_stack->align_limit)
                    <=
                    (uintptr_t)(owner_co->reg[ACO_REG_IDX_SP])
                )
            );
            owner_co->saved_stack.valid_sz =
                (uintptr_t)(owner_co->shared_stack->align_retptr)
                -
                (uintptr_t)(owner_co->reg[ACO_REG_IDX_SP]);
            if (owner_co->saved_stack.sz < owner_co->saved_stack.valid_sz) {
                if (aco_dealloc_fn != NULL) aco_dealloc_fn(owner_co->saved_stack.ptr);
                owner_co->saved_stack.ptr = NULL;
                while (1) {
                    owner_co->saved_stack.sz = owner_co->saved_stack.sz << 1;
                    aco_assert(owner_co->saved_stack.sz > 0);
                    if (owner_co->saved_stack.sz >= owner_co->saved_stack.valid_sz) {
                        break;
                    }
                }
                owner_co->saved_stack.ptr = aco_alloc(owner_co->saved_stack.sz);
            }
            // TODO: optimize the performance penalty of memcpy function call
            //   for very short memory span
            if (owner_co->saved_stack.valid_sz > 0) {
    #ifdef __x86_64__
                aco_fast_memcpy(
                    owner_co->saved_stack.ptr,
                    owner_co->reg[ACO_REG_IDX_SP],
                    owner_co->saved_stack.valid_sz
                );
    #else
                memcpy(
                    owner_co->saved_stack.ptr,
                    owner_co->reg[ACO_REG_IDX_SP],
                    owner_co->saved_stack.valid_sz
                );
    #endif
                owner_co->saved_stack.ct_save++;
            }
            if (owner_co->saved_stack.valid_sz > owner_co->saved_stack.max_cpsz) {
                owner_co->saved_stack.max_cpsz = owner_co->saved_stack.valid_sz;
            }
            owner_co->shared_stack->owner = NULL;
            owner_co->shared_stack->align_validsz = 0;
#else
            #error "platform not supporteded yet"
#endif
        }
        aco_assert(resume_co->shared_stack->owner == NULL);
#if defined(__i386__) || defined(__x86_64__)
        aco_assert(
            resume_co->saved_stack.valid_sz
            <=
            resume_co->shared_stack->align_limit - sizeof(void*)
        );
        // TODO: optimize the performance penalty of memcpy function call
        //   for very short memory span
        if (resume_co->saved_stack.valid_sz > 0) {
            void *dst = (void*)(
                (uintptr_t)(resume_co->shared_stack->align_retptr)
                - resume_co->saved_stack.valid_sz);
    #ifdef __x86_64__
            aco_fast_memcpy(dst, resume_co->saved_stack.ptr, resume_co->saved_stack.valid_sz);
    #else
            memcpy(dst, resume_co->saved_stack.ptr, resume_co->saved_stack.valid_sz);
    #endif
            resume_co->saved_stack.ct_restore++;
        }
        if (resume_co->saved_stack.valid_sz > resume_co->saved_stack.max_cpsz) {
            resume_co->saved_stack.max_cpsz = resume_co->saved_stack.valid_sz;
        }
        resume_co->shared_stack->align_validsz = resume_co->saved_stack.valid_sz + sizeof(void*);
        resume_co->shared_stack->owner = resume_co;
#else
        #error "platform not supporteded yet"
#endif
    }
    aco_gtls_co = resume_co;
    aco_yield_asm(resume_co->main_co, resume_co);
    aco_gtls_co = resume_co->main_co;
}

public void aco_destroy(aco_t* co) {
    aco_assertptr(co);
    if (aco_is_main_co(co)) {
        if (aco_dealloc_fn != NULL) aco_dealloc_fn(co);
    } else {
        if (co->shared_stack->owner == co) {
            co->shared_stack->owner = NULL;
            co->shared_stack->align_validsz = 0;
        }
        if (aco_dealloc_fn != NULL)
            aco_dealloc_fn(co->saved_stack.ptr);
        co->saved_stack.ptr = NULL;
        if (aco_dealloc_fn != NULL)
            aco_dealloc_fn(co);
    }
}

public void aco_exit_fn(void*) {
    aco_exit();
}
