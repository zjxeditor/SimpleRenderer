// Provide struct definitions for the bridge between CPU and GPU.

#pragma once

#include "MathHelper.h"

#define MaxLights 16

namespace handwork
{
	namespace rendering
	{
		struct Light
		{
			Vector3f Strength = { 0.5f, 0.5f, 0.5f };
			float FalloffStart = 1.0f;                          // point/spot light only
			Vector3f Direction = { 0.0f, -1.0f, 0.0f };			// directional/spot light only
			float FalloffEnd = 10.0f;                           // point/spot light only
			Vector3f Position = { 0.0f, 0.0f, 0.0f };			// point/spot light only
			float SpotPower = 64.0f;                            // spot light only
		};

		struct ObjectConstants
		{
			Matrix4x4 World;
			UINT MaterialIndex;
			UINT ObjPad0;
			UINT ObjPad1;
			UINT ObjPad2;
		};

		struct PassConstants
		{
			Matrix4x4 View;
			Matrix4x4 InvView;
			Matrix4x4 Proj;
			Matrix4x4 InvProj;
			Matrix4x4 ViewProj;
			Matrix4x4 InvViewProj;
			Matrix4x4 ViewProjTex;
			Matrix4x4 ShadowTransform;
			Vector3f EyePosW = { 0.0f, 0.0f, 0.0f };
			float cbPerObjectPad1 = 0.0f;
			Vector2f RenderTargetSize = { 0.0f, 0.0f };
			Vector2f InvRenderTargetSize = { 0.0f, 0.0f };
			float NearZ = 0.0f;
			float FarZ = 0.0f;
			float TotalTime = 0.0f;
			float DeltaTime = 0.0f;

			Vector4f AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

			// Indices [0, NUM_DIR_LIGHTS) are directional lights;
			// indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
			// indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
			// are spot lights for a maximum of MaxLights per object.
			Light Lights[MaxLights];
		};

		struct SsaoConstants
		{
			Matrix4x4 Proj;
			Matrix4x4 InvProj;
			Matrix4x4 ProjTex;
			Vector4f OffsetVectors[14];

			// For SsaoBlur.hlsl
			Vector4f BlurWeights[3];

			Vector2f InvRenderTargetSize = { 0.0f, 0.0f };

			// Coordinates given in view space.
			float OcclusionRadius = 0.5f;
			float OcclusionFadeStart = 0.2f;
			float OcclusionFadeEnd = 2.0f;
			float SurfaceEpsilon = 0.05f;
		};

		struct MaterialData
		{
			Vector3f Albedo = { 1.0f, 1.0f, 1.0f};	//Fresnel/Diffuse
			float Roughness = 0.5f;
			float Metalness = 0.5f;
		};

		struct InstanceData
		{
			Matrix4x4 World;
			UINT MaterialIndex;
			UINT InstPad0;
			UINT InstPad1;
			UINT InstPad2;
		};

		struct Vertex
		{
			Vector3f Pos;
			Vector3f Normal;
			Vector3f TangentU;
		};

	}	// namespace rendering
}	// namespace handwork
