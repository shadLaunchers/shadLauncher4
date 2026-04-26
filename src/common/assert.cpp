// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// History:
//   2026-01-02  Copied from shadPS4 Emulator Project (v0.13.0)

#include "common/arch.h"
#include "common/assert.h"
#include "common/logging/log.h"

#if defined(ARCH_X86_64)
#define Crash() __asm__ __volatile__("int $3")
#elif defined(ARCH_ARM64)
#define Crash() __asm__ __volatile__("brk 0")
#else
#error "Missing Crash() implementation for target CPU architecture."
#endif

void assert_fail_impl() {
    Common::Log::Shutdown();
    Crash();
}

[[noreturn]] void unreachable_impl() {
    Common::Log::Shutdown();
    Crash();
    throw std::runtime_error("Unreachable code");
}

void assert_fail_debug_msg(const char* msg) {
    LOG_CRITICAL(Debug, "Assertion Failed!\n{}", msg);
    assert_fail_impl();
}
