export module sr.math;

// MSVC의 표준 라이브러리 named module을 사용한다. 이 인터페이스를 소비하는
// 번역 단위는 각 <concepts>/<ranges> 헤더를 반복 전처리하지 않는다.
import std;

export namespace sr::math
{
    // 실수 전용 수학 알고리즘이 정수 나눗셈으로 조용히 바뀌는 것을 막는다.
    template<class T>
    concept FloatingScalar = std::floating_point<std::remove_cvref_t<T>>;

    // SRMath 벡터뿐 아니라 동일 계약(value_type, dimension, operator[])을
    // 만족하는 타입도 받을 수 있는 구조적 concept이다.
    template<class T>
    concept VectorLike = requires(const std::remove_reference_t<T>& value, std::size_t index)
    {
        typename std::remove_reference_t<T>::value_type;
        { std::remove_reference_t<T>::dimension } -> std::convertible_to<std::size_t>;
        { value[index] } -> std::convertible_to<typename std::remove_reference_t<T>::value_type>;
    };

    // 행렬은 column-major 이중 인덱싱 계약까지 컴파일 시 확인한다.
    template<class T>
    concept MatrixLike = requires(const std::remove_reference_t<T>& value, std::size_t row, std::size_t column)
    {
        typename std::remove_reference_t<T>::value_type;
        { std::remove_reference_t<T>::dimension } -> std::convertible_to<std::size_t>;
        { value[column][row] } -> std::convertible_to<typename std::remove_reference_t<T>::value_type>;
    };

    template<class T>
    concept FixedPointLike = requires(T value)
    {
        { value.toFloat() } -> std::convertible_to<float>;
        { value.toDouble() } -> std::convertible_to<double>;
    };

    // span/vector/array처럼 연속 메모리인 버퍼만 SIMD 경로에 허용한다.
    template<class Range, class Value>
    concept ContiguousRangeOf = std::ranges::contiguous_range<Range>
        && std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, Value>;

    template<class Range>
    concept IndexRange = std::ranges::contiguous_range<Range>
        && std::unsigned_integral<std::remove_cv_t<std::ranges::range_value_t<Range>>>;
}
