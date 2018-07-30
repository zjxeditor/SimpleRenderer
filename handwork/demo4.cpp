// Demonstrate the material model in the render system.

#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <Windows.h>
#include <iostream>

#include "rendering/app.h"
#include "rendering/d3dutil.h"
#include "utility/transform.h"

#include "utility/utility.h"
#include "utility/transform.h"
#include "mesh/fbxloader.h"
#include "mesh/meshtopology.h"
#include "utility/stringprint.h"
#include "mesh/subdivision.h"

using namespace handwork;
using namespace handwork::rendering;

class MyApp : public handwork::rendering::App {
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
	virtual void DiscreteEntrance() override;

private:
	float mLightRotationAngle = 0.0f;
	Vector3f mBaseLightDirections[3] = {
		Vector3f(0.57735f, -0.57735f, 0.57735f),
		Vector3f(1.0f, 1.0f, 1.0f),
		Vector3f(1.0f, 1.0f, 1.0f)
	};
	handwork::rendering::Light mDirectLights[3];
};


MyApp::MyApp(HINSTANCE hInstance) : App(hInstance) {
	mDirectLights[0].Direction = { 0.0f, -0.1f, 1.0f };
	mDirectLights[0].Strength = { 0.9f, 0.9f, 0.9f };
	mDirectLights[1].Direction = { 1.0f, 1.0f, 1.0f };
	mDirectLights[1].Strength = { 0.0f, 0.0f, 0.0f };
	mDirectLights[2].Direction = { 1.0f, 1.0f, 1.0f };
	mDirectLights[2].Strength = { 0.0f, 0.0f, 0.0f };
}

MyApp::~MyApp() {}

void MyApp::Update() {
	if (!mContinousMode)
		return;
	App::Update();
}

// Application entry point.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) {
#if defined(DEBUG) | defined(_DEBUG)
	// Enable run-time memory check for debug builds.
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	// Create additional console window.
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CON", "r", stdin);
	freopen_s(&stream, "CON", "w", stdout);
	freopen_s(&stream, "CON", "w", stderr);
	SetConsoleTitle(L"handwork_console");
#endif

	int res = 0;
	try {
		MyApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;
		res = theApp.Run();
	} catch (DxException& e) {
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
	}

#if defined(DEBUG) | defined(_DEBUG)
	// Free additional console window.
	FreeConsole();
#endif

	return res;
}

void MyApp::PreInitialize() {
	// Config rendering pass.
	mMsaaType = MSAATYPE::MSAAx4;
	mMaxRenderWidth = 1920;
	mMaxRenderHeight = 1080;
	mClientWidth = 800;
	mClientHeight = 600;
	mContinousMode = true;
	mDepthOnlyMode = false;

	// Initialize Google's logging library.
	FLAGS_log_dir = "./log/";
	google::InitGoogleLogging("handwork");
}

void MyApp::PostInitialize() {
	// Set up camera.
	mCamera->LookAt(Vector3f(0.0f, 0.0f, -8.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
	mCamera->SetFovY(60.0f);
	mRenderResources->SetLights(&mDirectLights[0]);
}

// Sample code to add render data.
void MyApp::AddRenderData() {
	// Add materials
	Material mat0;
	mat0.Name = "mat0";
	//mat0.DiffuseAlbedo = Vector4f(0.118f, 0.380f, 0.910f, 1.0f);
	float metalness = 0.8f;
	mat0.DiffuseAlbedo = Vector4f(0.976, 0.937f, 0.380f, 1.0f);
	mat0.DiffuseAlbedo.x *= (1.0f - metalness);
	mat0.DiffuseAlbedo.y *= (1.0f - metalness);
	mat0.DiffuseAlbedo.z *= (1.0f - metalness);
	mat0.FresnelR0 = Vector3f(1.022f, 0.782f, 0.344f);
	mat0.Roughness = 0.55f;

	mRenderResources->AddMaterial(mat0);

	// Add geometry data
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 40, 40);
	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.VertexCount = (UINT)sphere.Vertices.size();;
	sphereSubmesh.StartIndexLocation = 0;
	sphereSubmesh.BaseVertexLocation = 0;

	std::vector<Vertex> vertices(sphere.Vertices.size());
	for (size_t i = 0; i < sphere.Vertices.size(); ++i) {
		vertices[i].Pos = sphere.Vertices[i].Position;
		vertices[i].Normal = sphere.Vertices[i].Normal;
		vertices[i].TangentU = sphere.Vertices[i].TangentU;
	}
	std::vector<std::uint32_t> indices;
	indices.insert(indices.end(), std::begin(sphere.Indices32), std::end(sphere.Indices32));
	std::unordered_map<std::string, SubmeshGeometry> drawArgs;
	drawArgs["sphere"] = sphereSubmesh;

	mRenderResources->AddGeometryData(vertices, indices, drawArgs, "shapeGeo");

	// Add render items
	std::vector<RenderItemData> renderItems;
	RenderItemData sphereRitem;
	sphereRitem.World = Scale(4.0f, 4.0f, 4.0f).GetMatrix();
	sphereRitem.MatName = "mat0";
	sphereRitem.GeoName = "shapeGeo";
	sphereRitem.DrawArgName = "sphere";
	sphereRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderItems.push_back(sphereRitem);
	mRenderResources->AddRenderItem(renderItems, RenderLayer::Opaque);
}

// Entrance in discrete mode.
void  MyApp::DiscreteEntrance() {
}