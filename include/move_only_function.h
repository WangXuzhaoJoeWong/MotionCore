#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace wxz::core {

// A minimal move-only function wrapper with small-buffer optimization.
//
// Design goals:
// - Accept move-only callables (e.g., lambdas capturing move-only leases).
// - Avoid heap allocation for typical small callables.
// - C++17 compatible.
class MoveOnlyFunction {
public:
    MoveOnlyFunction() = default;
    MoveOnlyFunction(std::nullptr_t) {}

    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    MoveOnlyFunction(MoveOnlyFunction&& other) noexcept { move_from(std::move(other)); }
    MoveOnlyFunction& operator=(MoveOnlyFunction&& other) noexcept {
        if (this == &other) return *this;
        reset();
        move_from(std::move(other));
        return *this;
    }

    ~MoveOnlyFunction() { reset(); }

    template <class F,
              class DF = std::decay_t<F>,
              std::enable_if_t<!std::is_same_v<DF, MoveOnlyFunction>, int> = 0>
    MoveOnlyFunction(F&& f) {
        emplace<DF>(std::forward<F>(f));
    }

    explicit operator bool() const { return call_ != nullptr; }

    void operator()() noexcept {
        if (call_) call_(obj_);
    }

    void reset() noexcept {
        if (!call_) return;
        destroy_(obj_, on_heap_);
        obj_ = nullptr;
        call_ = nullptr;
        destroy_ = nullptr;
        move_ = nullptr;
        on_heap_ = false;
    }

private:
    static constexpr std::size_t kStorageSize = 128;
    using Storage = std::aligned_storage_t<kStorageSize, alignof(std::max_align_t)>;

    template <class T, class... Args>
    void emplace(Args&&... args) {
        reset();
        if constexpr (sizeof(T) <= kStorageSize && alignof(T) <= alignof(Storage) && std::is_nothrow_move_constructible_v<T>) {
            obj_ = &storage_;
            new (obj_) T(std::forward<Args>(args)...);
            on_heap_ = false;

            call_ = [](void* p) noexcept {
                try {
                    (*static_cast<T*>(p))();
                } catch (...) {
                }
            };
            destroy_ = [](void* p, bool) noexcept { static_cast<T*>(p)->~T(); };
            move_ = [](void* dst_storage, void*& dst_obj, void* src_storage, void*& src_obj, bool& dst_on_heap, bool& src_on_heap) noexcept {
                dst_obj = dst_storage;
                new (dst_obj) T(std::move(*static_cast<T*>(src_obj)));
                static_cast<T*>(src_obj)->~T();
                src_obj = nullptr;
                dst_on_heap = false;
                src_on_heap = false;
            };
        } else {
            obj_ = new T(std::forward<Args>(args)...);
            on_heap_ = true;

            call_ = [](void* p) noexcept {
                try {
                    (*static_cast<T*>(p))();
                } catch (...) {
                }
            };
            destroy_ = [](void* p, bool) noexcept { delete static_cast<T*>(p); };
            move_ = [](void*, void*& dst_obj, void*, void*& src_obj, bool& dst_on_heap, bool& src_on_heap) noexcept {
                dst_obj = src_obj;
                src_obj = nullptr;
                dst_on_heap = true;
                src_on_heap = false;
            };
        }
    }

    void move_from(MoveOnlyFunction&& other) noexcept {
        if (!other.call_) return;
        call_ = other.call_;
        destroy_ = other.destroy_;
        move_ = other.move_;
        if (move_) {
            move_(&storage_, obj_, &other.storage_, other.obj_, on_heap_, other.on_heap_);
        }
        other.call_ = nullptr;
        other.destroy_ = nullptr;
        other.move_ = nullptr;
        other.on_heap_ = false;
    }

    Storage storage_{};
    void* obj_{nullptr};
    void (*call_)(void*) noexcept = nullptr;
    void (*destroy_)(void*, bool) noexcept = nullptr;
    void (*move_)(void* dst_storage,
                  void*& dst_obj,
                  void* src_storage,
                  void*& src_obj,
                  bool& dst_on_heap,
                  bool& src_on_heap) noexcept = nullptr;
    bool on_heap_{false};
};

} // namespace wxz::core
