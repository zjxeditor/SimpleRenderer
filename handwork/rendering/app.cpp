// Customize this app class to specific requirements.

#include "app.h"
#include <WindowsX.h>

namespace handwork
{
	namespace rendering
	{
		using Microsoft::WRL::ComPtr;
		using namespace DirectX;
		using namespace DirectX::PackedVector;

		LRESULT CALLBACK
			MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			// Forward hwnd on because we can get messages (e.g., WM_CREATE)
			// before CreateWindow returns, and thus before mhMainWnd is valid.
			return App::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
		}

		App* App::mApp = nullptr;
		App* App::GetApp()
		{
			return mApp;
		}
		App::App(HINSTANCE hInstance)
			: mhAppInst(hInstance)
		{
			// Only one App can be constructed.
			assert(mApp == nullptr);
			mApp = this;
		}
		App::~App()
		{
			mDeviceResources->RegisterDeviceNotify(nullptr);
			if (mDeviceResources->GetD3DDevice() != nullptr)
				mDeviceResources->FlushCommandQueue();
			mRenderResources->ReleaseDeviceDependentResources();
		}

		bool App::Initialize()
		{
			// Initialize the windows imaging component functionality.
			ThrowIfFailed(CoInitializeEx(nullptr, COINITBASE_MULTITHREADED));

			// Do pre-initialize work.
			PreInitialize();

			// Create game timer and camera.
			mCamera = std::make_shared<Camera>(45.0f, 1.0f, 1000.0f);
			mGameTimer = std::make_shared<GameTimer>();
			mCamera->SetPosition(0.0f, 2.0f, -15.0f);

			// Init window.
			WNDCLASS wc;
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = MainWndProc;
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = mhAppInst;
			wc.hIcon = LoadIcon(0, IDI_APPLICATION);
			wc.hCursor = LoadCursor(0, IDC_ARROW);
			wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
			wc.lpszMenuName = 0;
			wc.lpszClassName = L"MainWnd";
			if (!RegisterClass(&wc))
			{
				MessageBox(0, L"RegisterClass Failed.", 0, 0);
				return false;
			}

			// Compute window rectangle dimensions based on requested client area dimensions.
			RECT R = { 0, 0, mClientWidth, mClientHeight };
			AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
			int width = R.right - R.left;
			int height = R.bottom - R.top;
			mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(),
				WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhAppInst, 0);
			if (!mhMainWnd)
			{
				MessageBox(0, L"CreateWindow Failed.", 0, 0);
				return false;
			}
			if(!mContinousMode)
				SetWindowLong(mhMainWnd, GWL_STYLE, GetWindowLong(mhMainWnd, GWL_STYLE)&~(WS_SIZEBOX | WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
			ShowWindow(mhMainWnd, SW_SHOW);
			UpdateWindow(mhMainWnd);

			// Init device resources and render resources.
			mDeviceResources = std::make_shared<DeviceResources>(mMsaaType, mMaxRenderWidth, mMaxRenderHeight);
			mRenderResources = std::make_shared<RenderResources>(mDeviceResources, mCamera, mGameTimer, mContinousMode, mDepthOnlyMode);
			mDeviceResources->RegisterDeviceNotify(this);
			mDeviceResources->SetWindow(mhAppInst, mhMainWnd);
			mRenderResources->CreateWindowSizeDependentResources();

			// Do post initialize work.
			PostInitialize();

			// Add necessary render data.
			mRenderResources->StartAddData();
			AddRenderData();
			mRenderResources->FinishAddData();

			// Update camera.
			mCamera->UpdateViewMatrix();

			return true;
		}

		int App::Run()
		{
			if (!mContinousMode)
			{
				DiscreteEntrance();
				return 0;
			}

			MSG msg = { 0 };
			mGameTimer->Reset();

			while (msg.message != WM_QUIT)
			{
				// If there are Window messages then process them.
				if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
				// Otherwise, do animation/game stuff.
				else
				{
					mGameTimer->Tick();

					if (!mAppPaused)
					{
						CalculateFrameStats();
						Update();
						mRenderResources->Update();
						mRenderResources->Render();
					}
					else
					{
						Sleep(100);
					}
				}
			}

			return (int)msg.wParam;
		}

		void App::Update()
		{
			if (!mContinousMode)
				return;

			const float dt = mGameTimer->DeltaTime();

			if (GetAsyncKeyState('W') & 0x8000)
				mCamera->Walk(mCameraSpeed*dt);
			if (GetAsyncKeyState('S') & 0x8000)
				mCamera->Walk(-1 * mCameraSpeed*dt);
			if (GetAsyncKeyState('A') & 0x8000)
				mCamera->Strafe(-1 * mCameraSpeed*dt);
			if (GetAsyncKeyState('D') & 0x8000)
				mCamera->Strafe(mCameraSpeed*dt);

			mCamera->UpdateViewMatrix();
		}

		LRESULT App::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			switch (msg)
			{
				// WM_ACTIVATE is sent when the window is activated or deactivated.  
				// We pause the game when the window is deactivated and unpause it 
				// when it becomes active.  
			case WM_ACTIVATE:
				if (LOWORD(wParam) == WA_INACTIVE)
				{
					mAppPaused = true;
					mGameTimer->Stop();
				}
				else
				{
					mAppPaused = false;
					mGameTimer->Start();
				}
				return 0;

				// WM_SIZE is sent when the user resizes the window.  
			case WM_SIZE:
				// Save the new client area dimensions.
				mClientWidth = LOWORD(lParam);
				mClientHeight = HIWORD(lParam);
				if (mDeviceResources && mRenderResources && mDeviceResources->GetD3DDevice())
				{
					if (wParam == SIZE_MINIMIZED)
					{
						mAppPaused = true;
						mMinimized = true;
						mMaximized = false;
						mDeviceResources->Trim();
					}
					else if (wParam == SIZE_MAXIMIZED)
					{
						mAppPaused = false;
						mMinimized = false;
						mMaximized = true;
						mDeviceResources->SetWindowSize(Vector2i(mClientWidth, mClientHeight));
						mRenderResources->CreateWindowSizeDependentResources();
					}
					else if (wParam == SIZE_RESTORED)
					{
						// Restoring from minimized state?
						if (mMinimized)
						{
							mAppPaused = false;
							mMinimized = false;
							mDeviceResources->SetWindowSize(Vector2i(mClientWidth, mClientHeight));
							mRenderResources->CreateWindowSizeDependentResources();
						}
						// Restoring from maximized state?
						else if (mMaximized)
						{
							mAppPaused = false;
							mMaximized = false;
							mDeviceResources->SetWindowSize(Vector2i(mClientWidth, mClientHeight));
							mRenderResources->CreateWindowSizeDependentResources();
						}
						else if (mResizing)
						{
							// If user is dragging the resize bars, we do not resize 
							// the buffers here because as the user continuously 
							// drags the resize bars, a stream of WM_SIZE messages are
							// sent to the window, and it would be pointless (and slow)
							// to resize for each WM_SIZE message received from dragging
							// the resize bars.  So instead, we reset after the user is 
							// done resizing the window and releases the resize bars, which 
							// sends a WM_EXITSIZEMOVE message.
						}
						else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
						{
							mDeviceResources->SetWindowSize(Vector2i(mClientWidth, mClientHeight));
							mRenderResources->CreateWindowSizeDependentResources();
						}
					}
				}
				return 0;

				// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
			case WM_ENTERSIZEMOVE:
				mAppPaused = true;
				mResizing = true;
				mGameTimer->Stop();
				return 0;

				// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
				// Here we reset everything based on the new window dimensions.
			case WM_EXITSIZEMOVE:
				mAppPaused = false;
				mResizing = false;
				mGameTimer->Start();
				if (mDeviceResources && mRenderResources && mDeviceResources->GetD3DDevice())
				{
					mDeviceResources->SetWindowSize(Vector2i(mClientWidth, mClientHeight));
					mRenderResources->CreateWindowSizeDependentResources();
				}
				return 0;

				// WM_DESTROY is sent when the window is being destroyed.
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;

				// The WM_MENUCHAR message is sent when a menu is active and the user presses 
				// a key that does not correspond to any mnemonic or accelerator key. 
			case WM_MENUCHAR:
				// Don't beep when we alt-enter.
				return MAKELRESULT(0, MNC_CLOSE);

				// Catch this message so to prevent the window from becoming too small.
			case WM_GETMINMAXINFO:
				((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
				((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
				return 0;

			case WM_LBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_RBUTTONDOWN:
				OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				return 0;
			case WM_LBUTTONUP:
			case WM_MBUTTONUP:
			case WM_RBUTTONUP:
				OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				return 0;
			case WM_MOUSEMOVE:
				OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				return 0;
			case WM_KEYUP:
				if (wParam == VK_ESCAPE)
				{
					PostQuitMessage(0);
				}
				return 0;
			}

			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		void App::OnMouseDown(WPARAM btnState, int x, int y)
		{
			if (!mContinousMode)
				return;

			mLastMousePos.x = x;
			mLastMousePos.y = y;
			SetCapture(mhMainWnd);
		}

		void App::OnMouseUp(WPARAM btnState, int x, int y)
		{
			if (!mContinousMode)
				return;

			ReleaseCapture();
		}

		void App::OnMouseMove(WPARAM btnState, int x, int y)
		{
			if (!mContinousMode)
				return;

			if ((btnState & MK_LBUTTON) != 0)
			{
				// Make each pixel correspond to a quarter of a degree.
				float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
				float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));
				mCamera->Pitch(Degrees(dy));
				mCamera->RotateY(Degrees(dx));
			}
			mLastMousePos.x = x;
			mLastMousePos.y = y;
		}

		void App::CalculateFrameStats()
		{
			if (!mContinousMode)
				return;

			// Code computes the average frames per second, and also the 
			// average time it takes to render one frame.  These stats 
			// are appended to the window caption bar.
			static int frameCnt = 0;
			static float timeElapsed = 0.0f;

			frameCnt++;
			// Compute averages over one second period.
			if ((mGameTimer->TotalTime() - timeElapsed) >= 1.0f)
			{
				float fps = (float)frameCnt; // fps = frameCnt / 1
				float mspf = 1000.0f / fps;

				std::wstring fpsStr = std::to_wstring(fps);
				std::wstring mspfStr = std::to_wstring(mspf);

				std::wstring windowText = mMainWndCaption +
					L"    fps: " + fpsStr +
					L"   mspf: " + mspfStr;

				SetWindowText(mhMainWnd, windowText.c_str());

				// Reset for next average.
				frameCnt = 0;
				timeElapsed += 1.0f;
			}
		}

		void App::OnDeviceLost()
		{
			mAppPaused = true;
			mGameTimer->Stop();

			mRenderResources->ReleaseDeviceDependentResources();
		}

		void App::OnDeviceRestored()
		{
			mRenderResources->CreateDeviceDependentResources();
			mRenderResources->CreateWindowSizeDependentResources();
			// Add necessary render data.
			mRenderResources->StartAddData();
			AddRenderData();
			mRenderResources->FinishAddData();

			mAppPaused = false;
			mGameTimer->Start();
		}

	}	// namespace rendering
}	// namespace handwork