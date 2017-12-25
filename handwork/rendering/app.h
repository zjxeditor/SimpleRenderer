// Customize this app class to specific requirements.

#pragma once

#include "geogenerator.h"
#include "camera.h"
#include "frameresource.h"
#include "shadowmap.h"
#include "ssao.h"
#include "deviceresources.h"
#include "renderresources.h"

namespace handwork
{
	namespace rendering
	{
		class App : IDeviceNotify
		{
		public:
			App(HINSTANCE hInstance);
			App(const App& rhs) = delete;
			App& operator=(const App& rhs) = delete;
			~App();
			static App* GetApp();

			LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

			bool Initialize();
			int Run();
			void SetCameraSpeed(float speed) { mCameraSpeed = speed; }

		protected:
			virtual void Update();
			virtual void OnMouseDown(WPARAM btnState, int x, int y);
			virtual void OnMouseUp(WPARAM btnState, int x, int y);
			virtual void OnMouseMove(WPARAM btnState, int x, int y);

			// Use these three method to do main application work.
			virtual void PreInitialize() = 0;
			virtual void PostInitialize() = 0;
			virtual void AddRenderData() = 0;

			// IDeviceNotify
			virtual void OnDeviceLost();
			virtual void OnDeviceRestored();

			std::shared_ptr<DeviceResources> mDeviceResources;
			std::shared_ptr<RenderResources> mRenderResources;
			std::shared_ptr<GameTimer> mGameTimer;
			std::shared_ptr<Camera> mCamera;

			MSAATYPE mMsaaType = MSAATYPE::MSAAx4;
			UINT mMaxRenderWidth = 1920;
			UINT mMaxRenderHeight = 1080;
			bool mUseStandardAssets = false;

		private:
			static App* mApp;

			void CalculateFrameStats();

			HINSTANCE mhAppInst = nullptr;		// application instance handle
			HWND      mhMainWnd = nullptr;		// main window handle
			bool      mAppPaused = false;		// is the application paused?
			bool      mMinimized = false;		// is the application minimized?
			bool      mMaximized = false;		// is the application maximized?
			bool      mResizing = false;		// are the resize bars being dragged?
			bool      mFullscreenState = false;	// fullscreen enabled

			std::wstring mMainWndCaption = L"handwork";
			int mClientWidth = 800;
			int mClientHeight = 600;
			POINT mLastMousePos;
			float mCameraSpeed = 10.0f;
		};

	}	// namespace rendering
}	// namespace handwork
