// Provide interval math operation support.

#pragma once

#include "utility.h"

namespace handwork
{
	class Interval
	{
	public:
		// Interval Public Methods
		Interval(float v) : low(v), high(v) {}
		Interval(float v0, float v1)
			: low(std::min(v0, v1)), high(std::max(v0, v1)) {}
		Interval operator+(const Interval &i) const { return Interval(low + i.low, high + i.high); }
		Interval operator-(const Interval &i) const { return Interval(low - i.high, high - i.low); }
		Interval operator*(const Interval &i) const 
		{
			return Interval(std::min(std::min(low * i.low, high * i.low),
				std::min(low * i.high, high * i.high)),
				std::max(std::max(low * i.low, high * i.low),
					std::max(low * i.high, high * i.high)));
		}
		float low, high;
	};

	inline Interval Sin(const Interval &i) 
	{
		CHECK_GE(i.low, 0);
		CHECK_LE(i.high, 2.0001 * Pi);
		float sinLow = std::sin(i.low), sinHigh = std::sin(i.high);
		if (sinLow > sinHigh) std::swap(sinLow, sinHigh);
		if (i.low < Pi / 2 && i.high > Pi / 2) sinHigh = 1.;
		if (i.low < (3.f / 2.f) * Pi && i.high >(3.f / 2.f) * Pi) sinLow = -1.;
		return Interval(sinLow, sinHigh);
	}

	inline Interval Cos(const Interval &i) 
	{
		CHECK_GE(i.low, 0);
		CHECK_LE(i.high, 2.0001 * Pi);
		float cosLow = std::cos(i.low), cosHigh = std::cos(i.high);
		if (cosLow > cosHigh) std::swap(cosLow, cosHigh);
		if (i.low < Pi && i.high > Pi) cosLow = -1.;
		return Interval(cosLow, cosHigh);
	}

}	// namespace handwork
