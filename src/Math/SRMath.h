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
	using vec2 = Vector<2>;
	using vec3 = Vector<3>;
	using vec4 = Vector<4>;

	// Vector2
	template <>
	struct alignas(16) Vector<2>
	{
		union {
			struct { float x, y; };
			float data[4]; // padding 8 bytes for m128
			__m128 m128;
		};

		Vector();
		Vector(float x, float y);

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
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

		Vector();
		Vector(float x, float y, float z);
		Vector(const SRMath::Vector<2>& v);
		Vector(const SRMath::Vector<4>& v);

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
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

		Vector();
		Vector(float x, float y, float z, float w);
		Vector(const SRMath::Vector<2>& v, float w);
		Vector(const SRMath::Vector<2>& v);
		Vector(const SRMath::Vector<3>& v, float w);
		Vector(const SRMath::Vector<3>& v);

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	inline Vector<2>::Vector() : x(0.0f), y(0.0f) {}
	inline Vector<2>::Vector(float x, float y) : x(x), y(y) {}

	inline Vector<3>::Vector() : x(0.0f), y(0.0f), z(0.0f) {}
	inline Vector<3>::Vector(float x, float y, float z) : x(x), y(y), z(z) {}
	inline Vector<3>::Vector(const SRMath::Vector<2>& v) : x(v.x), y(v.y), z(0.f) {}
	inline Vector<3>::Vector(const SRMath::Vector<4>& v) : x(v.x), y(v.y), z(v.z) {}

	inline Vector<4>::Vector() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
	inline Vector<4>::Vector(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
	inline Vector<4>::Vector(const SRMath::Vector<2>& v, float w) : x(v.x), y(v.y), z(0.f), w(w) {}
	inline Vector<4>::Vector(const SRMath::Vector<2>& v) : x(v.x), y(v.y), z(0.f), w(1.0f) {}
	inline Vector<4>::Vector(const SRMath::Vector<3>& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
	inline Vector<4>::Vector(const SRMath::Vector<3>& v) : x(v.x), y(v.y), z(v.z), w(1.0f) {}

	// inline operator overloading function
	template<size_t N>
	inline Vector<N> operator+(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret = a;

		ret += b;

		return ret;
	}

	template<size_t N>
	inline Vector<N>& operator+=(Vector<N>& a, const Vector<N>& b)
	{
		// SIMD
		a.m128 = _mm_add_ps(a.m128, b.m128);
		return a;
	}

	template<size_t N>
	inline Vector<N> operator-(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret = a;

		ret -= b;
		return ret;
	}

	template<size_t N>
	inline Vector<N>& operator-=(Vector<N>& a, const Vector<N>& b)
	{
		a.m128 = _mm_sub_ps(a.m128, b.m128);
		return a;
	}

	template<size_t N>
	inline Vector<N> operator*(const Vector<N>& v, float scalar)
	{
		Vector<N> ret = v;

		ret *= scalar;

		return ret;
	}

	template<size_t N>
	inline Vector<N>& operator*=(Vector<N>& v, float scalar)
	{
		__m128 s = _mm_set1_ps(scalar);
		v.m128 = _mm_mul_ps(v.m128, s);

		return v;
	}

	template<size_t N>
	inline Vector<N> operator*(float scalar, const Vector<N>& v)
	{
		return v * scalar;
	}

	template<size_t N>
	inline Vector<N> operator/(const Vector<N>& v, float scalar)
	{
		Vector<N> ret = v;
		ret /= scalar;
		
		return ret;
	}

	template<size_t N>
	inline Vector<N>& operator/=(Vector<N>& v, float scalar)
	{
		float rcp_scalar = 1.0f / scalar;
		__m128 r = _mm_set1_ps(rcp_scalar);
		v.m128 = _mm_mul_ps(v.m128, r);

		return v;
	}

	// -- Matrix 선언 및 정의
	template <size_t N> struct alignas(16) Matrix;
	

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
	struct Matrix<3> { // 16바이트 정렬이 필수는 아님

		union {
			Vector<3> cols[3];
			float data[3 * 3];
		};

		Vector<3>& operator[](size_t index) { return cols[index]; }
		const Vector<3>& operator[](size_t index) const { return cols[index]; }
	};

	using mat3 = Matrix<3>;

	template<>
	struct alignas(16) Matrix<4>
	{
		union {
			__m128 m128[4];
			Vector<4> cols[4];
			float data[4 * 4];
		};

		// 기본 생성자를 명시적으로 정의
		Matrix() {
			
			Matrix(1.f);
		}

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

	using mat4 = Matrix<4>;

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
		Vector<4> newV = { v.x, v.y, v.z, 1 };

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
		ret[3][0] = v.x;
		ret[3][1] = v.y;
		ret[3][2] = v.z;
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
		Vector<3> axis = rotationVector / angleInRadians;

		// 4. 기존의 로드리게스 회전 공식 코드를 그대로 사용합니다.
		const float c = std::cos(angleInRadians);
		const float s = std::sin(angleInRadians);
		const float t = 1.0f - c;

		Matrix<4> result(1); // 단위 행렬로 초기화할 필요 없음. 모든 요소를 직접 설정.

		// 오른손 좌표계 열 우선 회전 행렬
		result[0][0] = c + axis.x * axis.x * t;
		result[0][1] = axis.y * axis.x * t + axis.z * s;
		result[0][2] = axis.z * axis.x * t - axis.y * s;
		result[0][3] = 0.0f;

		result[1][0] = axis.x * axis.y * t - axis.z * s;
		result[1][1] = c + axis.y * axis.y * t;
		result[1][2] = axis.z * axis.y * t + axis.x * s;
		result[1][3] = 0.0f;

		result[2][0] = axis.x * axis.z * t + axis.y * s;
		result[2][1] = axis.y * axis.z * t - axis.x * s;
		result[2][2] = c + axis.z * axis.z * t;
		result[2][3] = 0.0f;

		result[3][0] = 0.0f;
		result[3][1] = 0.0f;
		result[3][2] = 0.0f;
		result[3][3] = 1.0f;

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
		result[0][0] = s.x;
		result[1][0] = s.y;
		result[2][0] = s.z;
		result[0][1] = u.x;
		result[1][1] = u.y;
		result[2][1] = u.z;
		result[0][2] = -f.x;
		result[1][2] = -f.y;
		result[2][2] = -f.z;
		result[3][0] = -dot(s, eye);
		result[3][1] = -dot(u, eye);
		result[3][2] = dot(f, eye);
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
		__m128 A = _mm_movelh_ps(m.m128[0], m.m128[1]);
		__m128 B = _mm_movehl_ps(m.m128[1], m.m128[0]);
		__m128 C = _mm_movelh_ps(m.m128[2], m.m128[3]);
		__m128 D = _mm_movehl_ps(m.m128[3], m.m128[2]);

		__m128 detSub = _mm_sub_ps(
			_mm_mul_ps(_mm_shuffle_ps(m.m128[0], m.m128[2], 0x88), _mm_shuffle_ps(m.m128[1], m.m128[3], 0xE4)),
			_mm_mul_ps(_mm_shuffle_ps(m.m128[0], m.m128[2], 0xE4), _mm_shuffle_ps(m.m128[1], m.m128[3], 0x88))
		);

		__m128 detA = _mm_shuffle_ps(detSub, detSub, 0x44);
		__m128 detB = _mm_shuffle_ps(detSub, detSub, 0xEE);
		__m128 detC = _mm_shuffle_ps(detSub, detSub, 0x55);
		__m128 detD = _mm_shuffle_ps(detSub, detSub, 0xFF);

		__m128 D_C = _mm_sub_ps(_mm_mul_ps(D, detA), _mm_mul_ps(C, detB));
		__m128 A_B = _mm_sub_ps(_mm_mul_ps(A, detD), _mm_mul_ps(B, detC));

		__m128 detM = _mm_mul_ps(_mm_shuffle_ps(A, A, 0x00), D_C);
		detM = _mm_add_ps(detM, _mm_mul_ps(_mm_shuffle_ps(A, A, 0x55), D_C));
		detM = _mm_add_ps(detM, _mm_mul_ps(_mm_shuffle_ps(A, A, 0xAA), D_C));
		detM = _mm_add_ps(detM, _mm_mul_ps(_mm_shuffle_ps(A, A, 0xFF), D_C));

		// 행렬식이 0에 가까운지 확인
		if (_mm_cvtss_f32(detM) == 0.0f)
		{
			return std::nullopt;
		}

		__m128 invDetM = _mm_div_ps(_mm_set1_ps(1.0f), detM);

		D_C = _mm_mul_ps(D_C, invDetM);
		A_B = _mm_mul_ps(A_B, invDetM);

		__m128 X = _mm_shuffle_ps(A_B, A_B, 0xFF);
		__m128 Y = _mm_shuffle_ps(D_C, D_C, 0xAA);
		__m128 Z = _mm_shuffle_ps(A_B, A_B, 0x55);
		__m128 W = _mm_shuffle_ps(D_C, D_C, 0x00);

		__m128 R0 = _mm_shuffle_ps(Y, W, 0x77);
		__m128 R1 = _mm_shuffle_ps(Y, W, 0x22);
		__m128 R2 = _mm_shuffle_ps(X, Z, 0x77);
		__m128 R3 = _mm_shuffle_ps(X, Z, 0x22);

		// out_inverse 행렬에 결과를 저장합니다.
		mat4 out_inverse;

		out_inverse.m128[0] = _mm_shuffle_ps(R0, R1, 0x88);
		out_inverse.m128[1] = _mm_shuffle_ps(R0, R1, 0xDD);
		out_inverse.m128[2] = _mm_shuffle_ps(R2, R3, 0x88);
		out_inverse.m128[3] = _mm_shuffle_ps(R2, R3, 0xDD);

		return out_inverse;
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

}