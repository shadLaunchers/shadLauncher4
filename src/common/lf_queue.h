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

// Simple unshrinkable array base for concurrent access. Only grows automatically.
// T must be DefaultConstructible (constexpr default ctor recommended).
template <typename T, usz N = std::max<usz>(256 / sizeof(T), 1)>
class lf_array {
    // Data (default-initialized)
    T m_data[N]{};

    // Next array block (atomic pointer)
    std::atomic<lf_array*> m_next{nullptr};

public:
    constexpr lf_array() = default;

    ~lf_array() {
        // delete all subsequent blocks
        for (auto ptr = m_next.load(std::memory_order_acquire); ptr;) {
            auto next = ptr->m_next.load(std::memory_order_acquire);
            ptr->m_next.store(nullptr, std::memory_order_relaxed);
            delete ptr;
            ptr = next;
        }
    }

    T& operator[](usz index) {
        lf_array* cur = this;

        for (usz i = 0;; i += N) {
            if (index - i < N) {
                return cur->m_data[index - i];
            }

            lf_array* next = cur->m_next.load(std::memory_order_acquire);

            if (!next) {
                // Prevent too large jumps; mimic original ensure: allow up to 2 blocks from current
                // If user asked for index > N*2 from this block, behaviour is undefined (we
                // assert). Here we'll do a runtime check and attempt to allocate a single new
                // block.
                if (!(index - i < N * 2)) {
                    // out of allowed range in original code; prefer to avoid uncontrolled growth
                    throw std::out_of_range("lf_array: index too large from current growth policy");
                }

                // Try to install a new block
                lf_array* new_block = new lf_array();

                lf_array* expected = nullptr;
                if (cur->m_next.compare_exchange_strong(expected, new_block,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
                    next = new_block;
                } else {
                    // someone else installed it
                    delete new_block;
                    next = expected;
                }
            }

            cur = next;
        }
    }

    // for_each: invokes func(T&) for each element; if func returns non-void and a truthy value,
    // returns pair(pointer-to-element, returned-value). If is_finite==false and list ends, allocate
    // further blocks on demand.
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
                    if (ret) {
                        return std::make_pair(std::addressof(cur->m_data[j]), std::move(ret));
                    }
                }
            }

            lf_array* next = cur->m_next.load(std::memory_order_acquire);

            if constexpr (!std::is_void_v<return_t>) {
                if (!next && !is_finite) {
                    // Try to install a new block if missing
                    lf_array* new_block = new lf_array();
                    lf_array* expected = nullptr;
                    if (cur->m_next.compare_exchange_strong(expected, new_block,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire)) {
                        next = new_block;
                    } else {
                        delete new_block;
                        next = expected;
                    }
                }
            }

            cur = next;
        }

        if constexpr (!std::is_void_v<return_t>) {
            return std::make_pair(static_cast<T*>(nullptr), return_t());
        }
    }

    u64 size() const {
        u64 size_n = 0;
        for (auto ptr = this; ptr; ptr = ptr->m_next.load(std::memory_order_acquire)) {
            size_n += N;
        }
        return size_n;
    }
};

// Simple lock-free FIFO queue base built on lf_array. Uses 64-bit control word:
// LSB 32-bit: push counter, MSB 32-bit: pop counter.
template <typename T, usz N = std::max<usz>(256 / sizeof(T), 1)>
class lf_fifo : public lf_array<T, N> {
    std::atomic<u64> m_ctrl{0};

public:
    constexpr lf_fifo() = default;

    // number of elements currently pushed but not popped (may wrap but using 32-bit deltas)
    u32 size() const {
        const u64 ctrl = m_ctrl.load(std::memory_order_acquire);
        return static_cast<u32>(ctrl - (ctrl >> 32));
    }

    // Acquire place for one or more elements: returns the starting index (push counter prior to
    // addition)
    u32 push_begin(u32 count = 1) {
        return static_cast<u32>(
            m_ctrl.fetch_add(static_cast<u64>(count), std::memory_order_acq_rel));
    }

    // Get current pop position
    u32 peek() const {
        return static_cast<u32>(m_ctrl.load(std::memory_order_acquire) >> 32);
    }

    // Acknowledge processed element(s), return number of the next pop index (or 0 if cleaned)
    u32 pop_end(u32 count = 1) {
        while (true) {
            u64 old = m_ctrl.load(std::memory_order_acquire);
            u64 ctrl = old + (static_cast<u64>(count) << 32);
            // if msb equals lsb (as 32-bit), we can reset to 0
            if ((ctrl >> 32) == static_cast<u32>(ctrl)) {
                ctrl = 0;
            }
            if (m_ctrl.compare_exchange_strong(old, ctrl, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
                return static_cast<u32>(ctrl >> 32);
            }
            // otherwise retry with updated old
        }
    }
};

// Helper type: linked list element
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
        for (lf_queue_item* ptr = m_link; ptr;) {
            auto next = ptr->m_link;
            ptr->m_link = nullptr;
            delete ptr;
            ptr = next;
        }
    }
};

// Forward iterator: non-owning pointer to the list element in lf_queue_slice<>
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

// Owning pointer to the linked list taken from the lf_queue<>
template <typename T>
class lf_queue_slice {
    lf_queue_item<T>* m_head = nullptr;

    template <typename U>
    friend class lf_queue;

public:
    constexpr lf_queue_slice() = default;
    lf_queue_slice(const lf_queue_slice&) = delete;

    lf_queue_slice(lf_queue_slice&& r) noexcept : m_head(r.m_head) {
        r.m_head = nullptr;
    }

    lf_queue_slice& operator=(const lf_queue_slice&) = delete;

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
        while (index-- != umax)
            ++it;
        return *it;
    }

    T& operator[](usz index) noexcept {
        lf_queue_iterator<T> it = begin();
        while (index-- != umax)
            ++it;
        return *it;
    }

    lf_queue_slice& pop_front() {
        if (m_head) {
            auto old = m_head;
            m_head = m_head->m_link;
            old->m_link = nullptr;
            delete old;
        }
        return *this;
    }
};

// Linked list-based multi-producer queue (consumer drains entire queue)
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
    // Dedicated atomic for wait/notify (C++20 atomic wait/notify)
    std::atomic<u32> m_wait{0};

    lf_queue_item<T>* load(fat_ptr value) const noexcept {
        return reinterpret_cast<lf_queue_item<T>*>(static_cast<std::uintptr_t>(value.ptr));
    }

    // Extract all elements and reverse element order (FILO -> FIFO)
    lf_queue_item<T>* reverse() noexcept {
        fat_ptr empty{0, 0, 0};
        fat_ptr old = m_head.load(std::memory_order_acquire);
        if (old.ptr == 0)
            return nullptr;

        if (m_head.exchange(empty, std::memory_order_acq_rel).ptr) {
            lf_queue_item<T>* head = load(old);
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
        return nullptr;
    }

public:
    constexpr lf_queue() = default;

    lf_queue(lf_queue&& other) noexcept {
        m_head.store(other.m_head.exchange(fat_ptr{0, 0, 0}), std::memory_order_release);
    }

    lf_queue& operator=(lf_queue&& other) noexcept {
        if (this == std::addressof(other))
            return *this;
        auto old = m_head.exchange(other.m_head.exchange(fat_ptr{0, 0, 0}));
        delete load(old);
        return *this;
    }

    ~lf_queue() {
        delete load(m_head.load(std::memory_order_acquire));
    }

    void wait(std::nullptr_t = nullptr) noexcept {
        if (!operator bool()) {
            // wait on m_wait when value equals 0
            m_wait.wait(0);
        }
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

    template <bool Notify = true, typename... Args>
    bool push(Args&&... args) {
        fat_ptr old = m_head.load(std::memory_order_acquire);
        auto item = new lf_queue_item<T>(load(old), std::forward<Args>(args)...);

        // attempt to push at head
        while (true) {
            fat_ptr cur = m_head.load(std::memory_order_acquire);
            item->m_link = load(cur);
            fat_ptr newv{reinterpret_cast<u64>(item), item != nullptr ? 1u : 0u, 0u};
            if (m_head.compare_exchange_strong(cur, newv, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
                // if queue was empty before push, notify waiters
                if (cur.ptr == 0 && Notify) {
                    m_wait.notify_one();
                }
                return cur.ptr == 0;
            }
            // otherwise loop; m_head updated in cur
        }
    }

    void notify(bool force = false) {
        if (force || operator bool()) {
            m_wait.notify_one();
        }
    }

    // Withdraw the list, supports range-for: for (auto&& x : q.pop_all()) ...
    lf_queue_slice<T> pop_all() {
        lf_queue_slice<T> result;
        result.m_head = reverse();
        return result;
    }

    // Withdraw the list in reverse order (LIFO/FILO)
    lf_queue_slice<T> pop_all_reversed() {
        lf_queue_slice<T> result;
        fat_ptr empty{0, 0, 0};
        fat_ptr old = m_head.exchange(empty, std::memory_order_acq_rel);
        result.m_head = load(old);
        return result;
    }

    // Apply func(data) to each element, return the total length (counting)
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

// Concurrent linked list, elements remain until destroyed.
template <typename T>
class lf_bunch final {
    std::atomic<lf_queue_item<T>*> m_head{nullptr};

public:
    constexpr lf_bunch() noexcept = default;

    ~lf_bunch() {
        delete m_head.load(std::memory_order_acquire);
    }

    // Add unconditionally
    template <typename... Args>
    T* push(Args&&... args) noexcept {
        auto old = m_head.load(std::memory_order_acquire);
        auto item = new lf_queue_item<T>(old, std::forward<Args>(args)...);
        while (!m_head.compare_exchange_strong(old, item, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
            item->m_link = old;
        }
        return std::addressof(item->m_data);
    }

    // Add if pred(item, all_items) is true for all existing items
    template <typename F, typename... Args>
    T* push_if(F pred, Args&&... args) noexcept {
        auto old = m_head.load(std::memory_order_acquire);
        lf_queue_item<T>* chk = old;
        auto item = new lf_queue_item<T>(old, std::forward<Args>(args)...);

        chk = nullptr;
        do {
            item->m_link = old;

            // Check all items in the queue
            for (auto ptr = old; ptr != chk; ptr = ptr->m_link) {
                if (!pred(item->m_data, ptr->m_data)) {
                    item->m_link = nullptr;
                    delete item;
                    return nullptr;
                }
            }

            chk = old;
        } while (!m_head.compare_exchange_strong(old, item, std::memory_order_acq_rel,
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
