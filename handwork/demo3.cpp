//// Demonstrate the discrete render mode, which only render the scene once and save the result to a local image.
//// In this demo, we encode the 24bit depth data into 3 channels of the rgb image.
//
//#include <stdio.h>
//#include <io.h>
//#include <fcntl.h>
//#include <Windows.h>
//#include <iostream>
//
//#include "myapp.h"
//#include "utility/utility.h"
//#include "utility/transform.h"
//#include "mesh/fbxloader.h"
//#include "mesh/meshtopology.h"
//#include "utility/stringprint.h"
//#include "mesh/subdivision.h"
//
//using namespace handwork;
//using namespace handwork::rendering;
//
//
//// Application entry point.
//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) {
//#if defined(DEBUG) | defined(_DEBUG)
//	// Enable run-time memory check for debug builds.
//	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
//
//	// Create additional console window.
//	AllocConsole();
//	FILE* stream;
//	freopen_s(&stream, "CON", "r", stdin);
//	freopen_s(&stream, "CON", "w", stdout);
//	freopen_s(&stream, "CON", "w", stderr);
//	SetConsoleTitle(L"handwork_console");
//#endif
//
//	int res = 0;
//	try {
//		MyApp theApp(hInstance);
//		if (!theApp.Initialize())
//			return 0;
//		res = theApp.Run();
//	} catch (DxException& e) {
//		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
//	}
//
//#if defined(DEBUG) | defined(_DEBUG)
//	// Free additional console window.
//	FreeConsole();
//#endif
//
//	return res;
//}
//
//
//std::vector<MeshJoint> MeshSkeleton;
//std::vector<MeshVertex> MeshVertices;
//std::vector<int> MeshIndices;
//std::unique_ptr<SubDivision> MeshSubDiv;
//float fileScale = 0.0f;
//
//void MyApp::PreInitialize() {
//	// Config rendering pass.
//	mMsaaType = MSAATYPE::MSAAx4;
//	mMaxRenderWidth = 1920;
//	mMaxRenderHeight = 1080;
//	mClientWidth = 800;
//	mClientHeight = 600;
//	mContinousMode = false;
//	mDepthOnlyMode = true;
//
//	// Initialize Google's logging library.
//	FLAGS_log_dir = "./log/";
//	google::InitGoogleLogging("handwork");
//}
//
//void MyApp::PostInitialize() {
//	// Set up camera.
//	mCamera->LookAt(Vector3f(0.0f, 10.0f, -20.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
//}
//
//// Sample code to add render data.
//void MyApp::AddRenderData() {
//	// Add materials
//	Material bricks0;
//	bricks0.Name = "bricks0";
//	bricks0.Albedo = Vector3f(1.0f, 1.0f, 1.0f);
//	bricks0.Roughness = 0.3f;
//	bricks0.Metalness = 0.1f;
//
//	Material tile0;
//	tile0.Name = "tile0";
//	tile0.MatCBIndex = 2;
//	tile0.Albedo = Vector3f(0.9f, 0.9f, 1.0f);
//	tile0.Roughness = 0.1f;
//	tile0.Metalness = 0.2f;
//
//	Material mirror0;
//	mirror0.Name = "mirror0";
//	mirror0.Albedo = Vector3f(0.98f, 0.97f, 0.95f);
//	mirror0.Roughness = 0.1f;
//	mirror0.Metalness = 0.9f;
//
//	Material skullMat;
//	skullMat.Name = "skullMat";
//	skullMat.Albedo = Vector3f(0.3f, 0.3f, 0.3f);
//	skullMat.Roughness = 0.2f;
//	skullMat.Metalness = 0.6f;
//
//	Material sky;
//	sky.Name = "sky";
//	sky.Albedo = Vector3f(1.0f, 1.0f, 1.0f);
//	sky.Roughness = 1.0f;
//	sky.Metalness = 0.1f;
//
//	mRenderResources->AddMaterial(bricks0);
//	mRenderResources->AddMaterial(tile0);
//	mRenderResources->AddMaterial(mirror0);
//	mRenderResources->AddMaterial(skullMat);
//	mRenderResources->AddMaterial(sky);
//
//	// Add geometry data
//	GeometryGenerator geoGen;
//	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
//	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
//	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
//	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
//	GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
//
//	UINT boxVertexOffset = 0;
//	UINT gridVertexOffset = (UINT)box.Vertices.size();
//	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
//	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
//	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
//
//	UINT boxIndexOffset = 0;
//	UINT gridIndexOffset = (UINT)box.Indices32.size();
//	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
//	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
//	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
//
//	SubmeshGeometry boxSubmesh;
//	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
//	boxSubmesh.VertexCount = (UINT)box.Vertices.size();
//	boxSubmesh.StartIndexLocation = boxIndexOffset;
//	boxSubmesh.BaseVertexLocation = boxVertexOffset;
//
//	SubmeshGeometry gridSubmesh;
//	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
//	gridSubmesh.VertexCount = (UINT)grid.Vertices.size();
//	gridSubmesh.StartIndexLocation = gridIndexOffset;
//	gridSubmesh.BaseVertexLocation = gridVertexOffset;
//
//	SubmeshGeometry sphereSubmesh;
//	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
//	sphereSubmesh.VertexCount = (UINT)sphere.Vertices.size();;
//	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
//	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
//
//	SubmeshGeometry cylinderSubmesh;
//	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
//	cylinderSubmesh.VertexCount = (UINT)cylinder.Vertices.size();
//	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
//	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
//
//	SubmeshGeometry quadSubmesh;
//	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
//	quadSubmesh.VertexCount = (UINT)quad.Vertices.size();
//	quadSubmesh.StartIndexLocation = quadIndexOffset;
//	quadSubmesh.BaseVertexLocation = quadVertexOffset;
//
//	auto totalVertexCount =
//		box.Vertices.size() +
//		grid.Vertices.size() +
//		sphere.Vertices.size() +
//		cylinder.Vertices.size() +
//		quad.Vertices.size();
//
//	std::vector<Vertex> vertices(totalVertexCount);
//	UINT k = 0;
//	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
//		vertices[k].Pos = box.Vertices[i].Position;
//		vertices[k].Normal = box.Vertices[i].Normal;
//		vertices[k].TangentU = box.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
//		vertices[k].Pos = grid.Vertices[i].Position;
//		vertices[k].Normal = grid.Vertices[i].Normal;
//		vertices[k].TangentU = grid.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
//		vertices[k].Pos = sphere.Vertices[i].Position;
//		vertices[k].Normal = sphere.Vertices[i].Normal;
//		vertices[k].TangentU = sphere.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
//		vertices[k].Pos = cylinder.Vertices[i].Position;
//		vertices[k].Normal = cylinder.Vertices[i].Normal;
//		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < quad.Vertices.size(); ++i, ++k) {
//		vertices[k].Pos = quad.Vertices[i].Position;
//		vertices[k].Normal = quad.Vertices[i].Normal;
//		vertices[k].TangentU = quad.Vertices[i].TangentU;
//	}
//	std::vector<std::uint32_t> indices;
//	indices.insert(indices.end(), std::begin(box.Indices32), std::end(box.Indices32));
//	indices.insert(indices.end(), std::begin(grid.Indices32), std::end(grid.Indices32));
//	indices.insert(indices.end(), std::begin(sphere.Indices32), std::end(sphere.Indices32));
//	indices.insert(indices.end(), std::begin(cylinder.Indices32), std::end(cylinder.Indices32));
//	indices.insert(indices.end(), std::begin(quad.Indices32), std::end(quad.Indices32));
//
//	std::unordered_map<std::string, SubmeshGeometry> drawArgs;
//	drawArgs["box"] = boxSubmesh;
//	drawArgs["grid"] = gridSubmesh;
//	drawArgs["sphere"] = sphereSubmesh;
//	drawArgs["cylinder"] = cylinderSubmesh;
//	drawArgs["quad"] = quadSubmesh;
//
//	mRenderResources->AddGeometryData(vertices, indices, drawArgs, "shapeGeo");
//
//	// Add render items
//
//	std::vector<RenderItemData> renderItems;
//	RenderItemData quadRitem;
//	quadRitem.World;
//	quadRitem.MatName = "bricks0";
//	quadRitem.GeoName = "shapeGeo";
//	quadRitem.DrawArgName = "quad";
//	quadRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//	renderItems.push_back(quadRitem);
//
//	mRenderResources->AddRenderItem(renderItems, RenderLayer::Debug);
//
//	renderItems.clear();
//
//	RenderItemData boxRitem;
//	boxRitem.World = Matrix4x4::Mul(Translate(Vector3f(0.0f, 0.5f, 0.0f)).GetMatrix(),
//		Scale(2.0f, 1.0f, 2.0f).GetMatrix());
//	//XMStoreFloat4x4(&boxRitem.World, XMMatrixScaling(2.0f, 1.0f, 2.0f)*XMMatrixTranslation(0.0f, 0.5f, 0.0f));
//	boxRitem.MatName = "bricks0";
//	boxRitem.GeoName = "shapeGeo";
//	boxRitem.DrawArgName = "box";
//	boxRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//	renderItems.push_back(boxRitem);
//
//	RenderItemData gridRitem;
//	gridRitem.World = Matrix4x4();
//	//gridRitem.World = MathHelper::Identity4x4();
//	gridRitem.MatName = "tile0";
//	gridRitem.GeoName = "shapeGeo";
//	gridRitem.DrawArgName = "grid";
//	gridRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//	renderItems.push_back(gridRitem);
//
//	for (int i = 0; i < 5; ++i) {
//		RenderItemData leftCylRitem;
//		RenderItemData rightCylRitem;
//		RenderItemData leftSphereRitem;
//		RenderItemData rightSphereRitem;
//
//		Matrix4x4 leftCylWorld = Translate(Vector3f(-5.0f, 1.5f, -10.0f + i * 5.0f)).GetMatrix();
//		Matrix4x4 rightCylWorld = Translate(Vector3f(+5.0f, 1.5f, -10.0f + i * 5.0f)).GetMatrix();
//		Matrix4x4 leftSphereWorld = Translate(Vector3f(-5.0f, 3.5f, -10.0f + i * 5.0f)).GetMatrix();
//		Matrix4x4 rightSphereWorld = Translate(Vector3f(+5.0f, 3.5f, -10.0f + i * 5.0f)).GetMatrix();
//
//		leftCylRitem.World = rightCylWorld;
//		//XMStoreFloat4x4(&leftCylRitem.World, rightCylWorld);
//		leftCylRitem.MatName = "bricks0";
//		leftCylRitem.GeoName = "shapeGeo";
//		leftCylRitem.DrawArgName = "cylinder";
//		leftCylRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//
//		rightCylRitem.World = leftCylWorld;
//		//XMStoreFloat4x4(&rightCylRitem.World, leftCylWorld);
//		rightCylRitem.MatName = "bricks0";
//		rightCylRitem.GeoName = "shapeGeo";
//		rightCylRitem.DrawArgName = "cylinder";
//		rightCylRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//
//		leftSphereRitem.World = leftSphereWorld;
//		//XMStoreFloat4x4(&leftSphereRitem.World, leftSphereWorld);
//		leftSphereRitem.MatName = "mirror0";
//		leftSphereRitem.GeoName = "shapeGeo";
//		leftSphereRitem.DrawArgName = "sphere";
//		leftSphereRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//
//		rightSphereRitem.World = rightSphereWorld;
//		//XMStoreFloat4x4(&rightSphereRitem.World, rightSphereWorld);
//		rightSphereRitem.MatName = "mirror0";
//		rightSphereRitem.GeoName = "shapeGeo";
//		rightSphereRitem.DrawArgName = "sphere";
//		rightSphereRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//
//		renderItems.push_back(leftCylRitem);
//		renderItems.push_back(rightCylRitem);
//		renderItems.push_back(leftSphereRitem);
//		renderItems.push_back(rightSphereRitem);
//	}
//
//	mRenderResources->AddRenderItem(renderItems, RenderLayer::Opaque);
//}
//
//// Entrance in discrete mode.
//void  MyApp::DiscreteEntrance() {
//	std::cout << "enter discrete mode" << std::endl;
//	mRenderResources->Update();
//	mRenderResources->Render();
//	auto res = mDeviceResources->RetrieveRenderTargetBuffer();
//	mDeviceResources->SaveToLocalImage(res, "screenshot.bmp");
//}