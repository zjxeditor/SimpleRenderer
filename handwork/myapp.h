// Customize the MyApp class to provide your own rendering logic.

#pragma once

#include "rendering/app.h"
#include "rendering/d3dutil.h"
#include "utility/transform.h"

namespace handwork
{
	class MyApp : public handwork::rendering::App
	{
	public:
		MyApp(HINSTANCE hInstance);
		MyApp(const MyApp& rhs) = delete;
		MyApp& operator=(const MyApp& rhs) = delete;
		~MyApp();

	protected:
		virtual void Update() override;
		virtual void PreInitialize() override;
		virtual void PostInitialize() override;
		virtual void AddRenderData() override;

	private:
		float mLightRotationAngle = 0.0f;
		Vector3f mBaseLightDirections[3] = {
			Vector3f(0.57735f, -0.57735f, 0.57735f),
			Vector3f(-0.57735f, -0.57735f, 0.57735f),
			Vector3f(0.0f, -0.707f, -0.707f)
		};
		handwork::rendering::Light mDirectLights[3];
	};


	MyApp::MyApp(HINSTANCE hInstance) : App(hInstance)
	{
		mDirectLights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
		mDirectLights[0].Strength = { 0.4f, 0.4f, 0.5f };
		mDirectLights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
		mDirectLights[1].Strength = { 0.1f, 0.1f, 0.1f };
		mDirectLights[2].Direction = { 0.0f, -0.707f, -0.707f };
		mDirectLights[2].Strength = { 0.0f, 0.0f, 0.0f };
	}

	MyApp::~MyApp()
	{
	}

	void MyApp::Update()
	{
		App::Update();

		// Animate the lights (and hence shadows).
		mLightRotationAngle += 0.1f*mGameTimer->DeltaTime();
		auto R = RotateY(Degrees(mLightRotationAngle));
		for (int i = 0; i < 3; ++i)
		{
			mDirectLights[i].Direction = R(mBaseLightDirections[i], VectorType::Vector);
		}

		mRenderResources->SetLights(&mDirectLights[0]);
	}

}	// namespace handwork
