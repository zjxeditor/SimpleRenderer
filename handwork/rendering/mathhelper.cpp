// Helper math class.

#include "MathHelper.h"
#include <float.h>
#include <cmath>

namespace handwork
{
	namespace rendering
	{
		float MathHelper::AngleFromXY(float x, float y)
		{
			float theta = 0.0f;

			// Quadrant I or IV
			if (x >= 0.0f)
			{
				// If x = 0, then atanf(y/x) = +pi/2 if y > 0
				//                atanf(y/x) = -pi/2 if y < 0
				theta = atanf(y / x); // in [-pi/2, +pi/2]

				if (theta < 0.0f)
					theta += 2.0f*Pi; // in [0, 2*pi).
			}

			// Quadrant II or III
			else
				theta = atanf(y / x) + Pi; // in [0, 2*pi).

			return theta;
		}

		Vector3f MathHelper::RandUnitVec3()
		{
			Vector3f One(1.0f, 1.0f, 1.0f);
			Vector3f Zero(0.0f, 0.0f, 0.0f);

			// Keep trying until we get a point on/in the hemisphere.
			while (true)
			{
				// Generate random point in the cube [-1,1]^3.
				Vector3f v(RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f));

				// Ignore points outside the unit sphere in order to get an even distribution 
				// over the unit sphere.  Otherwise points will clump more on the sphere near 
				// the corners of the cube.

				if (v.LengthSquared() > 1.0f)
					continue;

				return Normalize(v);
			}
		}

		Vector3f MathHelper::RandHemisphereUnitVec3(Vector3f n)
		{
			Vector3f One(1.0f, 1.0f, 1.0f);
			Vector3f Zero(0.0f, 0.0f, 0.0f);

			// Keep trying until we get a point on/in the hemisphere.
			while (true)
			{
				// Generate random point in the cube [-1,1]^3.
				Vector3f v(RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f));

				// Ignore points outside the unit sphere in order to get an even distribution 
				// over the unit sphere.  Otherwise points will clump more on the sphere near 
				// the corners of the cube.
				if (v.LengthSquared() > 1.0f)
					continue;

				// Ignore points in the bottom hemisphere.
				if (Dot(n, v) < 0.0f)
					continue;

				return Normalize(v);
			}
		}

	}	// namespace rendering
}	// namespace handwork

