// Simple first person style camera class that lets the viewer explore the 3D scene.
//   -It keeps track of the camera coordinate system relative to the world space
//    so that the view matrix can be constructed.  
//   -It keeps track of the viewing frustum of the camera so that the projection
//    matrix can be obtained.

#pragma once

#include "d3dUtil.h"

namespace handwork
{
	namespace rendering
	{
		class Camera
		{
		public:

			Camera();
			~Camera();

			// Get/Set world camera position.
			Vector3f GetPosition() const;
			void SetPosition(float x, float y, float z);
			void SetPosition(const Vector3f& v);

			// Get camera basis vectors.
			Vector3f GetRight() const;
			Vector3f GetUp() const;
			Vector3f GetLook() const;

			// Get frustum properties.
			float GetNearZ() const;
			float GetFarZ() const;
			float GetAspect() const;
			float GetFovY() const;
			float GetFovX() const;

			// Get near and far plane dimensions in view space coordinates.
			float GetNearWindowWidth() const;
			float GetNearWindowHeight() const;
			float GetFarWindowWidth() const;
			float GetFarWindowHeight() const;

			// Set frustum. Aspect = w / h, fov in degree.
			void SetLens(float fovY, float aspect, float zn, float zf);

			// Define camera space via LookAt parameters.
			void LookAt(const Vector3f& pos, const Vector3f& target, const Vector3f& up);

			// Get View/Proj matrices.
			Matrix4x4 GetView() const;
			Matrix4x4 GetProj() const;

			// Strafe/Walk the camera a distance d.
			void Strafe(float d);
			void Walk(float d);

			// Rotate the camera.
			void Pitch(float angle);
			void RotateY(float angle);

			// After modifying camera position/orientation, call to rebuild the view matrix.
			void UpdateViewMatrix();

		private:
			// Camera coordinate system with coordinates relative to world space.
			Vector3f mPosition = { 0.0f, 0.0f, 0.0f };
			Vector3f mRight = { 1.0f, 0.0f, 0.0f };
			Vector3f mUp = { 0.0f, 1.0f, 0.0f };
			Vector3f mLook = { 0.0f, 0.0f, 1.0f };

			// Cache frustum properties.
			float mNearZ = 0.0f;
			float mFarZ = 0.0f;
			float mAspect = 0.0f;
			float mFovY = 0.0f;
			float mNearWindowHeight = 0.0f;
			float mFarWindowHeight = 0.0f;

			bool mViewDirty = true;

			// Cache View/Proj matrices.
			Matrix4x4 mView;
			Matrix4x4 mProj;
		};

	}	// namespace rendering
}	// namespace handwork
