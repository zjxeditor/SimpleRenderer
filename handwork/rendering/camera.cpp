#include "camera.h"

namespace handwork
{
	namespace rendering
	{
		using namespace DirectX;

		Camera::Camera()
		{
			SetLens(45.0f, 1.0f, 1.0f, 1000.0f);
		}

		Camera::~Camera()
		{
		}

		Vector3f Camera::GetPosition() const
		{
			return mPosition;
		}

		void Camera::SetPosition(float x, float y, float z)
		{
			mPosition = Vector3f(x, y, z);
			mViewDirty = true;
		}

		void Camera::SetPosition(const Vector3f& v)
		{
			mPosition = v;
			mViewDirty = true;
		}

		Vector3f Camera::GetRight() const
		{
			return mRight;
		}

		Vector3f Camera::GetUp() const
		{
			return mUp;
		}

		Vector3f Camera::GetLook() const
		{
			return mLook;
		}

		float Camera::GetNearZ() const
		{
			return mNearZ;
		}

		float Camera::GetFarZ() const
		{
			return mFarZ;
		}

		float Camera::GetAspect() const
		{
			return mAspect;
		}

		float Camera::GetFovY() const
		{
			return mFovY;
		}

		float Camera::GetFovX() const
		{
			float halfWidth = 0.5f*GetNearWindowWidth();
			return 2.0f*atan(halfWidth / mNearZ);
		}

		float Camera::GetNearWindowWidth() const
		{
			return mAspect * mNearWindowHeight;
		}

		float Camera::GetNearWindowHeight() const
		{
			return mNearWindowHeight;
		}

		float Camera::GetFarWindowWidth() const
		{
			return mAspect * mFarWindowHeight;
		}

		float Camera::GetFarWindowHeight() const
		{
			return mFarWindowHeight;
		}

		void Camera::SetLens(float fovY, float aspect, float zn, float zf)
		{
			// cache properties
			mFovY = fovY;
			mAspect = aspect;
			mNearZ = zn;
			mFarZ = zf;

			mNearWindowHeight = 2.0f * mNearZ * tanf(0.5f*mFovY);
			mFarWindowHeight = 2.0f * mFarZ * tanf(0.5f*mFovY);

			mProj = Perspective(mFovY, mNearZ, mFarZ, mAspect).GetMatrix();
		}

		void Camera::LookAt(const Vector3f& pos, const Vector3f& target, const Vector3f& up)
		{
			Vector3f L = Normalize(target - pos);
			Vector3f R = Normalize(Cross(up, L));
			Vector3f U = Cross(L, R);

			mPosition = pos;
			mLook = L;
			mRight = R;
			mUp = U;

			mViewDirty = true;
		}

		Matrix4x4 Camera::GetView() const
		{
			assert(!mViewDirty);
			return mView;
		}

		Matrix4x4 Camera::GetProj() const
		{
			return mProj;
		}

		void Camera::Strafe(float d)
		{
			mPosition += d * mRight;
			mViewDirty = true;
		}

		void Camera::Walk(float d)
		{
			mPosition += d * mLook;
			mViewDirty = true;
		}

		// Angle in degree.
		void Camera::Pitch(float angle)
		{
			// Rotate up and look vector about the right vector.
			auto tf = Rotate(angle, mRight);
			mUp = tf(mUp, VectorType::Vector);
			mLook = tf(mLook, VectorType::Vector);

			mViewDirty = true;
		}

		void Camera::RotateY(float angle)
		{
			// Rotate the basis vectors about the world y-axis.
			auto tf = handwork::RotateY(angle);
			mRight = tf(mRight, VectorType::Vector);
			mUp = tf(mUp, VectorType::Vector);
			mLook = tf(mLook, VectorType::Vector);

			mViewDirty = true;
		}

		void Camera::UpdateViewMatrix()
		{
			if (mViewDirty)
			{
				mLook = Normalize(mLook);
				mUp = Normalize(Cross(mLook, mRight));
				mRight = Cross(mUp, mLook);

				// Fill in the view matrix entries.
				float x = -Dot(mPosition, mRight);
				float y = -Dot(mPosition, mUp);
				float z = -Dot(mPosition, mLook);

				mView.m[0][0] = mRight.x;
				mView.m[0][1] = mRight.y;
				mView.m[0][2] = mRight.z;
				mView.m[0][3] = x;

				mView.m[1][0] = mUp.x;
				mView.m[1][1] = mUp.y;
				mView.m[1][2] = mUp.z;
				mView.m[1][3] = y;

				mView.m[2][0] = mLook.x;
				mView.m[2][1] = mLook.y;
				mView.m[2][2] = mLook.z;
				mView.m[2][3] = z;

				mView.m[3][0] = 0.0f;
				mView.m[3][1] = 0.0f;
				mView.m[3][2] = 0.0f;
				mView.m[3][3] = 1.0f;

				mViewDirty = false;
			}
		}

	}	// namespace rendering
}	// namespace handwork
	