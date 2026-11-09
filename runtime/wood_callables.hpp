// Owned callable values, inline iterator steps, and owned cursor state.
#ifndef WOOD_CALLABLES_HPP
#define WOOD_CALLABLES_HPP
#include <cstddef>
#include <new>
#include <type_traits>
namespace wd {
template <typename R, typename... A> class Callable {
    static constexpr std::size_t capacity = 32;
    alignas(std::max_align_t) std::byte storage_[capacity];
    struct Table {
        R (*invoke)(void*, A...);
        void (*copy)(void*, const void*);
        void (*move)(void*, void*) noexcept;
        void (*destroy)(void*) noexcept;
    };
    const Table* table_ = nullptr;
    template <typename F>
    static constexpr bool local =
        sizeof(F) <= capacity && alignof(F) <= alignof(std::max_align_t) &&
        std::is_nothrow_move_constructible_v<F>;
    template <typename F> static F& access(void* storage) {
        if constexpr (local<F>)
            return *std::launder(reinterpret_cast<F*>(storage));
        else
            return **std::launder(reinterpret_cast<F**>(storage));
    }
    template <typename F> static const F& access(const void* storage) {
        if constexpr (local<F>)
            return *std::launder(reinterpret_cast<const F*>(storage));
        else
            return **std::launder(reinterpret_cast<F* const*>(storage));
    }
    template <typename F> static const Table* operations() {
        static const Table table{[](void* from, A... args) -> R {
                                     if constexpr (std::is_void_v<R>)
                                         access<F>(from)(std::move(args)...);
                                     else
                                         return access<F>(from)(std::move(args)...);
                                 },
                                 [](void* to, const void* from) {
                                     if constexpr (local<F>)
                                         ::new (to) F(access<F>(from));
                                     else
                                         ::new (to) F*(new F(access<F>(from)));
                                 },
                                 [](void* to, void* from) noexcept {
                                     if constexpr (local<F>) {
                                         ::new (to) F(std::move(access<F>(from)));
                                         access<F>(from).~F();
                                     } else
                                         ::new (to) F*(*std::launder(reinterpret_cast<F**>(from)));
                                 },
                                 [](void* from) noexcept {
                                     if constexpr (local<F>)
                                         access<F>(from).~F();
                                     else
                                         delete *std::launder(reinterpret_cast<F**>(from));
                                 }};
        return &table;
    }

  public:
    Callable() = delete;
    template <typename F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, Callable> &&
                 std::is_invocable_r_v<R, std::remove_cvref_t<F>&, A...>)
    Callable(F&& function) {
        using Value = std::remove_cvref_t<F>;
        if constexpr (local<Value>)
            ::new (static_cast<void*>(storage_)) Value(std::forward<F>(function));
        else
            ::new (static_cast<void*>(storage_)) Value*(new Value(std::forward<F>(function)));
        table_ = operations<Value>();
    }
    Callable(const Callable& other) {
        if (other.table_)
            other.table_->copy(storage_, other.storage_);
        table_ = other.table_;
    }
    Callable(Callable&& other) noexcept : table_(std::exchange(other.table_, nullptr)) {
        if (table_)
            table_->move(storage_, other.storage_);
    }
    Callable& operator=(Callable other) noexcept {
        swap(other);
        return *this;
    }
    void swap(Callable& other) noexcept {
        Callable temporary(std::move(other));
        other.table_ = std::exchange(table_, nullptr);
        if (other.table_)
            other.table_->move(other.storage_, storage_);
        table_ = std::exchange(temporary.table_, nullptr);
        if (table_)
            table_->move(storage_, temporary.storage_);
    }
    ~Callable() {
        if (table_)
            table_->destroy(storage_);
    }
    R operator()(A... args) {
        if (!table_)
            error({"", 0, 0}, "R4025", "cannot invoke a moved-from callable");
        return table_->invoke(storage_, std::move(args)...);
    }
};
template <typename R, typename... A> std::string to_str(const Callable<R, A...>&) {
    return "<callable>";
}
template <typename T> struct Step {
    std::optional<T> value;
    bool operator==(const Step&) const = default;
};
template <typename T> Step<T> yielded(T value) {
    return {std::optional<T>{std::in_place, std::move(value)}};
}
template <typename T> bool has_item(const Step<T>& step) {
    return step.value.has_value();
}
template <typename T> T step_value(const Step<T>& step, Site site) {
    if (!step.value)
        error(site, "R4024", "cannot read an exhausted iterator step");
    return *step.value;
}
template <typename T> T step_value(Step<T>&& step, Site site) {
    if (!step.value)
        error(site, "R4024", "cannot read an exhausted iterator step");
    return std::move(*step.value);
}
template <typename T> std::string to_str(const Step<T>& step) {
    return step.value ? "yielded(" + repr(*step.value) + ")" : "finished()";
}
inline std::string to_str(const Range&) {
    return "<range>";
}
inline Range make_range(Int stop, Site site) {
    return Range(0, stop, 1, site);
}
inline Range make_range(Int start, Int stop, Site site) {
    return Range(start, stop, 1, site);
}
inline Range make_range(Int start, Int stop, Int stride, Site site) {
    return Range(start, stop, stride, site);
}
template <typename S> class Cursor;
template <typename T> class Cursor<List<T>> {
    List<T> source_;
    Int index_ = 0;

  public:
    explicit Cursor(List<T> source) : source_(std::move(source)) {}
    Step<T> next(Site site) {
        if (static_cast<std::uint64_t>(index_) >= source_.size())
            return {};
        return yielded(source_.get(index_++, site));
    }
};
template <typename T> class Cursor<Array<T>> {
    Array<T> source_;
    std::size_t index_ = 0;

  public:
    explicit Cursor(Array<T> source) : source_(std::move(source)) {}
    Step<T> next(Site) {
        if (index_ == source_.size())
            return {};
        return yielded(source_.flat(index_++));
    }
};
template <> class Cursor<std::string> {
    std::string source_;
    Int index_ = 0;

  public:
    explicit Cursor(std::string source) : source_(std::move(source)) {}
    Step<std::string> next(Site site) {
        if (static_cast<std::uint64_t>(index_) >= source_.size())
            return {};
        return yielded(at(source_, index_++, site));
    }
};
template <typename K, typename V> class Cursor<Dict<K, V>> {
    Cursor<List<K>> keys_;

  public:
    explicit Cursor(Dict<K, V> source) : keys_(keys(source)) {}
    Step<K> next(Site site) {
        return keys_.next(site);
    }
};
template <> class Cursor<Range> {
    Range source_;

  public:
    explicit Cursor(Range source) : source_(std::move(source)) {}
    Step<Int> next(Site) {
        if (!source_.has_next())
            return {};
        return yielded(source_.next());
    }
};
inline Step<Int> next(Range& source, Site) {
    if (!source.has_next())
        return {};
    return yielded(source.next());
}
template <typename S> Cursor<S> iter(S source) {
    return Cursor<S>(std::move(source));
}
template <typename S> auto next(Cursor<S>& cursor, Site site) {
    return cursor.next(site);
}
template <typename S> std::string to_str(const Cursor<S>&) {
    return "<cursor>";
}
} // namespace wd
#endif
