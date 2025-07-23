#pragma once

#include <cmath>
#include <xmmintrin.h> // SSE Header
#include <smmintrin.h> // SSE4.1

constexpr float PI = 3.1415926535f;
const float angle90 = PI / 2.0f;

namespace SRMath {
	// General Template
	template <size_t N>
	struct alignas(16) Vector
	{
		// Check condition when instantiating it
		static_assert(N == 2 || N == 3 || N == 4,
			"Vector sturct only supports 2, 3, or 4 dimensions!");
		union {
			float data[N];
			__m128 m128;
		};

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	// Vector2
	template <>
	struct alignas(16) Vector<2>
	{
		union {
			struct { float x, y; };
			float data[4]; // padding 8 bytes for m128
			__m128 m128;
		};

		Vector() : x(0.0f), y(0.0f) {}
		Vector(float x, float y) : x(x), y(y) {}

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

		Vector() : x(0.0f), y(0.0f), z(0.0f) {}
		Vector(float x, float y, float z) : x(x), y(y), z(z) {}
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

		Vector() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		Vector(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	// inline operator overloading function

	template<size_t N>
	inline Vector<N> operator+(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret;
		// Classic
		/*for (size_t i = 0; i < N; i++)
			ret[i] = a[i] + b[i];*/

		// SIMD
		ret.m128 = _mm_add_ps(a.m128, b.m128);
		return ret;
	}

	template<size_t N>
	inline Vector<N> operator-(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret;

		// Classic
		/*for (size_t i = 0; i < N; i++)
			ret[i] = a[i] - b[i];
		*/

		// SIMD
		ret.m128 = _mm_sub_ps(a.m128, b.m128);
		return ret;
	}

	template<size_t N>
	inline Vector<N> operator*(const Vector<N>& v, float scalar)
	{
		Vector<N> ret;

		// Classic
		/*for (size_t i = 0; i < N; i++)
			ret[i] = v[i] * scalar;*/
		
		__m128 s = _mm_set1_ps(scalar);
		//SIMD
		ret.m128 = _mm_mul_ps(v.m128, s);

		return ret;
	}

	template<size_t N>
	inline Vector<N> operator*(float scalar, const Vector<N>& v)
	{
		return v * scalar;
	}

	template<size_t N>
	inline Vector<N> operator/(const Vector<N>& v, float scalar)
	{
		Vector<N> ret;
		
		// Classic
		/*for (size_t i = 0; i < N; i++)
			ret[i] = v[i] / scalar;*/
		
		__m128 s = _mm_set1_ps(scalar);
		ret.m128 = _mm_div_ps(v.m128, s);

		return ret;
	}

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

	using vec2 = Vector<2>;
	using vec3 = Vector<3>;
	using vec4 = Vector<4>;
	using mat3 = Matrix<3>;
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
	static inline Matrix<4> rotate(float angleInRadians, const Vector<3>& axis)
	{
		Vector<3> u = normalize(axis);

		const float c = std::cos(angleInRadians);
		const float s = -std::sin(angleInRadians);
		const float t = 1.0f - c;

		Matrix<4> result(1.0f);

		result[0][0] = t * u.x * u.x + c;
		result[0][1] = t * u.x * u.y + s * u.z;
		result[0][2] = t * u.x * u.z - s * u.y;

		result[1][0] = t * u.y * u.x - s * u.z;
		result[1][1] = t * u.y * u.y + c;
		result[1][2] = t * u.y * u.z + s * u.x;

		result[2][0] = t * u.z * u.x + s * u.y;
		result[2][1] = t * u.z * u.y - s * u.x;
		result[2][2] = t * u.z * u.z + c;

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
}