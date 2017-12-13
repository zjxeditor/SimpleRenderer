// Provide basic geometry support.

#pragma once

#include "../handwork.h"
#include <iterator>

namespace handwork
{
	template <typename T>
	inline bool isNaN(const T x) 
	{
		return std::isnan(x);
	}
	template <>
	inline bool isNaN(const int x) 
	{
		return false;
	}

	// Vector Declarations
	template <typename T>
	class Vector2 
	{
	public:
		// Vector2 Public Methods
		Vector2() { x = y = 0; }
		Vector2(T xx, T yy) : x(xx), y(yy) {}
		bool HasNaNs() const { return isNaN(x) || isNaN(y); }

		Vector2<T> operator+(const Vector2<T> &v) const { return Vector2(x + v.x, y + v.y); }
		Vector2<T> &operator+=(const Vector2<T> &v) 
		{ 
			x += v.x;
			y += v.y;
			return *this;
		}
		Vector2<T> operator-(const Vector2<T> &v) const { return Vector2(x - v.x, y - v.y); }
		Vector2<T> &operator-=(const Vector2<T> &v)
		{
			x -= v.x;
			y -= v.y;
			return *this;
		}
		bool operator==(const Vector2<T> &v) const { return x == v.x && y == v.y; }
		bool operator!=(const Vector2<T> &v) const { return x != v.x || y != v.y; }
		template <typename U>
		Vector2<T> operator*(U f) const { return Vector2<T>(f * x, f * y); }
		template <typename U>
		Vector2<T> &operator*=(U f) 
		{
			x *= f;
			y *= f;
			return *this;
		}
		template <typename U>
		Vector2<T> operator/(U f) const 
		{
			float inv = (float)1 / f;
			return Vector2<T>(x * inv, y * inv);
		}
		template <typename U>
		Vector2<T> &operator/=(U f) 
		{
			float inv = (float)1 / f;
			x *= inv;
			y *= inv;
			return *this;
		}
		Vector2<T> operator-() const { return Vector2<T>(-x, -y); }
		
		T operator[](int i) const 
		{
			if (i == 0) return x;
			return y;
		}
		T &operator[](int i) 
		{
			if (i == 0) return x;
			return y;
		}

		float LengthSquared() const { return x * x + y * y; }
		float Length() const { return std::sqrt(LengthSquared()); }

		// Vector2 Public Data
		T x, y;
	};

	template <typename T>
	inline std::ostream &operator<<(std::ostream &os, const Vector2<T> &v) 
	{
		os << "[ " << v.x << ", " << v.y << " ]";
		return os;
	}

	template <typename T>
	class Vector3 
	{
	public:
		// Vector3 Public Methods
		T operator[](int i) const
		{
			if (i == 0) return x;
			if (i == 1) return y;
			return z;
		}
		T &operator[](int i) 
		{
			if (i == 0) return x;
			if (i == 1) return y;
			return z;
		}
		Vector3() { x = y = z = 0; }
		Vector3(T x, T y, T z) : x(x), y(y), z(z) { }
		bool HasNaNs() const { return isNaN(x) || isNaN(y) || isNaN(z); }

		Vector3<T> operator+(const Vector3<T> &v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
		Vector3<T> &operator+=(const Vector3<T> &v) 
		{
			x += v.x;
			y += v.y;
			z += v.z;
			return *this;
		}
		Vector3<T> operator-(const Vector3<T> &v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
		Vector3<T> &operator-=(const Vector3<T> &v) 
		{
			x -= v.x;
			y -= v.y;
			z -= v.z;
			return *this;
		}
		bool operator==(const Vector3<T> &v) const { return x == v.x && y == v.y && z == v.z; }
		bool operator!=(const Vector3<T> &v) const { return x != v.x || y != v.y || z != v.z; }
		template <typename U>
		Vector3<T> operator*(U s) const { return Vector3<T>(s * x, s * y, s * z); }
		template <typename U>
		Vector3<T> &operator*=(U s) 
		{
			x *= s;
			y *= s;
			z *= s;
			return *this;
		}
		template <typename U>
		Vector3<T> operator/(U f) const 
		{
			float inv = (float)1 / f;
			return Vector3<T>(x * inv, y * inv, z * inv);
		}
		template <typename U>
		Vector3<T> &operator/=(U f) 
		{
			float inv = (float)1 / f;
			x *= inv;
			y *= inv;
			z *= inv;
			return *this;
		}
		Vector3<T> operator-() const { return Vector3<T>(-x, -y, -z); }
		float LengthSquared() const { return x * x + y * y + z * z; }
		float Length() const { return std::sqrt(LengthSquared()); }

		// Vector3 Public Data
		T x, y, z;
	};

	template <typename T>
	inline std::ostream &operator<<(std::ostream &os, const Vector3<T> &v) 
	{
		os << "[ " << v.x << ", " << v.y << ", " << v.z << " ]";
		return os;
	}

	typedef Vector2<float> Vector2f;
	typedef Vector2<int> Vector2i;
	typedef Vector3<float> Vector3f;
	typedef Vector3<int> Vector3i;

	// Geometry Inline Functions

	template <typename T, typename U>
	inline Vector3<T> operator*(U s, const Vector3<T> &v) {	return v * s; }

	template <typename T>
	Vector3<T> Abs(const Vector3<T> &v) { return Vector3<T>(std::abs(v.x), std::abs(v.y), std::abs(v.z)); }

	template <typename T>
	inline T Dot(const Vector3<T> &v1, const Vector3<T> &v2) { return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z; }

	template <typename T>
	inline T AbsDot(const Vector3<T> &v1, const Vector3<T> &v2) { return std::abs(Dot(v1, v2));	}

	template <typename T>
	inline Vector3<T> Cross(const Vector3<T> &v1, const Vector3<T> &v2) 
	{
		double v1x = v1.x, v1y = v1.y, v1z = v1.z;
		double v2x = v2.x, v2y = v2.y, v2z = v2.z;
		return Vector3<T>((v1y * v2z) - (v1z * v2y), (v1z * v2x) - (v1x * v2z),
			(v1x * v2y) - (v1y * v2x));
	}

	template <typename T>
	inline Vector3<T> Normalize(const Vector3<T> &v) { return v / v.Length(); }

	template <typename T>
	T MinComponent(const Vector3<T> &v) { return std::min(v.x, std::min(v.y, v.z)); }

	template <typename T>
	T MaxComponent(const Vector3<T> &v) { return std::max(v.x, std::max(v.y, v.z));	}

	template <typename T>
	int MaxDimension(const Vector3<T> &v) { return (v.x > v.y) ? ((v.x > v.z) ? 0 : 2) : ((v.y > v.z) ? 1 : 2); }

	template <typename T>
	Vector3<T> Min(const Vector3<T> &p1, const Vector3<T> &p2) 
	{
		return Vector3<T>(std::min(p1.x, p2.x), 
			std::min(p1.y, p2.y),
			std::min(p1.z, p2.z));
	}

	template <typename T>
	Vector3<T> Max(const Vector3<T> &p1, const Vector3<T> &p2) 
	{
		return Vector3<T>(std::max(p1.x, p2.x), 
			std::max(p1.y, p2.y),
			std::max(p1.z, p2.z));
	}

	template <typename T>
	Vector3<T> Permute(const Vector3<T> &v, int x, int y, int z) { return Vector3<T>(v[x], v[y], v[z]); }

	template <typename T>
	inline void CoordinateSystem(const Vector3<T> &v1, Vector3<T> *v2, Vector3<T> *v3) 
	{
		if (std::abs(v1.x) > std::abs(v1.y))
			*v2 = Vector3<T>(-v1.z, 0, v1.x) / std::sqrt(v1.x * v1.x + v1.z * v1.z);
		else
			*v2 = Vector3<T>(0, v1.z, -v1.y) / std::sqrt(v1.y * v1.y + v1.z * v1.z);
		*v3 = Cross(v1, *v2);
	}

	template <typename T>
	inline float Distance(const Vector3<T> &v1, const Vector3<T> &v2) { return (v1 - v2).Length(); }

	template <typename T>
	inline float DistanceSquared(const Vector3<T> &v1, const Vector3<T> &v2) { return (v1 - v2).LengthSquared(); }

	template <typename T>
	inline Vector3<T> Faceforward(const Vector3<T> &v, const Vector3<T> &v2) {	return (Dot(v, v2) < 0.f) ? -v : v;	}

	inline Vector3f SphericalDirection(float sinTheta, float cosTheta, float phi) 
	{
		return Vector3f(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
	}

	inline Vector3f SphericalDirection(float sinTheta, float cosTheta, float phi,
		const Vector3f &x, const Vector3f &y, const Vector3f &z) 
	{
		return sinTheta * std::cos(phi) * x + sinTheta * std::sin(phi) * y + cosTheta * z;
	}

	inline float SphericalTheta(const Vector3f &v) 
	{
		return std::acos(Clamp(v.z, -1, 1));
	}

	inline float SphericalPhi(const Vector3f &v) 
	{
		float p = std::atan2(v.y, v.x);
		return (p < 0) ? (p + 2 * Pi) : p;
	}

}	// namespace handwork
