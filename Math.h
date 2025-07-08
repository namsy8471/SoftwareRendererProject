#pragma once

#include <cmath>

namespace srMath {
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

		// 생성자 및 연산자들...
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

		// 생성자 및 연산자들...
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

		// 생성자 및 연산자들...
		Vector() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		Vector(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		float& operator[](size_t index) { return data[index]; }
		const float& operator[](size_t index) const { return data[index]; }
	};

	using vec2 = Vector<2>;
	using vec3 = Vector<3>;
	using vec4 = Vector<4>;

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
		Vector<N> ret;
		for (size_t i = 0; i < N; i++)
			ret[i] = v[i] * scalar;
		return ret;
	}

	template<size_t N>
	inline Vector<N> operator/(const Vector<N>& v, float scalar)
	{
		Vector<N> ret;
		for (size_t i = 0; i < N; i++)
			ret[i] = v[i] / scalar;
		return ret;
	}

	// inline utility functions (dot product, cross product)
	template<size_t N>
	inline float dot(const Vector<N> a, const Vector<N> b)
	{
		float ret = 0.f;
		for (size_t i = 0; i < N; i++)
			ret += a[i] * b[i];

		return ret;
	}

	inline vec3 cross(const vec3& a, const vec3& b)
	{
		return {
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		};
	}
}