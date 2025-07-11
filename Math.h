#pragma once

#include <cmath>

constexpr float PI = 3.1415926535f;
float angle90 = PI / 2.0f;

namespace SRMath {
	// General Template
	template <size_t N>
	struct Vector
	{
		// Check condition when instantiating it
		static_assert(N == 2 || N == 3 || N == 4,
			"Vector sturct only supports 2, 3, or 4 dimensions!");

		float data[N];
		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	// Vector2
	template <>
	struct Vector<2>
	{
		union {
			struct { float x, y; };
			float data[2];
		};

		Vector() : x(0.0f), y(0.0f) {}
		Vector(float x, float y) : x(x), y(y) {}

		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	// Vector3
	template <>
	struct Vector<3>
	{
		union {
			struct { float x, y, z; };
			float data[3];
		};

		Vector() : x(0.0f), y(0.0f), z(0.0f) {}
		Vector(float x, float y, float z) : x(x), y(y), z(z) {}
		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	// Vector4
	template <>
	struct Vector<4>
	{
		union {
			struct { float x, y, z, w; };
			float data[4];
		};

		Vector() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		Vector(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	template<size_t N>
	struct Matrix
	{
		static_assert(N == 3 || N == 4,
			"Matrix is only for 3 and 4 Dimensions!");
		
		union {
			Vector<N> cols[N];
			float data[N * N];
		};

		Matrix() = default;
		Matrix(float diagonal)
		{
			for (auto i = 0; i < N; i++)
			{
				for (auto j = 0; j < N; j++)
					data[i][j] = (i == j) ? diagonal : 0.0f;
			}
		}
		
		static Matrix<N> identity()
		{
			return Maxtrix<N>(1.f);
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

	template<size_t N>
	inline Vector<N> operator+(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret;
		for (size_t i = 0; i < N; i++)
			ret[i] = a[i] + b[i];
		return ret;
	}

	template<size_t N>
	inline Vector<N> operator-(const Vector<N>& a, const Vector<N>& b)
	{
		Vector<N> ret;
		for (size_t i = 0; i < N; i++)
			ret[i] = a[i] - b[i];
		return ret;
	}

	template<size_t N>
	inline Vector<N> operator*(const Vector<N>& v, float scalar)
	{
		Vector<N> ret;
		for (size_t i = 0; i < N; i++)
			ret[i] = v[i] * scalar;
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
		for (size_t i = 0; i < N; i++)
			ret[i] = v[i] / scalar;
		return ret;
	}

	inline vec4 operator*(const mat4& m, const vec4& v)
	{
		return (m[0] * v.x) + (m[1] * v.y) + (m[2] * v.z) + (m[3] * v.w);
	}

	inline vec4 operator*(const mat4& m, const vec3& v)
	{
		Vector<4> newV = { v.x, v.y, v.z, 1 };

		return m * newV;
	}

	template <size_t N>
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

	// Get Dot Product of Vector
	template<size_t N>
	inline float dot(const Vector<N> a, const Vector<N> b)
	{
		float ret = 0.f;
		for (size_t i = 0; i < N; i++)
			ret += a[i] * b[i];

		return ret;
	}

	// Get Cross Product of Vector
	inline vec3 cross(const vec3& a, const vec3& b)
	{
		return {
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		};
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
	inline Matrix<4> translate(const vec3& v)
	{
		Matrix<4> ret(1.0f);
		ret[3][0] = v.x;
		ret[3][1] = v.y;
		ret[3][2] = v.z;
		return ret;
	}

	// Scale Mat4
	inline Matrix<4> scale(const vec3& v)
	{
		Matrix<4> ret(1.0f);
		ret[0][0] = v.x;
		ret[1][1] = v.y;
		ret[2][2] = v.z;
		return ret;
	}

	// Rotate Mat4
	inline Matrix<4> rotate(float angleInRadians, const vec3& axis)
	{
		vec3 u = normalize(axis);

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
	

}