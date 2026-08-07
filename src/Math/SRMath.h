#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <expected>
#include <type_traits>
#include <xmmintrin.h> // SSE Header
#include <smmintrin.h> // SSE4.1
#include <cstdint>

namespace SRMath {

	enum class MatrixError
	{
		Singular
	};

	// 고정소수점 비트 수는 int32_t 안에 들어와야 한다. 오래된 템플릿은
	// 잘못된 FRACT_BIT도 본문 깊숙이 들어간 뒤 실패했지만 requires는 호출
	// 지점에서 제약 위반을 설명한다.
	template <std::size_t FRACT_BIT>
		requires (FRACT_BIT > 0 && FRACT_BIT < 31)
	struct FixedPoint
	{
		static constexpr std::int64_t scale = std::int64_t{ 1 } << FRACT_BIT;
		std::int32_t value = 0;

		constexpr FixedPoint() noexcept = default;
		explicit constexpr FixedPoint(std::int32_t rawValue) noexcept : value(rawValue) {}
		explicit constexpr FixedPoint(float input) noexcept
			: value(static_cast<std::int32_t>(input * static_cast<float>(scale))) {}
		explicit constexpr FixedPoint(double input) noexcept
			: value(static_cast<std::int32_t>(input * static_cast<double>(scale))) {}

		[[nodiscard]] constexpr float toFloat() const noexcept
		{
			return static_cast<float>(value) / static_cast<float>(scale);
		}

		[[nodiscard]] constexpr double toDouble() const noexcept
		{
			return static_cast<double>(value) / static_cast<double>(scale);
		}

		constexpr FixedPoint& operator+=(const FixedPoint& other) noexcept
		{
			value += other.value;
			return *this;
		}

		constexpr FixedPoint& operator-=(const FixedPoint& other) noexcept
		{
			value -= other.value;
			return *this;
		}

		constexpr FixedPoint& operator*=(const FixedPoint& other) noexcept
		{
			const std::int64_t product = static_cast<std::int64_t>(value) * other.value;
			value = static_cast<std::int32_t>(product >> FRACT_BIT);
			return *this; // 이전 구현에서 빠졌던 반환값을 복구한다.
		}

		constexpr FixedPoint& operator/=(const FixedPoint& other) noexcept
		{
			const std::int64_t numerator = static_cast<std::int64_t>(value) << FRACT_BIT;
			value = static_cast<std::int32_t>(numerator / other.value);
			return *this;
		}

		constexpr FixedPoint& operator*=(float scalar) noexcept
		{
			value = static_cast<std::int32_t>(static_cast<float>(value) * scalar);
			return *this;
		}

		constexpr FixedPoint& operator/=(float scalar) noexcept
		{
			value = static_cast<std::int32_t>(static_cast<float>(value) / scalar);
			return *this;
		}
	};

	template <std::size_t fractBits>
	[[nodiscard]] constexpr FixedPoint<fractBits> floatToFixed(float value) noexcept {
		return FixedPoint<fractBits>{ value };
	}

	template <std::size_t fractBits>
	[[nodiscard]] constexpr float fixedToFloat(FixedPoint<fractBits> value) noexcept
	{
		return value.toFloat();
	}

	using Fixed8 = FixedPoint<8>;
	using Fixed16 = FixedPoint<16>;
	using Fixed24 = FixedPoint<24>;
	using Fixed30 = FixedPoint<30>;

	template <std::size_t FRACT_BIT>
	[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator+(
		FixedPoint<FRACT_BIT> lhs, const FixedPoint<FRACT_BIT>& rhs) noexcept {
		lhs += rhs;
		return lhs;
	}

	template <std::size_t FRACT_BIT>
	[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator-(
		FixedPoint<FRACT_BIT> lhs, const FixedPoint<FRACT_BIT>& rhs) noexcept {
		lhs -= rhs;
		return lhs;
	}

	template <std::size_t FRACT_BIT>
	[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator*(
		FixedPoint<FRACT_BIT> lhs, const FixedPoint<FRACT_BIT>& rhs) noexcept {
		lhs *= rhs;
		return lhs;
	}

	template <std::size_t FRACT_BIT>
	[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator*(
		FixedPoint<FRACT_BIT> lhs, float scalar) noexcept {
		lhs *= scalar;
		return lhs;
	}

	template <std::size_t FRACT_BIT>
	[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator/(
		FixedPoint<FRACT_BIT> lhs, const FixedPoint<FRACT_BIT>& rhs) noexcept {
		lhs /= rhs;
		return lhs;
	}

	template <std::size_t FRACT_BIT>
	[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator/(
		FixedPoint<FRACT_BIT> lhs, float scalar) noexcept {
		lhs /= scalar;
		return lhs;
	}

	// 지원하지 않는 차원은 템플릿 본문 안의 오류가 아니라 호출 지점에서
	// constraint failure로 진단된다(C++20 concept/requires).
	template <std::size_t N>
	concept SupportedVectorDimension = N >= 2 && N <= 4;

	template <std::size_t N>
		requires SupportedVectorDimension<N>
	struct alignas(16) Vector;

	using vec4 = Vector<4>;
	using vec3 = Vector<3>;
	using vec2 = Vector<2>;
	using Color = Vector<3>; // RGB Color
	using Color4 = Vector<4>; // RGBA Color

	// Vector storage is the intentional exception to the std::array migration:
	// these C arrays are ABI overlays for MSVC's __m128 intrinsic and named x/y/z/w
	// access, not general-purpose collections. Changing this layout would add
	// loads/stores to every hot math operation.
#ifdef _MSC_VER
	// MSVC exposes convenient anonymous component structs over __m128 but warns
	// about that intrinsic-specific extension at /W4. Keep the suppression local
	// to the three ABI overlay definitions; application code remains /W4-clean.
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
	// Vector2
	template <>
	struct alignas(16) Vector<2>
	{
		using value_type = float;
		static constexpr size_t dimension = 2;

		union {
			struct { float x, y; };
			struct { float u, v; }; // For texture coordinates;
			float data[4]; // padding 8 bytes for m128
			__m128 m128;
		};

		Vector() noexcept : m128(_mm_setzero_ps()) {}
		explicit Vector(const __m128 v) noexcept : m128(v) {}
		explicit Vector(float x) noexcept : m128(_mm_set_ps(0.0f, 0.f, x, x)) {}
		Vector(float x, float y) noexcept : m128(_mm_set_ps(0.0f, 0.0f, y, x)) {}

		float& operator[](std::size_t index) noexcept { return data[index]; }
		const float& operator[](std::size_t index) const noexcept { return data[index]; }

		Vector<2>& operator+=(const Vector<2>& other) {
			// SIMD
			this->m128 = _mm_add_ps(this->m128, other.m128);
			return *this;
		}

		Vector<2>& operator-=(const Vector<2>& other) {
			this->m128 = _mm_sub_ps(this->m128, other.m128);
			return *this;
		}

		Vector<2>& operator*=(float scalar) {
			__m128 s = _mm_set1_ps(scalar);
			this->m128 = _mm_mul_ps(this->m128, s);
			return *this;
		}

		Vector<2>& operator*=(const Vector<2>& other) {
			this->m128 = _mm_mul_ps(this->m128, other.m128);
			return *this;
		}

		Vector<2>& operator/=(float scalar) noexcept {
			const float reciprocalScalar = 1.0f / scalar;
			const __m128 reciprocal = _mm_set1_ps(reciprocalScalar);
			this->m128 = _mm_mul_ps(this->m128, reciprocal);
			return *this;
		}

		[[nodiscard]] bool operator==(const Vector<2>& other) const noexcept
		{
			// Compare each component of the vectors
			__m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

			// Check if all components are equal
			return _mm_movemask_ps(cmp) == 0x3; // All bits set means all components are equal
		}

		[[nodiscard]] bool operator!=(const Vector<2>& other) const noexcept
		{
			return !(*this == other);
		}

		Vector<2>& clamp(float min, float max) noexcept
		{
			const __m128 min_vec = _mm_set1_ps(min);
			const __m128 max_vec = _mm_set1_ps(max);

			this->m128 = _mm_max_ps(min_vec, _mm_min_ps(max_vec, this->m128));

			return *this;
		}
	};

	// Vector3
	template <>
	struct alignas(16) Vector<3>
	{
		using value_type = float;
		static constexpr size_t dimension = 3;

		union {
			struct { float x, y, z; };
			struct { float r, g, b; }; // For RGB Color
			float data[4]; // padding 4 bytes for m128
			__m128 m128;
		};

		Vector() noexcept : m128(_mm_setzero_ps()) {}
		explicit Vector(const __m128 v) noexcept : m128(v) {}
		explicit Vector(float x) noexcept : m128(_mm_set_ps(0.0f, x, x, x)) {}
		Vector(float x, float y, float z) noexcept : m128(_mm_set_ps(0.0f, z, y, x)) {}
		explicit Vector(const SRMath::Vector<2>& v) noexcept : m128(_mm_set_ps(0.0f, 0.0f, v.y, v.x)) {}
		Vector(const SRMath::Vector<2>& v, float z) noexcept : m128(_mm_set_ps(0.0f, z, v.y, v.x)) {}
		Vector(const SRMath::Vector<3>& v) noexcept : m128(_mm_set_ps(0.0f, v.z, v.y, v.x)) {}
		explicit Vector(const SRMath::Vector<4>& v) noexcept;

		float& operator[](std::size_t index) noexcept { return data[index]; }
		const float& operator[](std::size_t index) const noexcept { return data[index]; }

		Vector& operator+=(const Vector<3>& other) {
			// SIMD
			this->m128 = _mm_add_ps(this->m128, other.m128);
			return *this;
		}

		Vector& operator-=(const Vector<3>& other) {
			this->m128 = _mm_sub_ps(this->m128, other.m128);
			return *this;
		}

		Vector& operator*=(float scalar) {
			__m128 s = _mm_set1_ps(scalar);
			this->m128 = _mm_mul_ps(this->m128, s);
			return *this;
		}

		Vector& operator*=(const Vector<3>& other) {
			this->m128 = _mm_mul_ps(this->m128, other.m128);
			return *this;
		}

		Vector& operator/=(float scalar) noexcept {
			const float reciprocalScalar = 1.0f / scalar;
			const __m128 reciprocal = _mm_set1_ps(reciprocalScalar);
			this->m128 = _mm_mul_ps(this->m128, reciprocal);
			return *this;
		}

		[[nodiscard]] bool operator==(const Vector& other) const noexcept
		{
			// Compare each component of the vectors
			__m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

			// Check if all components are equal
			return _mm_movemask_ps(cmp) == 0x7; // All bits set means all components are equal
		}

		[[nodiscard]] bool operator!=(const Vector& other) const noexcept
		{
			return !(*this == other);
		}

		Vector& clamp(float min, float max) noexcept
		{
			const __m128 min_vec = _mm_set1_ps(min);
			const __m128 max_vec = _mm_set1_ps(max);

			this->m128 = _mm_max_ps(min_vec, _mm_min_ps(max_vec, this->m128));

			return *this;
		}
	};

	// Vector4
	template <>
	struct alignas(16) Vector<4>
	{
		using value_type = float;
		static constexpr size_t dimension = 4;

		union {
			struct { float x, y, z, w; };
			struct { float r, g, b, a; }; // For RGBA Color
			float data[4];
			__m128 m128;
		};

		Vector() noexcept : m128(_mm_setzero_ps()) {}
		explicit Vector(const __m128 v) noexcept : m128(v) {}
		explicit Vector(float x) noexcept : m128(_mm_set_ps(x, x, x, x)) {}
		Vector(float x, float y, float z, float w) noexcept : m128(_mm_set_ps(w, z, y, x)) {}
		Vector(const SRMath::Vector<2>& v, float w) noexcept : m128(_mm_set_ps(w, 0.f, v.y, v.x)) {}
		explicit Vector(const SRMath::Vector<2>& v) noexcept : m128(_mm_set_ps(1.0f, 0.f, v.y, v.x)) {}
		Vector(const SRMath::Vector<3>& v, float w) noexcept : m128(_mm_set_ps(w, v.z, v.y, v.x)) {}
		explicit Vector(const SRMath::Vector<3>& v) noexcept : m128(_mm_set_ps(1.0f, v.z, v.y, v.x)) {}

		float& operator[](std::size_t index) noexcept { return data[index]; }
		const float& operator[](std::size_t index) const noexcept { return data[index]; }

		Vector<4>& operator+=(const Vector<4>& other) {
			// SIMD
			this->m128 = _mm_add_ps(this->m128, other.m128);
			return *this;
		}

		Vector<4>& operator-=(const Vector<4>& other) {
			this->m128 = _mm_sub_ps(this->m128, other.m128);
			return *this;
		}

		Vector<4>& operator*=(float scalar) {
			__m128 s = _mm_set1_ps(scalar);
			this->m128 = _mm_mul_ps(this->m128, s);
			return *this;
		}

		Vector<4>& operator*=(const Vector<4>& other) {
			this->m128 = _mm_mul_ps(this->m128, other.m128);
			return *this;
		}

		Vector<4>& operator/=(float scalar) noexcept {
			const float reciprocalScalar = 1.0f / scalar;
			const __m128 reciprocal = _mm_set1_ps(reciprocalScalar);
			this->m128 = _mm_mul_ps(this->m128, reciprocal);
			return *this;
		}

		[[nodiscard]] bool operator==(const Vector<4>& other) const noexcept
		{
			// Compare each component of the vectors
			__m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

			// Check if all components are equal
			return _mm_movemask_ps(cmp) == 0xF; // All bits set means all components are equal
		}

		[[nodiscard]] bool operator!=(const Vector<4>& other) const noexcept
		{
			return !(*this == other);
		}

		Vector<4>& clamp(float min, float max) noexcept
		{
			const __m128 min_vec = _mm_set1_ps(min);
			const __m128 max_vec = _mm_set1_ps(max);

			this->m128 = _mm_max_ps(min_vec, _mm_min_ps(max_vec, this->m128));

			return *this;
		}
	};

	inline Vector<3>::Vector(const Vector<4>& v) noexcept
		: m128(_mm_set_ps(0.0f, v.z, v.y, v.x)) // w는 0으로 설정
	{ }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	// inline operator overloading function
	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> operator+(Vector<N> lhs, const Vector<N>& rhs) noexcept
	{
		lhs += rhs;
		return lhs;
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> operator-(Vector<N> lhs, const Vector<N>& rhs) noexcept
	{
		lhs -= rhs;
		return lhs;
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> operator*(Vector<N> value, float scalar) noexcept
	{
		value *= scalar;
		return value;
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> operator*(float scalar, Vector<N> value) noexcept
	{
		value *= scalar;
		return value;
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> operator*(Vector<N> lhs, const Vector<N>& rhs) noexcept
	{
		lhs *= rhs;
		return lhs;
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> operator/(Vector<N> value, float scalar) noexcept
	{
		value /= scalar;
		return value;
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline bool operator==(const Vector<N>& lhs, const Vector<N>& rhs) noexcept
	{
		return lhs.operator==(rhs);
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline bool operator!=(const Vector<N>& lhs, const Vector<N>& rhs) noexcept
	{
		return !lhs.operator==(rhs);
	}

	// -- Matrix 선언 및 정의
	template <std::size_t N>
	concept SupportedMatrixDimension = N == 3 || N == 4;

	template <std::size_t N>
		requires SupportedMatrixDimension<N>
	struct alignas(16) Matrix;
	using mat3 = Matrix<3>;
	using mat4 = Matrix<4>;

	template<std::size_t N>
		requires SupportedMatrixDimension<N>
	struct alignas(16) Matrix
	{
		using value_type = float;
		static constexpr std::size_t dimension = N;

		// cols/data are the same SIMD-friendly column-major storage viewed in two
		// ways. As with Vector, this is an intrinsic ABI boundary rather than an
		// ordinary collection; algorithmic scratch space uses std::array below.
		union {
			Vector<N> cols[N]; // __m128 is included in Vec4
			float data[N * N];
		};

		Matrix() = default;
		explicit Matrix(float diagonal) noexcept
		{
			for (std::size_t i = 0; i < N; ++i)
			{
				for (std::size_t j = 0; j < N; ++j)
					cols[i][j] = (i == j) ? diagonal : 0.0f;
			}
		}

		[[nodiscard]] static Matrix<N> identity() noexcept
		{
			return Matrix<N>(1.f);
		}

		Vector<N>& operator[](std::size_t index) noexcept { return cols[index]; }
		const Vector<N>& operator[](std::size_t index) const noexcept { return cols[index]; }
	};


	template <typename T>
	concept Matrix4Like = std::same_as<std::remove_cvref_t<T>, Matrix<4>>;

	// C++20 concept는 기존 생성자 안의 static_assert보다 먼저, 정확한 호출
	// 위치에서 "4x4 행렬이 아님"을 진단한다.
	template<Matrix4Like T1, Matrix4Like T2>
	class Matrix4x4Proxy
	{
	private:
		const T1& m_lhs; // left-hand side
		const T2& m_rhs; // right-hand side
	public:
		Matrix4x4Proxy(const T1& lhs, const T2& rhs) noexcept : m_lhs(lhs), m_rhs(rhs) {}

		// 행렬 곱셈 연산자 오버로딩
		[[nodiscard]] vec4 operator[](std::size_t col_idx) const noexcept
		{
			// m_lhs * v
			return m_lhs * m_rhs[col_idx];
		}

		[[nodiscard]] const T1& lhs() const noexcept { return m_lhs; }
		[[nodiscard]] const T2& rhs() const noexcept { return m_rhs; }
	};

	[[nodiscard]] inline Matrix4x4Proxy<mat4, mat4> operator*(const mat4& lhs, const mat4& rhs) noexcept
	{
		return Matrix4x4Proxy<mat4, mat4>(lhs, rhs);
	}

	template<>
	// SSE requires 16-byte alignment. The previous 64-byte alignment padded
	// every containing render command/GameObject without helping AVX dispatch,
	// which combines registers rather than loading a 32-byte mat4 block.
	struct alignas(16) Matrix<4>
	{
		using value_type = float;
		static constexpr std::size_t dimension = 4;

		union {
			__m128 m128[4];
			Vector<4> cols[4];
			float data[4 * 4];
		};

		// 기본 생성자를 명시적으로 정의
		Matrix() noexcept : Matrix(1.f) {}

		// 대각선 값을 받는 생성자도 활성화
		explicit Matrix(float diagonal) noexcept
		{
			// _mm_set_ps는 인자를 역순으로 배치하므로 각 열의 대각 성분 위치를
			// 명시한다. 0이면 zero matrix, 1이면 identity가 된다.
			m128[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, diagonal); // col 0
			m128[1] = _mm_set_ps(0.0f, 0.0f, diagonal, 0.0f); // col 1
			m128[2] = _mm_set_ps(0.0f, diagonal, 0.0f, 0.0f); // col 2
			m128[3] = _mm_set_ps(diagonal, 0.0f, 0.0f, 0.0f); // col 3
		}

		[[nodiscard]] static Matrix<4> identity() noexcept
		{
			return Matrix<4>(1.f);
		}

		Vector<4>& operator[](std::size_t index) noexcept { return cols[index]; }
		const Vector<4>& operator[](std::size_t index) const noexcept { return cols[index]; }

		template<Matrix4Like T1, Matrix4Like T2>
		Matrix(const Matrix4x4Proxy<T1, T2>& proxy) noexcept
		{
			// 내부 로직은 operator=와 완전히 동일합니다.
			// 프록시로부터 각 열의 계산 결과를 가져와 새 행렬을 초기화합니다.
			for (int i = 0; i < 4; i++) this->m128[i] = _mm_setzero_ps();
			multiply(*this, proxy.lhs(), proxy.rhs());
		}

		template<Matrix4Like T1, Matrix4Like T2>
		mat4& operator=(const Matrix4x4Proxy<T1, T2>& proxy) noexcept
		{
			multiply(*this, proxy.lhs(), proxy.rhs());
			return *this; // return this matrix
		}

	};

	template<>
	struct Matrix<3> { // 16바이트 정렬이 필수는 아님
		using value_type = float;
		static constexpr std::size_t dimension = 3;

		union {
			Vector<3> cols[3];
			float data[3 * 3];
		};

		Vector<3>& operator[](std::size_t index) noexcept { return cols[index]; }
		const Vector<3>& operator[](std::size_t index) const noexcept { return cols[index]; }
	};

	// Column-major matrix/vector product. Each matrix column is multiplied by
	// one broadcast vector component, so SSE maps directly to the formula.
	[[nodiscard]] inline vec4 operator*(const mat4& m, const vec4& v) noexcept
	{
		const __m128 vx = _mm_set1_ps(v.x);
		const __m128 vy = _mm_set1_ps(v.y);
		const __m128 vz = _mm_set1_ps(v.z);
		const __m128 vw = _mm_set1_ps(v.w);

		__m128 res = _mm_mul_ps(m.cols[0].m128, vx);
		res = _mm_add_ps(res, _mm_mul_ps(m.cols[1].m128, vy));
		res = _mm_add_ps(res, _mm_mul_ps(m.cols[2].m128, vz));
		res = _mm_add_ps(res, _mm_mul_ps(m.cols[3].m128, vw));

		vec4 ret;
		ret.m128 = res;
		return ret;
	}

	[[nodiscard]] inline vec4 operator*(const mat4& m, const vec3& v) noexcept
	{
		Vector<4> newV(v);
		return m * newV;
	}

	inline void multiply(mat4& result, const mat4& a, const mat4& b) noexcept
	{
		// A의 열들을 미리 SIMD 레지스터에 로드합니다.
		// 이렇게 하면 루프마다 메모리에서 읽어오는 것을 방지할 수 있습니다.
		__m128 a_col0 = a.m128[0];
		__m128 a_col1 = a.m128[1];
		__m128 a_col2 = a.m128[2];
		__m128 a_col3 = a.m128[3];

		// --- 결과 행렬의 첫 번째 열 계산 ---
		__m128 b_splat = _mm_set1_ps(b.cols[0].x);
		__m128 res = _mm_mul_ps(a_col0, b_splat);

		b_splat = _mm_set1_ps(b.cols[0].y);
		res = _mm_add_ps(res, _mm_mul_ps(a_col1, b_splat));

		b_splat = _mm_set1_ps(b.cols[0].z);
		res = _mm_add_ps(res, _mm_mul_ps(a_col2, b_splat));

		b_splat = _mm_set1_ps(b.cols[0].w);
		res = _mm_add_ps(res, _mm_mul_ps(a_col3, b_splat));
		result.m128[0] = res;

		// --- 결과 행렬의 두 번째 열 계산 ---
		b_splat = _mm_set1_ps(b.cols[1].x);
		res = _mm_mul_ps(a_col0, b_splat);

		b_splat = _mm_set1_ps(b.cols[1].y);
		res = _mm_add_ps(res, _mm_mul_ps(a_col1, b_splat));

		b_splat = _mm_set1_ps(b.cols[1].z);
		res = _mm_add_ps(res, _mm_mul_ps(a_col2, b_splat));

		b_splat = _mm_set1_ps(b.cols[1].w);
		res = _mm_add_ps(res, _mm_mul_ps(a_col3, b_splat));
		result.m128[1] = res;

		// --- 결과 행렬의 세 번째 열 계산 ---
		b_splat = _mm_set1_ps(b.cols[2].x);
		res = _mm_mul_ps(a_col0, b_splat);

		b_splat = _mm_set1_ps(b.cols[2].y);
		res = _mm_add_ps(res, _mm_mul_ps(a_col1, b_splat));

		b_splat = _mm_set1_ps(b.cols[2].z);
		res = _mm_add_ps(res, _mm_mul_ps(a_col2, b_splat));

		b_splat = _mm_set1_ps(b.cols[2].w);
		res = _mm_add_ps(res, _mm_mul_ps(a_col3, b_splat));
		result.m128[2] = res;

		// --- 결과 행렬의 네 번째 열 계산 ---
		b_splat = _mm_set1_ps(b.cols[3].x);
		res = _mm_mul_ps(a_col0, b_splat);

		b_splat = _mm_set1_ps(b.cols[3].y);
		res = _mm_add_ps(res, _mm_mul_ps(a_col1, b_splat));

		b_splat = _mm_set1_ps(b.cols[3].z);
		res = _mm_add_ps(res, _mm_mul_ps(a_col2, b_splat));

		b_splat = _mm_set1_ps(b.cols[3].w);
		res = _mm_add_ps(res, _mm_mul_ps(a_col3, b_splat));
		result.m128[3] = res;
	}

	// inline utility functions (dot product, cross product)
	[[nodiscard]] inline float dot(const vec4& a, const vec4& b) noexcept
	{
		__m128 ret = _mm_dp_ps(a.m128, b.m128, 0xF1);

		return _mm_cvtss_f32(ret);
	}

	[[nodiscard]] inline float dot(const vec3& a, const vec3& b) noexcept
	{
		__m128 ret = _mm_dp_ps(a.m128, b.m128, 0x71);

		return _mm_cvtss_f32(ret);
	}

	[[nodiscard]] inline float dot(const vec2& a, const vec2& b) noexcept
	{
		__m128 ret = _mm_dp_ps(a.m128, b.m128, 0x31);

		return _mm_cvtss_f32(ret);
	}

	// Get Cross Product of Vector
	[[nodiscard]] inline vec3 cross(const vec3& a, const vec3& b) noexcept
	{
		const __m128 a_shuf1 =
			_mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 0, 2, 1));
		const __m128 b_shuf1 =
			_mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 1, 0, 2));

		const __m128 term1 = _mm_mul_ps(a_shuf1, b_shuf1);

		const __m128 a_shuf2 =
			_mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 1, 0, 2));
		const __m128 b_shuf2 =
			_mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 0, 2, 1));

		const __m128 term2 = _mm_mul_ps(a_shuf2, b_shuf2);

		vec3 ret;
		ret.m128 = _mm_sub_ps(term1, term2);

		return ret;
	}

	[[nodiscard]] inline vec3 cross(const vec2& a, const vec2& b) noexcept
	{
		vec3 newA, newB;
		newA.m128 = a.m128;
		newB.m128 = b.m128;

		return cross(newA, newB);
	}

	// Get power of Length of Vector to compare length with the other Vector
	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline float lengthSq(const Vector<N>& v) noexcept
	{
		return dot(v, v);
	}

	// Get Length of Vector
	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline float length(const Vector<N>& v) noexcept
	{
		return std::sqrt(lengthSq(v));
	}

	// Normalize Vector
	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> normalize(const Vector<N>& v) noexcept
	{
		const float vectorLength = length(v);
		if (vectorLength < 1e-5f) return v;

		return v / vectorLength;
	}

	// Identity
	[[nodiscard]] inline Matrix<4> identity() noexcept
	{
		return Matrix<4>(1.f);
	}

	// Translate Mat4
	[[nodiscard]] inline Matrix<4> translate(const Vector<3>& v) noexcept
	{
		Matrix<4> ret(1.0f);
		ret[3].m128 = _mm_set_ps(1.0f, v.z, v.y, v.x); // SIMD 최적화
		return ret;
	}

	// Scale Mat4
	[[nodiscard]] inline Matrix<4> scale(const Vector<3>& v) noexcept
	{
		Matrix<4> ret(1.0f);
		ret[0][0] = v.x;
		ret[1][1] = v.y;
		ret[2][2] = v.z;
		return ret;
	}

	// Rotate Mat4 (Right-handed Coordinate System)
	[[nodiscard]] inline Matrix<4> rotate(const vec3& rotationVector) noexcept
	{
		// 1. 벡터의 크기(length)를 회전 각도(angle)로 사용합니다.
		const float angleInRadians = length(rotationVector);

		// 2. 만약 회전 각도가 거의 0이면, 계산 없이 단위 행렬을 반환합니다 (최적화 및 0으로 나누기 방지).
		if (angleInRadians < 1e-6f)
		{
			return Matrix<4>(1.0f);
		}

		// 3. 벡터를 정규화하여 회전 축을 구합니다.
		const vec3 axis = rotationVector / angleInRadians;

		// 4. 기존의 로드리게스 회전 공식 코드를 그대로 사용합니다.
		const float c = std::cos(angleInRadians);
		const float s = std::sin(angleInRadians);
		const float t = 1.0f - c;

		Matrix<4> result(1.0f);

		// 오른손 좌표계 열 우선 회전 행렬
		result[0].m128 = _mm_set_ps(0.0f, axis.z * axis.x * t - axis.y * s,
			axis.y* axis.x* t + axis.z * s, c + axis.x * axis.x * t); // SIMD 최적화

		result[1].m128 = _mm_set_ps(0.0f, axis.z * axis.y * t + axis.x * s,
			c + axis.y * axis.y * t, axis.x * axis.y * t - axis.z * s); // SIMD 최적화

		result[2].m128 = _mm_set_ps(0.0f, c + axis.z * axis.z * t,
			axis.y * axis.z * t - axis.x * s, axis.x * axis.z * t + axis.y * s); // SIMD 최적화

		result[3].m128 = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f); // 마지막 행은 단위 행렬의 마지막 행

		return result;
	}

	// perspective (Right-handed Coordinate System)
	[[nodiscard]] inline Matrix<4> perspective(
		float angleInRadians, float aspectRatio, float zNear, float zFar) noexcept
	{
		Matrix<4> result(0.0f);

		const float f = 1.0f / std::tan(angleInRadians / 2.0f);

		result[0][0] = f / aspectRatio;
		result[1][1] = f;
		result[2][2] = (zFar + zNear) / (zNear - zFar);
		result[2][3] = -1.0f;
		result[3][2] = (2.0f * zFar * zNear) / (zNear - zFar);

		return result;
	}

	[[nodiscard]] inline mat4 lookAt(const vec3& eye, const vec3& center, const vec3& up) noexcept
	{
		const vec3 f = normalize(center - eye);
		const vec3 s = normalize(cross(f, up));
		const vec3 u = cross(s, f);

		mat4 result(1.0f);
		result[0].m128 = _mm_set_ps(0.0f, -f.x, u.x, s.x); // SIMD 최적화
		result[1].m128 = _mm_set_ps(0.0f, -f.y, u.y, s.y); // SIMD 최적화
		result[2].m128 = _mm_set_ps(0.0f, -f.z, u.z, s.z); // SIMD 최적화
		result[3].m128 = _mm_set_ps(1.0f, dot(f, eye), -dot(u, eye), -dot(s, eye));

		return result;
	}

	// SIMD를 사용한 4x4 행렬 전치 함수
	[[nodiscard]] inline mat4 transpose(const mat4& m) noexcept {
		mat4 result;

		// 임시 변수로 두 열씩 묶어 처리
		const __m128 tmp0 = _mm_unpacklo_ps(m.m128[0], m.m128[1]); // {x0, x1, y0, y1}
		const __m128 tmp1 = _mm_unpackhi_ps(m.m128[0], m.m128[1]); // {z0, z1, w0, w1}
		const __m128 tmp2 = _mm_unpacklo_ps(m.m128[2], m.m128[3]); // {x2, x3, y2, y3}
		const __m128 tmp3 = _mm_unpackhi_ps(m.m128[2], m.m128[3]); // {z2, z3, w2, w3}

		// 임시 변수들을 다시 섞어서 최종 행(row)을 만듭니다.
		// 이 행들이 결과 행렬의 새로운 열(column)이 됩니다.
		result.m128[0] = _mm_movelh_ps(tmp0, tmp2); // {x0, x1, x2, x3}
		result.m128[1] = _mm_movehl_ps(tmp2, tmp0); // {y0, y1, y2, y3}
		result.m128[2] = _mm_movelh_ps(tmp1, tmp3); // {z0, z1, z2, z3}
		result.m128[3] = _mm_movehl_ps(tmp3, tmp1); // {w0, w1, w2, w3}

		return result;
	}

	// SIMD를 사용한 4x4 행렬 역행렬 함수
	// 행렬식이 0에 가까우면 예외/임의 identity 대신 구조화된 오류를 반환한다.
	[[nodiscard]] inline std::expected<mat4, MatrixError> inverse(const mat4& m) noexcept {
		// Gauss-Jordan augmented rows. Fixed extents belong in the type; unlike
		// the ABI unions above these are ordinary algorithmic scratch buffers.
		std::array<__m128, 4> rowL{};
		std::array<__m128, 4> rowR{};

		// Build rows from column-major mat4: element (r,c) == m[c][r]
		for (int r = 0; r < 4; ++r) {
			// _mm_set_ps sets (w,z,y,x) -> elements [3]=w ... [0]=x
			rowL[r] = _mm_set_ps(m[3][r], m[2][r], m[1][r], m[0][r]);
			// identity: row r has 1.0 at column r
			rowR[r] = _mm_set_ps((r == 3) ? 1.0f : 0.0f,
				(r == 2) ? 1.0f : 0.0f,
				(r == 1) ? 1.0f : 0.0f,
				(r == 0) ? 1.0f : 0.0f);
		}

		constexpr float singular_epsilon = 1e-12f;
		std::array<float, 4> components{};

		for (int col = 0; col < 4; ++col) {
			// --- pivot selection (partial pivot) ---
			int pivot_row = col;
			float max_abs = 0.0f;
			for (int r = col; r < 4; ++r) {
				_mm_storeu_ps(components.data(), rowL[r]);
				const float val = components[col];
				const float aval = std::fabs(val);
				if (aval > max_abs) { max_abs = aval; pivot_row = r; }
			}

			if (max_abs < singular_epsilon) {
				// Singular or nearly singular
				return std::unexpected(MatrixError::Singular);
			}

			// swap rows if pivot_row != col
			if (pivot_row != col) {
				std::swap(rowL[col], rowL[pivot_row]);
				std::swap(rowR[col], rowR[pivot_row]);
			}

			// --- normalize pivot row: divide entire row by pivot element ---
			_mm_storeu_ps(components.data(), rowL[col]);
			const float pivot = components[col];
			const float inv_pivot = 1.0f / pivot;
			const __m128 inv_pivot_v = _mm_set1_ps(inv_pivot);

			rowL[col] = _mm_mul_ps(rowL[col], inv_pivot_v);
			rowR[col] = _mm_mul_ps(rowR[col], inv_pivot_v);

			// After normalization, pivot element is (ideally) 1.0

			// --- eliminate column in other rows ---
			for (int r = 0; r < 4; ++r) {
				if (r == col) continue;
				_mm_storeu_ps(components.data(), rowL[r]);
				const float factor = components[col];
				if (factor == 0.0f) continue;
				const __m128 factor_v = _mm_set1_ps(factor);

				// row_r = row_r - factor * row_col
				rowL[r] = _mm_sub_ps(rowL[r], _mm_mul_ps(factor_v, rowL[col]));
				rowR[r] = _mm_sub_ps(rowR[r], _mm_mul_ps(factor_v, rowR[col]));
			}
		}

		// Build resulting inverse matrix (column-major).
		// result[c][r] = rowR[r] element at column c
		mat4 result(0.0f);
		for (int r = 0; r < 4; ++r) {
			_mm_storeu_ps(components.data(), rowR[r]);
			for (int c = 0; c < 4; ++c) {
				result[c][r] = components[c];
			}
		}

		return result;
	}

	[[nodiscard]] inline std::expected<mat4, MatrixError> inverse_transpose(const mat4& m) noexcept {

		if (auto inverse_mat = inverse(m)) {
			return transpose(*inverse_mat);
		}
		return std::unexpected(MatrixError::Singular);
	}

	[[nodiscard]] inline vec3 reflect(const vec3& incident, const vec3& normal) noexcept
	{
		return incident - normal * (2.0f * dot(normal, incident));
	}

	template<std::size_t N>
		requires SupportedVectorDimension<N>
	[[nodiscard]] inline Vector<N> clamp(Vector<N> value, float minimum, float maximum) noexcept
	{
		value.clamp(minimum, maximum);
		return value;
	}
}
