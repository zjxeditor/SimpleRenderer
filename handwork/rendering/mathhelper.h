// Helper math class.

#pragma once

#include <Windows.h>
#include <cstdint>
#include "../utility/utility.h"
#include "../utility/geometry.h"
#include "../utility/transform.h"

namespace handwork
{
	namespace rendering
	{
		class MathHelper
		{
		public:
			// Returns random float in [0, 1).
			static float RandF()
			{
				return (float)(rand()) / (float)RAND_MAX;
			}

			// Returns random float in [a, b).
			static float RandF(float a, float b)
			{
				return a + RandF()*(b - a);
			}

			static int Rand(int a, int b)
			{
				return a + rand() % ((b - a) + 1);
			}

			// Returns the polar angle of the point (x,y) in [0, 2*PI). In radians.
			static float AngleFromXY(float x, float y);

			// In radians.
			static Vector3f SphericalToCartesian(float radius, float theta, float phi)
			{
				return Vector3f(
					radius*sinf(phi)*cosf(theta),
					radius*cosf(phi),
					radius*sinf(phi)*sinf(theta));
			}

			static Matrix4x4 InverseTranspose(const Matrix4x4& M)
			{
				// Inverse-transpose is just applied to normals.  So zero out 
				// translation row so that it doesn't get into our inverse-transpose
				// calculation--we don't want the inverse-transpose of the translation.
				Matrix4x4 A = M;
				A.m[0][3] = 0.0f;
				A.m[1][3] = 0.0f;
				A.m[2][3] = 0.0f;
				A.m[3][3] = 1.0f;
				return Transpose(Inverse(A));
			}

			static Vector3f RandUnitVec3();
			static Vector3f RandHemisphereUnitVec3(Vector3f n);
		};

	}	// namespace rendering
}	// namespace handwork

