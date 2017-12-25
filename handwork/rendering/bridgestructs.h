// Provide struct definitions for the bridge between CPU and GPU.

#pragma once

#include <DirectXMath.h>
#include "MathHelper.h"

#define MaxLights 16

namespace handwork
{
	namespace rendering
	{
		struct Light
		{
			DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
			float FalloffStart = 1.0f;                          // point/spot light only
			DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
			float FalloffEnd = 10.0f;                           // point/spot light only
			DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
			float SpotPower = 64.0f;                            // spot light only
		};

		struct ObjectConstants
		{
			DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
			UINT     MaterialIndex;
			UINT     ObjPad0;
			UINT     ObjPad1;
			UINT     ObjPad2;
		};

		struct PassConstants
		{
			DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
			DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
			DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
			DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
			DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
			DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
			DirectX::XMFLOAT4X4 ViewProjTex = MathHelper::Identity4x4();
			DirectX::XMFLOAT4X4 ShadowTransform = MathHelper::Identity4x4();
			DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
			float cbPerObjectPad1 = 0.0f;
			DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
			DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
			float NearZ = 0.0f;
			float FarZ = 0.0f;
			float TotalTime = 0.0f;
			float DeltaTime = 0.0f;

			DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

			// Indices [0, NUM_DIR_LIGHTS) are directional lights;
			// indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
			// indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
			// are spot lights for a maximum of MaxLights per object.
			Light Lights[MaxLights];
		};

		struct SsaoConstants
		{
			DirectX::XMFLOAT4X4 Proj;
			DirectX::XMFLOAT4X4 InvProj;
			DirectX::XMFLOAT4X4 ProjTex;
			DirectX::XMFLOAT4   OffsetVectors[14];

			// For SsaoBlur.hlsl
			DirectX::XMFLOAT4 BlurWeights[3];

			DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };

			// Coordinates given in view space.
			float OcclusionRadius = 0.5f;
			float OcclusionFadeStart = 0.2f;
			float OcclusionFadeEnd = 2.0f;
			float SurfaceEpsilon = 0.05f;
		};

		struct MaterialData
		{
			DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
			DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
			float Roughness = 0.5f;
		};

		struct Vertex
		{
			DirectX::XMFLOAT3 Pos;
			DirectX::XMFLOAT3 Normal;
			DirectX::XMFLOAT3 TangentU;
		};

	}	// namespace rendering
}	// namespace handwork
