// Demonstrate the usage of mesh subdivision and limit surface point evaluation.

#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <Windows.h>
#include <iostream>

#include "myapp.h"
#include "utility/utility.h"
#include "utility/transform.h"
#include "mesh/fbxloader.h"
#include "mesh/meshtopology.h"
#include "utility/stringprint.h"
#include "mesh/subdivision.h"

using namespace handwork;
using namespace handwork::rendering;


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


std::vector<MeshJoint> MeshSkeleton;
std::vector<MeshVertex> MeshVertices;
std::vector<int> MeshIndices;
std::unique_ptr<SubDivision> MeshSubDiv;
float fileScale = 0.0f;

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
	mCamera->LookAt(Vector3f(0.0f, 10.0f, -20.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

	// Import 3d model
	std::string file = "./data/hand.fbx";
	bool flag = ImportFbx(file, fileScale, MeshSkeleton, MeshVertices, MeshIndices);

	// Precompute for subdivision
	std::vector<Vector3f> positions(MeshVertices.size());
	std::transform(MeshVertices.begin(), MeshVertices.end(), positions.begin(), [](MeshVertex& a) {return a.Position; });

	MeshSubDiv = std::make_unique<SubDivision>(10, KernelType::kCPU, 1, positions.size(), MeshIndices.size() / 3, &MeshIndices[0]);
	MeshSubDiv->UpdateSrc(&positions[0].x);
}

void MyApp::AddRenderData() {
#pragma region Add Materials

	// Add materials
	Material matyellow;
	matyellow.Name = "yellow";
	matyellow.Albedo = Vector3f(0.83f, 0.58f, 0.05f);
	matyellow.Roughness = 0.3f;
	matyellow.Metalness = 0.05f;
	mRenderResources->AddMaterial(matyellow);

	Material matred;
	matred.Name = "red";
	matred.Albedo = Vector3f(0.89f, 0.09f, 0.37f);
	matred.Roughness = 0.3f;
	matred.Metalness = 0.08f;
	mRenderResources->AddMaterial(matred);

	Material matblue;
	matblue.Name = "blue";
	matblue.Albedo = Vector3f(0.09f, 0.41f, 0.93f);
	matblue.Roughness = 0.3f;
	matblue.Metalness = 0.07f;
	mRenderResources->AddMaterial(matblue);

	Material matgreen;
	matgreen.Name = "green";
	matgreen.Albedo = Vector3f(0.07f, 0.78f, 0.27f);
	matgreen.Roughness = 0.3f;
	matgreen.Metalness = 0.06f;
	mRenderResources->AddMaterial(matgreen);

#pragma endregion Add Materials

#pragma region Add Geometry

	int nVertsSD;
	auto vertsData = reinterpret_cast<const Vector3f*>(MeshSubDiv->EvaluateNormal(nVertsSD));
	int level = MeshSubDiv->GetTopologyLevelNum() - 1;
	auto topology = MeshSubDiv->GetTopology(level);
	for (int i = 0; i < level; ++i) {
		vertsData += MeshSubDiv->GetTopology(i)->VertsNum;
	}

	// Add mesh geometry data
	SubmeshGeometry submesh;
	submesh.VertexCount = topology->VertsNum;
	submesh.IndexCount = topology->FacesNum * 3;
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	std::unordered_map<std::string, SubmeshGeometry> drawArgs;
	drawArgs["mesh"] = submesh;

	//std::vector<Vertex> vertices(MeshVertices.size());
	std::vector<Vertex> vertices(topology->VertsNum);
	for (size_t i = 0; i < vertices.size(); ++i) {
		vertices[i].Pos = vertsData[i];
		vertices[i].Normal = Vector3f(0.0f, 0.0f, 1.0f);
		vertices[i].TangentU = Vector3f(0.0f, 0.0f, 1.0f);
	}
	std::vector<std::uint32_t> indices;
	indices.insert(indices.end(), std::begin(topology->Indices), std::end(topology->Indices));

	mRenderResources->AddGeometryData(vertices, indices, drawArgs, "mesh");

	// Add common shape geometry data
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 10, 10);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 10, 10);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

	UINT sphereVertexOffset = 0;
	UINT cylinderVertexOffset = (UINT)sphere.Vertices.size();
	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	UINT sphereIndexOffset = 0;
	UINT cylinderIndexOffset = (UINT)sphere.Indices32.size();
	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.VertexCount = (UINT)sphere.Vertices.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.VertexCount = (UINT)cylinder.Vertices.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.VertexCount = (UINT)quad.Vertices.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	drawArgs.clear();
	drawArgs["sphere"] = sphereSubmesh;
	drawArgs["cylinder"] = cylinderSubmesh;
	drawArgs["quad"] = quadSubmesh;

	vertices.clear();
	vertices.resize(sphere.Vertices.size() + cylinder.Vertices.size() + quad.Vertices.size());
	UINT k = 0;
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < quad.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Normal = quad.Vertices[i].Normal;
		vertices[k].TangentU = quad.Vertices[i].TangentU;
	}

	indices.clear();
	indices.insert(indices.end(), std::begin(sphere.Indices32), std::end(sphere.Indices32));
	indices.insert(indices.end(), std::begin(cylinder.Indices32), std::end(cylinder.Indices32));
	indices.insert(indices.end(), std::begin(quad.Indices32), std::end(quad.Indices32));

	mRenderResources->AddGeometryData(vertices, indices, drawArgs, "shapeGeo");

#pragma endregion Add Geometry

#pragma region Add Render Items

	// Add render items
	std::vector<RenderItemData> renderItems;

	auto boundBox = mRenderResources->GetMeshGeometry("mesh")->DrawArgs["mesh"].BoxBounds;
	auto center = Vector3f(boundBox.Center.x, boundBox.Center.y, boundBox.Center.z);
	auto extent = Vector3f(boundBox.Extents.x, boundBox.Extents.y, boundBox.Extents.z);
	float scale = 8.0f / MaxComponent(extent);
	Matrix4x4 worldBase = Matrix4x4::Mul(RotateY(0.0f).GetMatrix(),
		Matrix4x4::Mul(Scale(scale, scale, scale).GetMatrix(), Translate(-center).GetMatrix()));

	RenderItemData meshRitem;
	meshRitem.World = worldBase;
	meshRitem.MatName = "yellow";
	meshRitem.GeoName = "mesh";
	meshRitem.DrawArgName = "mesh";
	meshRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderItems.push_back(meshRitem);
	mRenderResources->AddRenderItem(renderItems, RenderLayer::WireFrame);

	int nLimitSD;
	auto limitsData = reinterpret_cast<const Vector3f*>(MeshSubDiv->EvaluateLimit(nLimitSD));
	renderItems.clear();
	RenderItemData pointInstItem;
	pointInstItem.GeoName = "shapeGeo";
	pointInstItem.DrawArgName = "sphere";
	pointInstItem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pointInstItem.Instances.resize(nLimitSD);
	for (int i = 0; i < nLimitSD; ++i) {
		auto& inst = pointInstItem.Instances[i];
		inst.MatName = "blue";
		inst.World = Matrix4x4::Mul(worldBase, Matrix4x4::Mul(Translate(*(limitsData + i * 3)).GetMatrix(), Scale(0.5f, 0.5f, 0.5f).GetMatrix()));
		//inst.World = ConvertToXMFLOAT4X4(worldBase);
	}
	renderItems.push_back(pointInstItem);
	mRenderResources->AddRenderItem(renderItems, RenderLayer::OpaqueInst);

	renderItems.clear();
	std::vector<Matrix4x4> globalTransform(MeshSkeleton.size());
	std::vector<Vector3f> jointsWorldPos(MeshSkeleton.size());
	float jointScale = 4.0f;
	for (size_t i = 0; i < MeshSkeleton.size(); ++i) {
		Matrix4x4 localTransform = Translate(MeshSkeleton[i].Translation).GetMatrix();
		localTransform = Matrix4x4::Mul(localTransform, RotateZ(MeshSkeleton[i].Rotation.z).GetMatrix());
		localTransform = Matrix4x4::Mul(localTransform, RotateY(MeshSkeleton[i].Rotation.y).GetMatrix());
		localTransform = Matrix4x4::Mul(localTransform, RotateX(MeshSkeleton[i].Rotation.x).GetMatrix());
		localTransform = Matrix4x4::Mul(localTransform,
			Scale(MeshSkeleton[i].Scaling.x, MeshSkeleton[i].Scaling.y, MeshSkeleton[i].Scaling.z).GetMatrix());

		// Calulate global transform.
		if (MeshSkeleton[i].Parent < 0)
			globalTransform[i] = localTransform;
		else
			globalTransform[i] = Matrix4x4::Mul(globalTransform[MeshSkeleton[i].Parent], localTransform);
		jointsWorldPos[i] = Transform(globalTransform[i], Matrix4x4())(Vector3f(), VectorType::Point);

		// Add bone
		if (MeshSkeleton[i].Parent >= 0) {
			Vector3f boneVector = jointsWorldPos[i] - jointsWorldPos[MeshSkeleton[i].Parent];
			float boneScale = boneVector.Length();
			boneVector = Normalize(boneVector);
			float phi = std::acos(Clamp(boneVector.y, -1, 1));
			phi = -phi;
			float ct = boneVector.x / sqrt(boneVector.x * boneVector.x + boneVector.z * boneVector.z);
			float theta = std::acos(Clamp(ct, -1, 1));
			if (boneVector.z > 0)
				theta = -theta;

			Matrix4x4 boneWorld = Translate(Vector3f(0.0f, 0.5f, 0.0f)).GetMatrix();
			boneWorld = Matrix4x4::Mul(Scale(jointScale / 2, boneScale, jointScale / 2).GetMatrix(), boneWorld);
			boneWorld = Matrix4x4::Mul(RotateZ(Degrees(phi)).GetMatrix(), boneWorld);
			boneWorld = Matrix4x4::Mul(RotateY(Degrees(theta)).GetMatrix(), boneWorld);
			boneWorld = Matrix4x4::Mul(Translate(jointsWorldPos[MeshSkeleton[i].Parent]).GetMatrix(), boneWorld);

			RenderItemData boneRitem;
			boneRitem.World = Matrix4x4::Mul(worldBase, boneWorld);
			boneRitem.MatName = "green";
			boneRitem.GeoName = "shapeGeo";
			boneRitem.DrawArgName = "cylinder";
			boneRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderItems.push_back(boneRitem);
		}

		RenderItemData jointRitem;
		jointRitem.World = Matrix4x4::Mul(Matrix4x4::Mul(worldBase, Translate(jointsWorldPos[i]).GetMatrix()),
			Scale(jointScale, jointScale, jointScale).GetMatrix());
		jointRitem.MatName = "red";
		jointRitem.GeoName = "shapeGeo";
		jointRitem.DrawArgName = "sphere";
		jointRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItems.push_back(jointRitem);
	}

	mRenderResources->AddRenderItem(renderItems, RenderLayer::Opaque);

#pragma endregion Add Render Items
}

// Entrance in discrete mode.
void  MyApp::DiscreteEntrance() {}