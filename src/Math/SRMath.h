#pragma once

#include <cmath>
#include <optional>
#include <xmmintrin.h> // SSE Header
#include <smmintrin.h> // SSE4.1

constexpr float PI = 3.1415926535f;
const float angle90 = PI / 2.0f;


namespace SRMath {
	// General Template

	template <size_t N>	struct alignas(16) Vector;
	
	using vec4 = Vector<4>;
	using vec3 = Vector<3>;
	using vec2 = Vector<2>;

	// Vector2
	template <>
	struct alignas(16) Vector<2>
	{
		union {
			struct { float x, y; };
			float data[4]; // padding 8 bytes for m128
			__m128 m128;
		};

		Vector() : m128(_mm_setzero_ps()) {}
		Vector(const __m128 v) : m128(v) {}
		Vector(float x) : m128(_mm_set_ps(0.0f, 0.f, x, x)) {}
		Vector(float x, float y) : m128(_mm_set_ps(0.0f, 0.0f, y, x)) {}

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }

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

		Vector<2>& operator/=(float scalar) {
			float rcp_scalar = 1.0f / scalar;
			__m128 r = _mm_set1_ps(rcp_scalar);
			this->m128 = _mm_mul_ps(this->m128, r);
			return *this;
		}

		bool operator==(const Vector<2>& other)
		{
			// Compare each component of the vectors
			__m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

			// Check if all components are equal
			return _mm_movemask_ps(cmp) == 0x3; // All bits set means all components are equal
		}

		bool operator!=(const Vector<2>& other)
		{
			return *this == other ? false : true; // All bits set means all components are equal
		}

		Vector<2> clamp(float min, float max)
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
		union {
			struct { float x, y, z; };
			float data[4]; // padding 4 bytes for m128
			__m128 m128;
		};

		Vector() : m128(_mm_setzero_ps()) {}
		Vector(const __m128 v) : m128(v) {}
		Vector(float x) : m128(_mm_set_ps(0.0f, x, x, x)) {}
		Vector(float x, float y, float z) : m128(_mm_set_ps(0.0f, z, y, x)) {}
		Vector(const SRMath::Vector<2>& v) : m128(_mm_set_ps(0.0f, 0.0f, v.y, v.x)) {}
		Vector(const SRMath::Vector<2>& v, float z) : m128(_mm_set_ps(0.0f, z, v.y, v.x)) {}
		Vector(const SRMath::Vector<3>& v) : m128(_mm_set_ps(0.0f, v.z, v.y, v.x)) {}
		Vector(const SRMath::Vector<4>& v);

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }

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

		Vector& operator/=(float scalar) {
			float rcp_scalar = 1.0f / scalar;
			__m128 r = _mm_set1_ps(rcp_scalar);
			this->m128 = _mm_mul_ps(this->m128, r);
			return *this;
		}

		bool operator==(const Vector& other)
		{
			// Compare each component of the vectors
			__m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

			// Check if all components are equal
			return _mm_movemask_ps(cmp) == 0x7; // All bits set means all components are equal
		}

		bool operator!=(const Vector& other)
		{
			return *this == other ? false : true; // All bits set means all components are equal
		}

		Vector clamp(float min, float max)
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
		union {
			struct { float x, y, z, w; };
			float data[4];
			__m128 m128;
		};

		Vector() : m128(_mm_setzero_ps()) {}
		Vector(const __m128 v) : m128(v) {}
		Vector(float x) : m128(_mm_set_ps(x, x, x, x)) {}
		Vector(float x, float y, float z, float w) : m128(_mm_set_ps(w, z, y, x)) {}
		Vector(const SRMath::Vector<2>& v, float w) : m128(_mm_set_ps(w, 0.f, v.y, v.x)) {}
		Vector(const SRMath::Vector<2>& v) : m128(_mm_set_ps(1.0f, 0.f, v.y, v.x)) {}
		Vector(const SRMath::Vector<3>& v, float w) : m128(_mm_set_ps(w, v.z, v.y, v.x)) {}
		Vector(const SRMath::Vector<3>& v) : m128(_mm_set_ps(1.0f, v.z, v.y, v.x)) {}

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }

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

		Vector<4>& operator/=(float scalar) {
			float rcp_scalar = 1.0f / scalar;
			__m128 r = _mm_set1_ps(rcp_scalar);
			this->m128 = _mm_mul_ps(this->m128, r);
			return *this;
		}

		bool operator==(const Vector<4>& other)
		{
			// Compare each component of the vectors
			__m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

			// Check if all components are equal
			return _mm_movemask_ps(cmp) == 0xF; // All bits set means all components are equal
		}

		bool operator!=(const Vector<4>& other)
		{
			return *this == other ? false : true; // All bits set means all components are equal
		}

		Vector<4> clamp(float min, float max)
		{
			const __m128 min_vec = _mm_set1_ps(min);
			const __m128 max_vec = _mm_set1_ps(max);

			this->m128 = _mm_max_ps(min_vec, _mm_min_ps(max_vec, this->m128));

			return *this;
		}
	};

	inline Vector<3>::Vector(const Vector<4>& v)
		: m128(_mm_set_ps(0.0f, v.z, v.y, v.x)) // w는 0으로 설정
	{ }
	// inline operator overloading function
	template<size_t N>
	inline Vector<N> operator+(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret = a;

		ret += b;

		return ret;
	}

	template<size_t N>
	inline Vector<N> operator-(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret = a;

		ret -= b;
		return ret;
	}

	template<size_t N>
	inline Vector<N> operator*(const Vector<N>& v, float scalar)
	{
		Vector<N> ret = v;

		ret *= scalar;

		return ret;
	}

	template<size_t N>
	inline Vector<N> operator*(float scalar, const Vector<N>& v)
	{
		return v * scalar;
	}


	template<size_t N>
	inline Vector<N> operator*(const Vector<N>& v1, const Vector<N>& v2)
	{
		Vector<N> ret = v1;
		ret *= v2;

		return ret;
	}

	template<size_t N>
	inline Vector<N> operator/(const Vector<N>& v, float scalar)
	{
		Vector<N> ret = v;
		ret /= scalar;
		
		return ret;
	}

	template<size_t N>
	inline bool operator==(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret = a;
		return ret == b;
	}

	template<size_t N>
	inline bool operator!=(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret = a;
		return ret != b;
	}

	// -- Matrix 선언 및 정의
	template <size_t N> struct alignas(16) Matrix;
	using mat3 = Matrix<3>;
	using mat4 = Matrix<4>;

	template<size_t N>
	struct alignas(16) Matrix
	{
		static_assert(N == 3 || N == 4,
			"Matrix is only for 3 and 4 Dimensions!");

		union {
			Vector<N> cols[N]; // __m128 is included in Vec4
			float data[N * N];
		};

		Matrix() = default;
		Matrix(float diagonal)
		{
			for (auto i = 0; i < N; i++)
			{
				for (auto j = 0; j < N; j++)
					cols[i][j] = (i == j) ? diagonal : 0.0f;
			}
		}

		static Matrix<N> identity()
		{
			return Matrix<N>(1.f);
		}

		Vector<N>& operator[](size_t index) { return cols[index]; }
		const Vector<N>& operator[](size_t index) const { return cols[index]; }
	};

	template<>
	struct alignas(16) Matrix<4>
	{
		union {
			__m128 m128[4];
			Vector<4> cols[4];
			float data[4 * 4];
		};

		// 기본 생성자를 명시적으로 정의
		Matrix() : Matrix(1.f) {}

		// 대각선 값을 받는 생성자도 활성화
		Matrix(float diagonal)
		{
			// union의 한 멤버를 선택하여 초기화
			// 예를 들어 단위 행렬로 초기화
			// 역순으로 넣어야 함
			m128[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, diagonal); // col 0
			m128[1] = _mm_set_ps(0.0f, 0.0f, diagonal, 0.0f); // col 1
			m128[2] = _mm_set_ps(0.0f, diagonal, 0.0f, 0.0f); // col 2
			m128[3] = _mm_set_ps(diagonal, 0.0f, 0.0f, 0.0f); // col 3
		}

		static Matrix<4> identity()
		{
			// 이제 기본 생성자를 호출할 수 없으므로,
			// 아래와 같이 명시적으로 생성자를 호출해야 함
			return Matrix<4>(1.f);
		}

		Vector<4>& operator[](size_t index) { return cols[index]; }
		const Vector<4>& operator[](size_t index) const { return cols[index]; }
	};

	template<>
	struct Matrix<3> { // 16바이트 정렬이 필수는 아님

		union {
			Vector<3> cols[3];
			float data[3 * 3];
		};

		Vector<3>& operator[](size_t index) { return cols[index]; }
		const Vector<3>& operator[](size_t index) const { return cols[index]; }
	};

	// inline operator overloading function
	inline vec4 operator*(const mat4& m, const vec4& v)
	{
		//Classic
		//return (m[0] * v.x) + (m[1] * v.y) + (m[2] * v.z) + (m[3] * v.w);
		
		// Col-major calculate
		__m128 vx = _mm_set1_ps(v.x); // {v.x , v.x, v.x, v.x}
		__m128 vy = _mm_set1_ps(v.y);
		__m128 vz = _mm_set1_ps(v.z);
		__m128 vw = _mm_set1_ps(v.w);

		__m128 res = _mm_mul_ps(m.cols[0].m128, vx);
		res = _mm_add_ps(res, _mm_mul_ps(m.cols[1].m128, vy));
		res = _mm_add_ps(res, _mm_mul_ps(m.cols[2].m128, vz));
		res = _mm_add_ps(res, _mm_mul_ps(m.cols[3].m128, vw));

		vec4 ret;
		ret.m128 = res;
		return ret;
	}

	inline vec4 operator*(const mat4& m, const vec3& v)
	{
		Vector<4> newV(v);
		return m * newV;
	}

	inline mat4 operator*(const mat4& a, const mat4& b)
	{
		mat4 ret(0.0f);

		ret[0] = a * b[0];
		ret[1] = a * b[1];
		ret[2] = a * b[2];
		ret[3] = a * b[3];
		
		return ret;
	}


	// inline utility functions (dot product, cross product)
	inline float dot(const vec4& a, const vec4& b)
	{
		__m128 ret = _mm_dp_ps(a.m128, b.m128, 0xF1);

		return _mm_cvtss_f32(ret);
	}

	inline float dot(const vec3& a, const vec3& b)
	{
		__m128 ret = _mm_dp_ps(a.m128, b.m128, 0x71);

		return _mm_cvtss_f32(ret);
	}

	inline float dot(const vec2& a, const vec2& b)
	{
		__m128 ret = _mm_dp_ps(a.m128, b.m128, 0x31);

		return _mm_cvtss_f32(ret);
	}

	// Get Cross Product of Vector
	inline vec3 cross(const vec3& a, const vec3& b)
	{
		//Classic
		/*return {
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		};*/

		__m128 a_shuf1 = 
			_mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 0, 2, 1));
		__m128 b_shuf1 =
			_mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 1, 0, 2));

		__m128 term1 = _mm_mul_ps(a_shuf1, b_shuf1);

		__m128 a_shuf2 =
			_mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 1, 0, 2));
		__m128 b_shuf2 =
			_mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 0, 2, 1));

		__m128 term2 = _mm_mul_ps(a_shuf2, b_shuf2);

		vec3 ret;
		ret.m128 = _mm_sub_ps(term1, term2);

		return ret;
	}

	inline vec3 cross(const vec2& a, const vec2& b)
	{
		vec3 newA, newB;
		newA.m128 = a.m128;
		newB.m128 = b.m128;

		return cross(newA, newB);
	}

	// Get power of Length of Vector to compare length with the other Vector
	template<size_t N>
	inline float lengthSq(const Vector<N>& v)
	{
		return dot(v, v);
	}

	// Get Length of Vector
	template<size_t N>
	inline float length(const Vector<N>& v)
	{
		return sqrt(lengthSq(v));
	}

	// Normalize Vector
	template<size_t N>
	inline Vector<N> normalize(const Vector<N>& v)
	{
		float l = length(v);
		if (l < 1e-5f) return v;

		return v / length(v);
	}

	// Identity
	inline Matrix<4> identity()
	{
		return Matrix<4>(1.f);
	}

	// Translate Mat4
	static Matrix<4> translate(const Vector<3>& v)
	{
		Matrix<4> ret(1.0f);
		ret[3].m128 = _mm_set_ps(1.0f, v.z, v.y, v.x); // SIMD 최적화
		return ret;
	}

	// Scale Mat4
	static Matrix<4> scale(const Vector<3>& v)
	{
		Matrix<4> ret(1.0f);
		ret[0][0] = v.x;
		ret[1][1] = v.y;
		ret[2][2] = v.z;
		return ret;
	}

	// Rotate Mat4 (Right-handed Coordinate System)
	static inline Matrix<4> rotate(const vec3& rotationVector)
	{
		// 1. 벡터의 크기(length)를 회전 각도(angle)로 사용합니다.
		float angleInRadians = length(rotationVector);

		// 2. 만약 회전 각도가 거의 0이면, 계산 없이 단위 행렬을 반환합니다 (최적화 및 0으로 나누기 방지).
		if (angleInRadians < 1e-6f)
		{
			return Matrix<4>(1.0f);
		}

		// 3. 벡터를 정규화하여 회전 축을 구합니다.
		vec3 axis = rotationVector / angleInRadians;

		// 4. 기존의 로드리게스 회전 공식 코드를 그대로 사용합니다.
		const float c = std::cos(angleInRadians);
		const float s = std::sin(angleInRadians);
		const float t = 1.0f - c;

		Matrix<4> result(1); // 단위 행렬로 초기화할 필요 없음. 모든 요소를 직접 설정.

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
	static Matrix<4> perspective(float angleInRadians, float aspectRatio, float zNear, float zFar)
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

	static inline mat4 lookAt(const vec3& eye, const vec3& center, const vec3& up)
	{
		vec3 f = normalize(center - eye);
		vec3 s = normalize(cross(f, up));
		vec3 u = cross(s, f);

		mat4 result(1.0f);
		/*result[0][0] = s.x;
		result[0][1] = u.x;
		result[0][2] = -f.x;*/

		result[0].m128 = _mm_set_ps(0.0f, -f.x, u.x, s.x); // SIMD 최적화

		/*result[1][0] = s.y;
		result[1][1] = u.y;
		result[1][2] = -f.y;*/

		result[1].m128 = _mm_set_ps(0.0f, -f.y, u.y, s.y); // SIMD 최적화

		/*result[2][0] = s.z;
		result[2][1] = u.z;
		result[2][2] = -f.z;*/

		result[2].m128 = _mm_set_ps(0.0f, -f.z, u.z, s.z); // SIMD 최적화

		/*result[3][0] = -dot(s, eye);
		result[3][1] = -dot(u, eye);
		result[3][2] = dot(f, eye);*/
		
		result[3].m128 = _mm_set_ps(1.0f, dot(f, eye), -dot(u, eye), -dot(s, eye));
		
		return result;
	}

	// SIMD를 사용한 4x4 행렬 전치 함수
	inline mat4 transpose(const mat4& m) {
		mat4 result;

		// 임시 변수로 두 열씩 묶어 처리
		__m128 tmp0 = _mm_unpacklo_ps(m.m128[0], m.m128[1]); // {x0, x1, y0, y1}
		__m128 tmp1 = _mm_unpackhi_ps(m.m128[0], m.m128[1]); // {z0, z1, w0, w1}
		__m128 tmp2 = _mm_unpacklo_ps(m.m128[2], m.m128[3]); // {x2, x3, y2, y3}
		__m128 tmp3 = _mm_unpackhi_ps(m.m128[2], m.m128[3]); // {z2, z3, w2, w3}

		// 임시 변수들을 다시 섞어서 최종 행(row)을 만듭니다.
		// 이 행들이 결과 행렬의 새로운 열(column)이 됩니다.
		result.m128[0] = _mm_movelh_ps(tmp0, tmp2); // {x0, x1, x2, x3}
		result.m128[1] = _mm_movehl_ps(tmp2, tmp0); // {y0, y1, y2, y3}
		result.m128[2] = _mm_movelh_ps(tmp1, tmp3); // {z0, z1, z2, z3}
		result.m128[3] = _mm_movehl_ps(tmp3, tmp1); // {w0, w1, w2, w3}

		return result;
	}

	// SIMD를 사용한 4x4 행렬 역행렬 함수
	// 행렬식이 0에 가까우면 역행렬이 존재하지 않으므로 false를 반환합니다.
	inline std::optional<mat4> inverse(const mat4& m) {
		// We'll represent augmented matrix rows as:
	// rowL[r] = left 4 elements (original matrix row r)  -> __m128
	// rowR[r] = right 4 elements (identity initially)     -> __m128
		__m128 rowL[4];
		__m128 rowR[4];

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

		const float EPS = 1e-12f;

		// Temporary arrays for extracting elements when needed
		float tmp[4];

		for (int col = 0; col < 4; ++col) {
			// --- pivot selection (partial pivot) ---
			int pivot_row = col;
			float max_abs = 0.0f;
			for (int r = col; r < 4; ++r) {
				_mm_store_ps(tmp, rowL[r]);      // tmp[0] = col0, tmp[1] = col1, ...
				float val = tmp[col];
				float aval = std::fabs(val);
				if (aval > max_abs) { max_abs = aval; pivot_row = r; }
			}

			if (max_abs < EPS) {
				// Singular or nearly singular
				return std::nullopt;
			}

			// swap rows if pivot_row != col
			if (pivot_row != col) {
				std::swap(rowL[col], rowL[pivot_row]);
				std::swap(rowR[col], rowR[pivot_row]);
			}

			// --- normalize pivot row: divide entire row by pivot element ---
			_mm_store_ps(tmp, rowL[col]);
			float pivot = tmp[col];
			float inv_pivot = 1.0f / pivot;
			__m128 inv_pivot_v = _mm_set1_ps(inv_pivot);

			rowL[col] = _mm_mul_ps(rowL[col], inv_pivot_v);
			rowR[col] = _mm_mul_ps(rowR[col], inv_pivot_v);

			// After normalization, pivot element is (ideally) 1.0

			// --- eliminate column in other rows ---
			for (int r = 0; r < 4; ++r) {
				if (r == col) continue;
				_mm_store_ps(tmp, rowL[r]);
				float factor = tmp[col]; // scalar factor
				if (factor == 0.0f) continue;
				__m128 factor_v = _mm_set1_ps(factor);

				// row_r = row_r - factor * row_col
				rowL[r] = _mm_sub_ps(rowL[r], _mm_mul_ps(factor_v, rowL[col]));
				rowR[r] = _mm_sub_ps(rowR[r], _mm_mul_ps(factor_v, rowR[col]));
			}
		}

		// Build resulting inverse matrix (column-major).
		// result[c][r] = rowR[r] element at column c
		mat4 result(0.0f);
		for (int r = 0; r < 4; ++r) {
			_mm_store_ps(tmp, rowR[r]); // tmp[0]..tmp[3] correspond to columns 0..3
			for (int c = 0; c < 4; ++c) {
				result[c][r] = tmp[c];
			}
		}

		return result;
	}

	inline std::optional<mat4> inverse_transpose(const mat4& m) {

		if (auto inverse_mat = inverse(m)) {
			return transpose(*inverse_mat);
		}
		// 역행렬이 없는 경우, 단위 행렬 등 적절한 값을 반환
		return std::nullopt;
	}

	inline vec3 reflect (const vec3& I, const vec3& N)
	{
		// I - 2 * dot(N, I) * N
		float dotNI = dot(N, I);
		return I - (N * (2.0f * dotNI));
	}

	template<size_t N>
	inline Vector<N> clamp(const Vector<N>& v, float min, float max)
	{
		const __m128 min_vec = _mm_set1_ps(min);
		const __m128 max_vec = _mm_set1_ps(max);

		return v.clamp(min, max);
	}
}