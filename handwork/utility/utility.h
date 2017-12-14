// Pre-include header which defines common stuff.

#pragma once

#include <type_traits>
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <float.h>
#include <intrin.h>
#include <glog/logging.h>
#include "error.h"

#pragma warning(disable : 4305)  // double constant assigned to float
#pragma warning(disable : 4244)  // int -> float conversion
#pragma warning(disable : 4843)  // double -> float conversion

// Global Macros
#define snprintf _snprintf
#define HANDWORK_THREAD_LOCAL __declspec(thread)
#define alloca _alloca
#ifndef HANDWORKHE_LINE_SIZE
#define HANDWORK_L1_CACHE_LINE_SIZE 64
#endif
#define ALLOCA(TYPE, COUNT) (TYPE *) alloca((COUNT) * sizeof(TYPE))
#define MaxFloat std::numeric_limits<float>::max()
#define Infinity std::numeric_limits<float>::infinity()
#define MachineEpsilon (std::numeric_limits<float>::epsilon() * 0.5)

namespace handwork
{
	// Global Forward Declarations
	template <typename T>
	class Vector2;
	template <typename T>
	class Vector3;
	class Transform;
	struct Quaternion;
	class RNG;
	class MemoryArena;
	template <typename T, int logBlockSize = 2>
	class BlockedArray;
	struct Matrix4x4;
	class ParamSet;
	template <typename T>
	struct ParamSetItem;

	// Global Constants
	static constexpr float Pi = 3.14159265358979323846;
	static constexpr float InvPi = 0.31830988618379067154;
	static constexpr float Inv2Pi = 0.15915494309189533577;
	static constexpr float Inv4Pi = 0.07957747154594766788;
	static constexpr float PiOver2 = 1.57079632679489661923;
	static constexpr float PiOver4 = 0.78539816339744830961;
	static constexpr float Sqrt2 = 1.41421356237309504880;

	// Global Inline Functions
	inline uint32_t FloatToBits(float f)
	{
		uint32_t ui;
		memcpy(&ui, &f, sizeof(float));
		return ui;
	}

	inline float BitsToFloat(uint32_t ui)
	{
		float f;
		memcpy(&f, &ui, sizeof(uint32_t));
		return f;
	}

	inline uint64_t FloatToBits(double f)
	{
		uint64_t ui;
		memcpy(&ui, &f, sizeof(double));
		return ui;
	}

	inline double BitsToFloat(uint64_t ui)
	{
		double f;
		memcpy(&f, &ui, sizeof(uint64_t));
		return f;
	}

	inline float NextFloatUp(float v)
	{
		// Handle infinity and negative zero for _NextFloatUp()_
		if (std::isinf(v) && v > 0.) return v;
		if (v == -0.f) v = 0.f;

		// Advance _v_ to next higher float
		uint32_t ui = FloatToBits(v);
		if (v >= 0)
			++ui;
		else
			--ui;
		return BitsToFloat(ui);
	}

	inline float NextFloatDown(float v)
	{
		// Handle infinity and positive zero for _NextFloatDown()_
		if (std::isinf(v) && v < 0.) return v;
		if (v == 0.f) v = -0.f;
		uint32_t ui = FloatToBits(v);
		if (v > 0)
			--ui;
		else
			++ui;
		return BitsToFloat(ui);
	}

	inline double NextFloatUp(double v, int delta = 1)
	{
		if (std::isinf(v) && v > 0.) return v;
		if (v == -0.f) v = 0.f;
		uint64_t ui = FloatToBits(v);
		if (v >= 0.)
			ui += delta;
		else
			ui -= delta;
		return BitsToFloat(ui);
	}

	inline double NextFloatDown(double v, int delta = 1)
	{
		if (std::isinf(v) && v < 0.) return v;
		if (v == 0.f) v = -0.f;
		uint64_t ui = FloatToBits(v);
		if (v > 0.)
			ui -= delta;
		else
			ui += delta;
		return BitsToFloat(ui);
	}

	template <typename T, typename U, typename V>
	inline T Clamp(T val, U low, V high)
	{
		if (val < low)
			return low;
		else if (val > high)
			return high;
		else
			return val;
	}

	template <typename T>
	inline T Mod(T a, T b)
	{
		T result = a - (a / b) * b;
		return (T)((result < 0) ? result + b : result);
	}

	template <>
	inline float Mod(float a, float b) {
		return std::fmod(a, b);
	}

	inline float Radians(float deg) { return (Pi / 180) * deg; }

	inline float Degrees(float rad) { return (180 / Pi) * rad; }

	inline float Log2(float x)
	{
		const float invLog2 = 1.442695040888963387004650940071;
		return std::log(x) * invLog2;
	}

	inline int Log2Int(uint32_t v)
	{
		unsigned long lz = 0;
		if (_BitScanReverse(&lz, v)) return lz;
		return 0;
	}

	inline int Log2Int(int32_t v) { return Log2Int((uint32_t)v); }

	inline int Log2Int(uint64_t v)
	{
		unsigned long lz = 0;
#if defined(_WIN64)
		_BitScanReverse64(&lz, v);
#else
		if (_BitScanReverse(&lz, v >> 32))
			lz += 32;
		else
			_BitScanReverse(&lz, v & 0xffffffff);
#endif // _WIN64
		return lz;
	}

	inline int Log2Int(int64_t v) { return Log2Int((uint64_t)v); }

	template <typename T>
	inline constexpr bool IsPowerOf2(T v)
	{
		return v && !(v & (v - 1));
	}

	inline int32_t RoundUpPow2(int32_t v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		return v + 1;
	}

	inline int64_t RoundUpPow2(int64_t v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v |= v >> 32;
		return v + 1;
	}

	inline int CountTrailingZeros(uint32_t v)
	{
		unsigned long index;
		if (_BitScanForward(&index, v))
			return index;
		else
			return 32;
	}

	inline float Lerp(float t, float v1, float v2) { return (1 - t) * v1 + t * v2; }

	inline bool Quadratic(float a, float b, float c, float *t0, float *t1)
	{
		// Find quadratic discriminant
		double discrim = (double)b * (double)b - 4 * (double)a * (double)c;
		if (discrim < 0) return false;
		double rootDiscrim = std::sqrt(discrim);

		// Compute quadratic _t_ values
		double q;
		if (b < 0)
			q = -.5 * (b - rootDiscrim);
		else
			q = -.5 * (b + rootDiscrim);
		*t0 = q / a;
		*t1 = c / q;
		if (*t0 > *t1) std::swap(*t0, *t1);
		return true;
	}

}	// namespace handwork

