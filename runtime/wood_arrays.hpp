// Fixed-shape numeric values with contiguous ownership and strided COW snapshots.
#ifndef WOOD_ARRAYS_HPP
#define WOOD_ARRAYS_HPP
#include <array>
#include <type_traits>
namespace wd {
inline constexpr std::size_t array_max_rank = 8;
inline void array_error(Site site, const char* message) {
    error(site, "R4026", message);
}
struct ArrayShape {
    std::size_t rank = 1;
    std::array<Int, array_max_rank> extents{};
    std::array<Int, array_max_rank> strides{};
    std::size_t count = 0;
    ArrayShape dense() const {
        auto result = *this;
        Int step = count ? 1 : 0;
        for (std::size_t reverse = rank; reverse != 0; --reverse) {
            const auto axis = reverse - 1;
            result.strides[axis] = step;
            step *= extents[axis];
        }
        return result;
    }
    static ArrayShape from_values(const std::array<Int, array_max_rank>& dimensions,
                                  std::size_t rank, Site site) {
        if (rank == 0 || rank > array_max_rank)
            array_error(site, "array rank must be in 1..8");
        ArrayShape result;
        result.rank = rank;
        Int product = 1;
        for (std::size_t reverse = result.rank; reverse != 0; --reverse) {
            const auto axis = reverse - 1;
            const auto extent = dimensions[axis];
            if (extent < 0 || (extent != 0 && product > max_int / extent))
                array_error(site,
                            "array dimensions must be nonnegative and their product must fit int");
            result.extents[axis] = extent;
            result.strides[axis] = product;
            product *= extent;
        }
        result.count = static_cast<std::size_t>(product);
        return result;
    }
    static ArrayShape make(const List<Int>& dimensions, Site site) {
        if (dimensions.size() > array_max_rank)
            array_error(site, "array rank must be in 1..8");
        std::array<Int, array_max_rank> values{};
        for (std::size_t i = 0; i < dimensions.size(); ++i)
            values[i] = dimensions.get(static_cast<Int>(i), site);
        return from_values(values, dimensions.size(), site);
    }
    template <typename... I> static ArrayShape make(const std::tuple<I...>& dimensions, Site site) {
        static_assert(sizeof...(I) <= array_max_rank);
        std::array<Int, array_max_rank> values{};
        std::size_t i = 0;
        std::apply([&](auto... value) { ((values[i++] = value), ...); }, dimensions);
        return from_values(values, sizeof...(I), site);
    }
    static ArrayShape make(Int size, Site site) {
        return make(std::tuple{size}, site);
    }
};
template <typename T> class Array {
    static_assert(std::is_same_v<T, Int> || std::is_same_v<T, double>);
    std::shared_ptr<std::vector<T>> storage_;
    ArrayShape shape_;
    Int offset_ = 0;
    Int physical(std::size_t flat) const {
        Int offset = offset_;
        for (std::size_t reverse = shape_.rank; reverse != 0; --reverse) {
            const auto axis = reverse - 1;
            const auto extent = static_cast<std::size_t>(shape_.extents[axis]);
            offset += static_cast<Int>(flat % extent) * shape_.strides[axis];
            flat /= extent;
        }
        return offset;
    }
    template <typename... I> Int coordinate(const std::tuple<I...>& indices, Site site) const {
        if (sizeof...(I) != shape_.rank)
            array_error(site, "one integer index is required for each array axis");
        Int offset = offset_;
        std::size_t axis = 0;
        std::apply(
            [&](auto... index) {
                ((offset += static_cast<Int>(index_offset(
                                index, static_cast<std::size_t>(shape_.extents[axis]), site)) *
                            shape_.strides[axis],
                  ++axis),
                 ...);
            },
            indices);
        return offset;
    }
    void detach(Site site) {
        if (size() != 0 && (!storage_ || storage_.use_count() != 1))
            *this = copy(site);
    }

  public:
    Array() = default; // valid empty rank-one value, also safe after a move
    Array(const Array&) = default;
    Array& operator=(const Array&) = default;
    Array(Array&& other) noexcept
        : storage_(std::move(other.storage_)), shape_(other.shape_), offset_(other.offset_) {
        other.shape_ = {};
        other.offset_ = 0;
    }
    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            storage_ = std::move(other.storage_);
            shape_ = other.shape_;
            offset_ = other.offset_;
            other.shape_ = {};
            other.offset_ = 0;
        }
        return *this;
    }
    static Array full(const List<Int>& dimensions, T fill, Site site) {
        return from_layout(ArrayShape::make(dimensions, site), fill, site);
    }
    template <typename S> static Array full(const S& dimensions, T fill, Site site) {
        return from_layout(ArrayShape::make(dimensions, site), fill, site);
    }
    static Array from_layout(ArrayShape shape, T fill, Site site) {
        Array result;
        result.shape_ = shape.dense();
        if (result.size() > std::vector<T>{}.max_size())
            array_error(site, "array exceeds native allocation limits");
        if (result.size())
            result.storage_ = std::make_shared<std::vector<T>>(result.size(), fill);
        return result;
    }
    std::size_t size() const {
        return shape_.count;
    }
    std::size_t rank() const {
        return shape_.rank;
    }
    const ArrayShape& layout() const {
        return shape_;
    }
    Int extent(std::size_t axis) const {
        return shape_.extents[axis];
    }
    Int stride(std::size_t axis) const {
        return shape_.strides[axis];
    }
    bool contiguous() const {
        if (!size())
            return true;
        Int stride = 1;
        for (std::size_t reverse = rank(); reverse != 0; --reverse) {
            const auto axis = reverse - 1;
            if (extent(axis) > 1 && shape_.strides[axis] != stride)
                return false;
            stride *= extent(axis);
        }
        return true;
    }
    bool reusable() const {
        return storage_ && storage_.use_count() == 1 && contiguous();
    }
    List<Int> dimensions() const {
        List<Int> result;
        for (std::size_t axis = 0; axis < rank(); ++axis)
            result.push(extent(axis), {"<array>", 0, 0});
        return result;
    }
    bool same_shape(const Array& other) const {
        return rank() == other.rank() && shape_.extents == other.shape_.extents;
    }
    // Pointers stay inside native kernels or a synchronous, declared C borrow.
    const T* data() const {
        return size() ? storage_->data() + offset_ : nullptr;
    }
    const T* foreign_data(Site site) const {
        if (!contiguous())
            array_error(site, "native buffer imports require contiguous arrays; use num.copy");
        return data();
    }
    T* foreign_mutable_data(Site site) {
        if (!contiguous())
            array_error(site,
                        "mutable native buffer imports require contiguous arrays; use num.copy");
        detach(site);
        return size() ? storage_->data() + offset_ : nullptr;
    }
    T flat(std::size_t index) const {
        return contiguous() ? data()[index]
                            : (*storage_)[static_cast<std::size_t>(physical(index))];
    }
    T flat_checked(Int index, Site site) const {
        return flat(index_offset(index, size(), site));
    }
    template <typename... I> T get(const std::tuple<I...>& index, Site site) const {
        return (*storage_)[static_cast<std::size_t>(coordinate(index, site))];
    }
    T get(Int index, Site site) const {
        return get(std::tuple{index}, site);
    }
    template <typename... I> T& mut_at(const std::tuple<I...>& index, Site site) {
        // Validate against the old descriptor before allocating; reacquire after detaching.
        static_cast<void>(coordinate(index, site));
        detach(site);
        return (*storage_)[static_cast<std::size_t>(coordinate(index, site))];
    }
    T& mut_at(Int index, Site site) {
        return mut_at(std::tuple{index}, site);
    }
    Array copy(Site) const {
        Array result;
        result.shape_ = shape_.dense();
        if (size()) {
            if (contiguous())
                result.storage_ = std::make_shared<std::vector<T>>(data(), data() + size());
            else {
                result.storage_ = std::make_shared<std::vector<T>>();
                result.storage_->reserve(size());
                for (std::size_t i = 0; i < size(); ++i)
                    result.storage_->push_back(flat(i));
            }
        }
        return result;
    }
    template <typename S> Array reshape(const S& dimensions, Site site) const {
        const auto shape = ArrayShape::make(dimensions, site);
        if (shape.count != size())
            array_error(site, "reshape must preserve the number of elements");
        auto result = contiguous() ? *this : copy(site);
        result.shape_ = shape;
        return result;
    }
    Array transpose(const List<Int>& axes, Site site) const {
        if (axes.size() != 0 && axes.size() != rank())
            array_error(site, "transpose requires a permutation of all axes");
        Array result = *this;
        std::array<bool, array_max_rank> seen{};
        for (std::size_t i = 0; i < rank(); ++i) {
            const auto axis = axes.size() ? axes.get(static_cast<Int>(i), site)
                                          : static_cast<Int>(rank() - 1 - i);
            if (axis < 0 || static_cast<std::size_t>(axis) >= rank() ||
                seen[static_cast<std::size_t>(axis)])
                array_error(site, "transpose axes must form a permutation");
            seen[static_cast<std::size_t>(axis)] = true;
            result.shape_.extents[i] = extent(static_cast<std::size_t>(axis));
            result.shape_.strides[i] = stride(static_cast<std::size_t>(axis));
        }
        return result;
    }
    Array sliced(Int axis_value, std::optional<Int> start, std::optional<Int> stop, Int step,
                 Site site) const {
        const auto axis = index_offset(axis_value, rank(), site);
        const auto slice =
            slice_indices(static_cast<std::size_t>(extent(axis)), start, stop, step, site);
        Array result = *this;
        result.shape_.extents[axis] = static_cast<Int>(slice.count);
        // Singleton/empty axes need no multiplied stride. This handles INT64_MIN
        // steps without rejecting perfectly valid one-item views.
        if (slice.count > 1)
            result.shape_.strides[axis] = mul(stride(axis), step, site);
        std::size_t count = 1;
        for (std::size_t i = 0; i < rank(); ++i)
            count *= static_cast<std::size_t>(result.extent(i));
        result.shape_.count = count;
        result.offset_ = count ? offset_ + slice.start * stride(axis) : 0;
        return result;
    }
    // Internal kernel access: clone only when storage is shared or strided.
    T* writable(Site site) {
        if (!contiguous())
            *this = copy(site);
        detach(site);
        return size() ? storage_->data() + offset_ : nullptr;
    }
    bool operator==(const Array& other) const {
        if (!same_shape(other))
            return false;
        for (std::size_t i = 0; i < size(); ++i)
            if (flat(i) != other.flat(i))
                return false;
        return true;
    }
};
template <typename S, typename T> Array<T> array_full(const S& shape, T value, Site site) {
    return Array<T>::full(shape, value, site);
}
template <typename T> Array<T> array_from_list(const List<T>& values, Site site) {
    auto result = Array<T>::full(std::tuple{static_cast<Int>(values.size())}, T{}, site);
    auto* output = result.writable(site);
    for (std::size_t i = 0; i < values.size(); ++i)
        output[i] = values.get(static_cast<Int>(i), site);
    return result;
}
template <typename T> Int len(const Array<T>& value, Site) {
    return static_cast<Int>(value.size());
}
template <typename T> T at(const Array<T>& value, Int index, Site site) {
    return value.get(index, site);
}
template <typename T, typename... I>
T at(const Array<T>& value, const std::tuple<I...>& index, Site site) {
    return value.get(index, site);
}
template <typename T>
Array<T> slice(const Array<T>& value, std::optional<Int> start, std::optional<Int> stop, Int step,
               Site site) {
    return value.sliced(0, start, stop, step, site);
}
template <typename T> List<Int> array_shape(const Array<T>& value) {
    return value.dimensions();
}
template <typename To, typename From>
Array<To> array_convert(const Array<From>& source, Site site) {
    if constexpr (std::is_same_v<To, From>)
        return source;
    else {
        auto result = Array<To>::from_layout(source.layout(), To{}, site);
        auto* output = result.writable(site);
        for (std::size_t i = 0; i < source.size(); ++i) {
            if constexpr (std::is_same_v<To, Int>)
                output[i] = to_int(source.flat(i), site);
            else
                output[i] = static_cast<double>(source.flat(i));
        }
        return result;
    }
}
template <typename T> Array<double> array_floats(const Array<T>& source, Site site) {
    return array_convert<double>(source, site);
}
template <typename T> Array<Int> array_integers(const Array<T>& source, Site site) {
    return array_convert<Int>(source, site);
}
template <typename T> Int array_rank(const Array<T>& value) {
    return static_cast<Int>(value.rank());
}
template <typename T> bool array_contiguous(const Array<T>& value) {
    return value.contiguous();
}
template <typename T> Array<T> array_copy(const Array<T>& value, Site site) {
    return value.copy(site);
}
template <typename T, typename S>
Array<T> array_reshape(const Array<T>& value, const S& shape, Site site) {
    return value.reshape(shape, site);
}
template <typename T>
Array<T> array_transpose(const Array<T>& value, const List<Int>& axes, Site site) {
    return value.transpose(axes, site);
}
template <typename T>
Array<T> array_slice(const Array<T>& value, Int axis, Int start, Int stop, Int step, Site site) {
    return value.sliced(axis, start, stop, step, site);
}
template <typename T> List<T> array_to_list(const Array<T>& value, Site site) {
    List<T> result;
    result.reserve(value.size(), site);
    for (std::size_t i = 0; i < value.size(); ++i)
        result.push(value.flat(i), site);
    return result;
}
template <typename T> std::string to_str(const Array<T>& value) {
    return "array(shape=" + to_str(value.dimensions()) +
           ", values=" + to_str(array_to_list(value, {"<array>", 0, 0})) + ")";
}
struct CompensatedSum {
    double total = 0, correction = 0;
    void add(double value) {
        const auto next = total + value;
        if (!std::isfinite(next)) {
            total = next;
            correction = 0;
            return;
        }
        correction +=
            std::abs(total) >= std::abs(value) ? (total - next) + value : (value - next) + total;
        total = next;
    }
    double value() const {
        return total + correction;
    }
};
template <typename T> T array_sum(const Array<T>& values, Site site) {
    const auto* contiguous = values.contiguous() ? values.data() : nullptr;
    if constexpr (std::is_same_v<T, Int>) {
        Int result = 0;
        for (std::size_t i = 0; i < values.size(); ++i)
            result = add(result, contiguous ? contiguous[i] : values.flat(i), site);
        return result;
    } else {
        CompensatedSum result;
        for (std::size_t i = 0; i < values.size(); ++i)
            result.add(contiguous ? contiguous[i] : values.flat(i));
        return result.value();
    }
}
template <typename T> T array_dot(const Array<T>& a, const Array<T>& b, Site site) {
    if (a.rank() != 1 || b.rank() != 1 || a.size() != b.size())
        array_error(site, "dot requires equal-length rank-one arrays");
    if constexpr (std::is_same_v<T, Int>) {
        Int result = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            result = add(result, mul(a.flat(i), b.flat(i), site), site);
        return result;
    } else {
        CompensatedSum result;
        for (std::size_t i = 0; i < a.size(); ++i)
            result.add(a.flat(i) * b.flat(i));
        return result.value();
    }
}
enum class ArrayOp { Add, Sub, Mul, Div };
template <typename T> struct ArrayOperand {
    static constexpr bool array = false;
    using Item = T;
};
template <typename T> struct ArrayOperand<Array<T>> {
    static constexpr bool array = true;
    using Item = T;
};
template <typename A, typename B, ArrayOp Op> auto array_binary(A a, B b, Site site) {
    using AI = typename ArrayOperand<A>::Item;
    using BI = typename ArrayOperand<B>::Item;
    using T = std::conditional_t<Op == ArrayOp::Div, double, std::common_type_t<AI, BI>>;
    const auto& shape = [&]() -> const auto& {
        if constexpr (ArrayOperand<A>::array)
            return a;
        else
            return b;
    }();
    if constexpr (ArrayOperand<A>::array && ArrayOperand<B>::array) {
        bool same = a.rank() == b.rank();
        if (same)
            for (std::size_t axis = 0; axis < a.rank(); ++axis)
                same = same && a.extent(axis) == b.extent(axis);
        if (!same)
            array_error(site, "elementwise arrays must have identical shapes");
    }
    const auto count = shape.size();
    const AI* ap = nullptr;
    const BI* bp = nullptr;
    if constexpr (ArrayOperand<A>::array)
        if (a.contiguous())
            ap = a.data();
    if constexpr (ArrayOperand<B>::array)
        if (b.contiguous())
            bp = b.data();
    const auto av = [&]([[maybe_unused]] std::size_t i) -> T {
        if constexpr (ArrayOperand<A>::array)
            return static_cast<T>(ap ? ap[i] : a.flat(i));
        else
            return static_cast<T>(a);
    };
    const auto bv = [&]([[maybe_unused]] std::size_t i) -> T {
        if constexpr (ArrayOperand<B>::array)
            return static_cast<T>(bp ? bp[i] : b.flat(i));
        else
            return static_cast<T>(b);
    };
    if constexpr (Op == ArrayOp::Div)
        for (std::size_t i = 0; i < count; ++i)
            if (bv(i) == 0)
                error(site, "R4002", "division by zero in array");
    Array<T> result;
    // Reuse an owned contiguous argument's buffer. Shared values detach below;
    // views and caller-visible aliases retain their snapshots.
    bool reused_a = false, reused_b = false;
    if constexpr (std::is_same_v<A, Array<T>>)
        if (a.reusable()) {
            result = std::move(a);
            reused_a = true;
        }
    if constexpr (std::is_same_v<B, Array<T>>)
        if (!reused_a && b.reusable()) {
            result = std::move(b);
            reused_b = true;
        }
    if (!reused_a && !reused_b)
        result = Array<T>::from_layout(shape.layout(), T{}, site);
    // A reused operand is read from its materialized output. Other operands keep
    // owning their input snapshots while writable() detaches as necessary.
    auto* output = result.writable(site);
    for (std::size_t i = 0; i < count; ++i) {
        const T left = reused_a ? output[i] : av(i);
        const T right = reused_b ? output[i] : bv(i);
        if constexpr (std::is_same_v<T, Int>) {
            if constexpr (Op == ArrayOp::Add)
                output[i] = add(left, right, site);
            if constexpr (Op == ArrayOp::Sub)
                output[i] = sub(left, right, site);
            if constexpr (Op == ArrayOp::Mul)
                output[i] = mul(left, right, site);
        } else {
            if constexpr (Op == ArrayOp::Add)
                output[i] = left + right;
            if constexpr (Op == ArrayOp::Sub)
                output[i] = left - right;
            if constexpr (Op == ArrayOp::Mul)
                output[i] = left * right;
            if constexpr (Op == ArrayOp::Div)
                output[i] = left / right;
        }
    }
    return result;
}
template <typename A, typename B> auto array_add(A a, B b, Site s) {
    return array_binary<A, B, ArrayOp::Add>(std::move(a), std::move(b), s);
}
template <typename A, typename B> auto array_sub(A a, B b, Site s) {
    return array_binary<A, B, ArrayOp::Sub>(std::move(a), std::move(b), s);
}
template <typename A, typename B> auto array_mul(A a, B b, Site s) {
    return array_binary<A, B, ArrayOp::Mul>(std::move(a), std::move(b), s);
}
template <typename A, typename B> auto array_div(A a, B b, Site s) {
    return array_binary<A, B, ArrayOp::Div>(std::move(a), std::move(b), s);
}
inline Array<double> array_matmul(const Array<double>& a, const Array<double>& b, Site site) {
    if (a.rank() != 2 || b.rank() != 2 || a.extent(1) != b.extent(0))
        array_error(site, "matmul requires (m,k) and (k,n) float arrays");
    auto result = Array<double>::full(std::tuple{a.extent(0), b.extent(1)}, 0.0, site);
    if (!result.size() || a.extent(1) == 0)
        return result;
    auto* output = result.writable(site);
    const auto* left = a.data();
    const auto* right = b.data();
    const auto m = a.extent(0), k = a.extent(1), n = b.extent(1);
    // Independent columns permit host SIMD without reassociating each dot
    // product, enabling fast-math, changing k order, or weakening bounds safety.
    for (Int i = 0; i < m; ++i)
        for (Int inner = 0; inner < k; ++inner) {
            const double value = left[i * a.stride(0) + inner * a.stride(1)];
            auto* row = output + i * n;
            const auto* source = right + inner * b.stride(0);
            if (b.stride(1) == 1)
                for (Int j = 0; j < n; ++j)
                    row[j] += value * source[j];
            else
                for (Int j = 0; j < n; ++j)
                    row[j] += value * source[j * b.stride(1)];
        }
    return result;
}
inline Array<double> array_affine(Array<double> points, const Array<double>& matrix, Site site) {
    if (points.rank() != 2 || points.extent(1) != 3 || matrix.rank() != 2 ||
        matrix.extent(0) != 4 || matrix.extent(1) != 4)
        array_error(site, "affine transform requires (n,3) points and a (4,4) matrix");
    for (Int column = 0; column < 4; ++column)
        if (matrix.get(std::tuple{Int{3}, column}, site) != (column == 3 ? 1.0 : 0.0))
            array_error(site, "affine transform does not accept a projective matrix");
    std::array<double, 12> coefficients{};
    for (Int row = 0; row < 3; ++row)
        for (Int column = 0; column < 4; ++column)
            coefficients[static_cast<std::size_t>(row * 4 + column)] =
                matrix.get(std::tuple{row, column}, site);
    const auto rows = points.extent(0), row_stride = points.stride(0),
               axis_stride = points.stride(1);
    const auto* input = points.data();
    const bool reuse = points.reusable();
    auto result =
        reuse ? std::move(points) : Array<double>::from_layout(points.layout(), 0.0, site);
    auto* output = result.writable(site);
    for (Int row = 0; row < rows; ++row) {
        const double x = input[row * row_stride], y = input[row * row_stride + axis_stride],
                     z = input[row * row_stride + 2 * axis_stride];
        for (Int axis = 0; axis < 3; ++axis) {
            const auto start = static_cast<std::size_t>(axis * 4);
            double value = 0;
            value += x * coefficients[start];
            value += y * coefficients[start + 1];
            value += z * coefficients[start + 2];
            output[row * 3 + axis] = value + coefficients[start + 3];
        }
    }
    return result;
}
inline double math_sqrt(double value, Site site) {
    if (value < 0 || !std::isfinite(value))
        error(site, "R4027", "sqrt requires a finite nonnegative value");
    return std::sqrt(value);
}
inline double math_sin(double value, Site site) {
    if (!std::isfinite(value))
        error(site, "R4027", "sin requires a finite value");
    return std::sin(value);
}
inline double math_cos(double value, Site site) {
    if (!std::isfinite(value))
        error(site, "R4027", "cos requires a finite value");
    return std::cos(value);
}
inline bool math_finite(double value) {
    return std::isfinite(value);
}
} // namespace wd
#endif
