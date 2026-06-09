// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Simple std-based replacements for original custom types
using usz = std::size_t;
using u64 = std::uint64_t;
using u32 = std::uint32_t;
constexpr usz umax = static_cast<usz>(-1);

template <typename T, usz N = std::max<usz>(256 / sizeof(T), 1)>
class lf_array {
    // Inline storage for the first block.
    T m_data[N]{};

    // Pointer to the next block. nullptr until growth installs one.
    std::atomic<lf_array*> m_next{nullptr};

public:
    constexpr lf_array() = default;

    ~lf_array() {
        // Iterative deletion to avoid stack overflow on very long chains.
        // Each block's destructor would otherwise recurse via its own m_next.
        for (auto* ptr = m_next.load(std::memory_order_acquire); ptr;) {
            auto* next = ptr->m_next.load(std::memory_order_acquire);
            ptr->m_next.store(nullptr, std::memory_order_relaxed);
            delete ptr;
            ptr = next;
        }
    }

    T& operator[](usz index) {
        lf_array* cur = this;

        for (usz base = 0;; base += N) {
            if (index - base < N)
                return cur->m_data[index - base];

            lf_array* next = cur->m_next.load(std::memory_order_acquire);

            if (!next) {
                if (!(index - base < N * 2))
                    throw std::out_of_range("lf_array: index too far beyond current growth");

                lf_array* fresh = new lf_array();
                lf_array* expected = nullptr;

                if (cur->m_next.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
                    next = fresh;
                } else {
                    // Lost the race; another thread installed first.
                    delete fresh;
                    next = expected;
                }
            }

            cur = next;
        }
    }

    // Apply func to each element across all currently-allocated blocks.
    template <typename F>
        requires(std::is_invocable_v<F, T&>)
    auto for_each(F&& func, bool is_finite = true) {
        lf_array* cur = this;
        using return_t = std::invoke_result_t<F, T&>;

        while (cur) {
            for (usz j = 0; j < N; ++j) {
                if constexpr (std::is_void_v<return_t>) {
                    std::invoke(func, cur->m_data[j]);
                } else {
                    auto ret = std::invoke(func, cur->m_data[j]);
                    if (ret)
                        return std::make_pair(std::addressof(cur->m_data[j]), std::move(ret));
                }
            }

            lf_array* next = cur->m_next.load(std::memory_order_acquire);

            if constexpr (!std::is_void_v<return_t>) {
                if (!next && !is_finite) {
                    lf_array* fresh = new lf_array();
                    lf_array* expected = nullptr;

                    if (cur->m_next.compare_exchange_strong(expected, fresh,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire)) {
                        next = fresh;
                    } else {
                        delete fresh;
                        next = expected;
                    }
                }
            }

            cur = next;
        }

        if constexpr (!std::is_void_v<return_t>)
            return std::make_pair(static_cast<T*>(nullptr), return_t());
    }

    // Returns N * (number of allocated blocks). Note this is the
    // capacity, not the number of elements actually written to — the
    // array has no notion of "used" vs "default-constructed".
    u64 size() const {
        u64 total = 0;
        for (auto const* ptr = this; ptr; ptr = ptr->m_next.load(std::memory_order_acquire))
            total += N;
        return total;
    }
};

// =============================================================================
// lf_fifo<T, N>
// =============================================================================
template <typename T, usz N = std::max<usz>(256 / sizeof(T), 1)>
class lf_fifo : public lf_array<T, N> {
    std::atomic<u64> m_ctrl{0};

public:
    constexpr lf_fifo() = default;

    // Currently-occupied slot count.
    u32 size() const {
        const u64 ctrl = m_ctrl.load(std::memory_order_acquire);
        return static_cast<u32>(ctrl - (ctrl >> 32));
    }

    // Reserve `count` slot(s); returns the first reserved slot index.
    u32 push_begin(u32 count = 1) {
        return static_cast<u32>(
            m_ctrl.fetch_add(static_cast<u64>(count), std::memory_order_acq_rel));
    }

    // Current read cursor.
    u32 peek() const {
        return static_cast<u32>(m_ctrl.load(std::memory_order_acquire) >> 32);
    }

    // Mark `count` slot(s) as consumed. If push == pop after this,
    // the control word is reset to zero (zero is returned in that case).
    u32 pop_end(u32 count = 1) {
        u64 old = m_ctrl.load(std::memory_order_acquire);
        for (;;) {
            u64 next = old + (static_cast<u64>(count) << 32);
            // Reset when push == pop, freeing the 32-bit counter range.
            if ((next >> 32) == static_cast<u32>(next))
                next = 0;

            if (m_ctrl.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
                return static_cast<u32>(next >> 32);
            }
            // `old` was refreshed by compare_exchange_weak; retry.
        }
    }
};

// =============================================================================
// Forward declarations for lf_queue support types
// =============================================================================
template <typename T>
class lf_queue_iterator;
template <typename T>
class lf_queue_slice;
template <typename T>
class lf_queue;
template <typename T>
class lf_bunch;

// =============================================================================
// lf_queue_item<T> — internal linked-list node
// =============================================================================
template <typename T>
class lf_queue_item final {
    lf_queue_item* m_link = nullptr;
    T m_data;

    template <typename U>
    friend class lf_queue_iterator;
    template <typename U>
    friend class lf_queue_slice;
    template <typename U>
    friend class lf_queue;
    template <typename U>
    friend class lf_bunch;

    constexpr lf_queue_item() = default;

    template <typename... Args>
    constexpr lf_queue_item(lf_queue_item* link, Args&&... args)
        : m_link(link), m_data(std::forward<Args>(args)...) {}

public:
    lf_queue_item(const lf_queue_item&) = delete;
    lf_queue_item& operator=(const lf_queue_item&) = delete;

    ~lf_queue_item() {
        // Iterative cleanup. A single recursive ~lf_queue_item would
        // recurse N times for an N-element chain and could overflow.
        for (auto* ptr = m_link; ptr;) {
            auto* next = ptr->m_link;
            ptr->m_link = nullptr;
            delete ptr;
            ptr = next;
        }
    }
};

// =============================================================================
// lf_queue_iterator<T> — non-owning forward iterator
// =============================================================================
template <typename T>
class lf_queue_iterator {
    lf_queue_item<T>* m_ptr = nullptr;

    template <typename U>
    friend class lf_queue_slice;
    template <typename U>
    friend class lf_bunch;

public:
    constexpr lf_queue_iterator() = default;

    bool operator==(const lf_queue_iterator& rhs) const {
        return m_ptr == rhs.m_ptr;
    }
    bool operator!=(const lf_queue_iterator& rhs) const {
        return !(*this == rhs);
    }

    T& operator*() const {
        return m_ptr->m_data;
    }
    T* operator->() const {
        return std::addressof(m_ptr->m_data);
    }

    lf_queue_iterator& operator++() {
        m_ptr = m_ptr->m_link;
        return *this;
    }

    lf_queue_iterator operator++(int) {
        lf_queue_iterator tmp = *this;
        ++(*this);
        return tmp;
    }
};

// =============================================================================
// lf_queue_slice<T> — owning view of a drained queue
// =============================================================================
template <typename T>
class lf_queue_slice {
    lf_queue_item<T>* m_head = nullptr;

    template <typename U>
    friend class lf_queue;

public:
    constexpr lf_queue_slice() = default;

    lf_queue_slice(const lf_queue_slice&) = delete;
    lf_queue_slice& operator=(const lf_queue_slice&) = delete;

    lf_queue_slice(lf_queue_slice&& r) noexcept : m_head(r.m_head) {
        r.m_head = nullptr;
    }

    lf_queue_slice& operator=(lf_queue_slice&& r) noexcept {
        if (this != &r) {
            delete m_head;
            m_head = r.m_head;
            r.m_head = nullptr;
        }
        return *this;
    }

    ~lf_queue_slice() {
        delete m_head;
    }

    T& operator*() const {
        return m_head->m_data;
    }
    T* operator->() const {
        return std::addressof(m_head->m_data);
    }

    explicit operator bool() const {
        return m_head != nullptr;
    }

    T* get() const {
        return m_head ? std::addressof(m_head->m_data) : nullptr;
    }

    lf_queue_iterator<T> begin() const {
        lf_queue_iterator<T> it;
        it.m_ptr = m_head;
        return it;
    }

    lf_queue_iterator<T> end() const {
        return {};
    }

    const T& operator[](usz index) const noexcept {
        lf_queue_iterator<T> it = begin();
        while (--index != umax)
            ++it;
        return *it;
    }

    T& operator[](usz index) noexcept {
        lf_queue_iterator<T> it = begin();
        while (--index != umax)
            ++it;
        return *it;
    }

    lf_queue_slice& pop_front() {
        if (m_head) {
            auto* old = m_head;
            m_head = m_head->m_link;
            old->m_link = nullptr;
            delete old;
        }
        return *this;
    }
};

// =============================================================================
// lf_queue<T> — MPSC linked-list queue
// =============================================================================
template <typename T>
class lf_queue final {
private:
    struct fat_ptr {
        u64 ptr;
        u32 is_non_null;
        u32 reserved;
    };

    static_assert(std::is_trivially_copyable_v<fat_ptr>,
                  "fat_ptr must be trivially copyable for atomic operations");

    std::atomic<fat_ptr> m_head{fat_ptr{0, 0, 0}};

    std::atomic<u32> m_wait{0};

    static lf_queue_item<T>* load(fat_ptr v) noexcept {
        return reinterpret_cast<lf_queue_item<T>*>(static_cast<std::uintptr_t>(v.ptr));
    }

    // Drain the head and reverse the chain in place, turning the
    // FILO push order into FIFO consumption order.
    lf_queue_item<T>* reverse() noexcept {
        fat_ptr taken = m_head.exchange(fat_ptr{0, 0, 0}, std::memory_order_acq_rel);
        lf_queue_item<T>* head = load(taken);

        if (!head)
            return nullptr;

        if (auto* prev = head->m_link) {
            head->m_link = nullptr;
            do {
                auto* pprev = prev->m_link;
                prev->m_link = head;
                head = std::exchange(prev, pprev);
            } while (prev);
        }

        return head;
    }

public:
    constexpr lf_queue() = default;

    lf_queue(lf_queue&& other) noexcept {
        m_head.store(other.m_head.exchange(fat_ptr{0, 0, 0}), std::memory_order_release);
    }

    lf_queue& operator=(lf_queue&& other) noexcept {
        if (this == std::addressof(other))
            return *this;

        // Take other's head, swap into ours, delete the (orphaned) old head.
        auto stolen = other.m_head.exchange(fat_ptr{0, 0, 0}, std::memory_order_acq_rel);
        auto ousted = m_head.exchange(stolen, std::memory_order_acq_rel);
        delete load(ousted);
        return *this;
    }

    ~lf_queue() {
        delete load(m_head.load(std::memory_order_acquire));
    }

    // Block until the queue is non-empty. No-op if already non-empty.
    void wait(std::nullptr_t = nullptr) noexcept {
        if (!operator bool())
            m_wait.wait(0);
    }

    std::atomic<u32>& get_wait_atomic() noexcept {
        return m_wait;
    }

    const volatile void* observe() const noexcept {
        return load(m_head.load(std::memory_order_acquire));
    }

    explicit operator bool() const noexcept {
        return observe() != nullptr;
    }

    // Push a new element constructed from args. Returns true if the
    // queue was empty before this push (i.e. this is the wakeup edge).
    template <bool Notify = true, typename... Args>
    bool push(Args&&... args) {
        auto* item = new lf_queue_item<T>(nullptr, std::forward<Args>(args)...);

        fat_ptr cur = m_head.load(std::memory_order_acquire);
        for (;;) {
            item->m_link = load(cur);
            fat_ptr next{reinterpret_cast<u64>(item), 1u, 0u};

            if (m_head.compare_exchange_weak(cur, next, std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
                const bool was_empty = (cur.ptr == 0);
                if (was_empty) {
                    // Bump the wait-atomic so wait()'s value-tagged
                    // sleep wakes up.
                    m_wait.fetch_add(1, std::memory_order_release);
                    if constexpr (Notify)
                        m_wait.notify_one();
                }
                return was_empty;
            }
        }
    }

    void notify(bool force = false) {
        if (force || operator bool())
            m_wait.notify_one();
    }

    // Drain the queue. Use in range-for: `for (auto&& x : q.pop_all()) ...`
    lf_queue_slice<T> pop_all() {
        lf_queue_slice<T> result;
        result.m_head = reverse();
        return result;
    }

    // Drain the queue in original push order (newest first).
    lf_queue_slice<T> pop_all_reversed() {
        lf_queue_slice<T> result;
        fat_ptr taken = m_head.exchange(fat_ptr{0, 0, 0}, std::memory_order_acq_rel);
        result.m_head = load(taken);
        return result;
    }

    template <typename F>
    usz apply(F func) {
        usz count = 0;
        for (auto slice = pop_all(); slice; slice.pop_front()) {
            std::invoke(func, *slice);
            ++count;
        }
        return count;
    }
};

template <typename T>
class lf_bunch final {
    std::atomic<lf_queue_item<T>*> m_head{nullptr};

public:
    constexpr lf_bunch() noexcept = default;

    ~lf_bunch() {
        delete m_head.load(std::memory_order_acquire);
    }

    template <typename... Args>
    T* push(Args&&... args) noexcept {
        auto* item = new lf_queue_item<T>(nullptr, std::forward<Args>(args)...);

        auto* expected = m_head.load(std::memory_order_acquire);
        do {
            item->m_link = expected;
        } while (!m_head.compare_exchange_weak(expected, item, std::memory_order_acq_rel,
                                               std::memory_order_acquire));

        return std::addressof(item->m_data);
    }

    template <typename F, typename... Args>
    T* push_if(F pred, Args&&... args) noexcept {
        auto* expected = m_head.load(std::memory_order_acquire);
        auto* item = new lf_queue_item<T>(expected, std::forward<Args>(args)...);

        lf_queue_item<T>* checked = nullptr;

        do {
            item->m_link = expected;

            for (auto* p = expected; p != checked; p = p->m_link) {
                if (!pred(item->m_data, p->m_data)) {
                    item->m_link = nullptr;
                    delete item;
                    return nullptr;
                }
            }

            checked = expected;
        } while (!m_head.compare_exchange_weak(expected, item, std::memory_order_acq_rel,
                                               std::memory_order_acquire));

        return std::addressof(item->m_data);
    }

    lf_queue_iterator<T> begin() const {
        lf_queue_iterator<T> it;
        it.m_ptr = m_head.load(std::memory_order_acquire);
        return it;
    }

    lf_queue_iterator<T> end() const {
        return {};
    }
};