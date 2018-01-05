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
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		MyApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}


DirectX::XMFLOAT4X4 ConvertToXMFLOAT4X4(const Matrix4x4& m)
{
	// Transpose to match DirectX system.
	Matrix4x4 tf = Transpose(m);
	return DirectX::XMFLOAT4X4(
		tf.m[0][0], tf.m[0][1], tf.m[0][2], tf.m[0][3],
		tf.m[1][0], tf.m[1][1], tf.m[1][2], tf.m[1][3],
		tf.m[2][0], tf.m[2][1], tf.m[2][2], tf.m[2][3],
		tf.m[3][0], tf.m[3][1], tf.m[3][2], tf.m[3][3]);
}

DirectX::XMFLOAT4 ConvertToXMFLOAT4(const Vector3f& v, const float& alpha)
{
	return DirectX::XMFLOAT4(v.x, v.y, v.z, alpha);
}

DirectX::XMFLOAT4 ConvertToXMFLOAT4(float x, float y, float z, float w)
{
	return DirectX::XMFLOAT4(x, y, z, w);
}

DirectX::XMFLOAT3 ConvertToXMFLOAT3(const Vector3f& v)
{
	return DirectX::XMFLOAT3(v.x, v.y, v.z);
}

DirectX::XMFLOAT3 ConvertToXMFLOAT3(float x, float y, float z)
{
	return DirectX::XMFLOAT3(x, y, z);
}


std::vector<MeshJoint> MeshSkeleton;
std::vector<MeshVertex> MeshVertices;
std::vector<int> MeshIndices;
std::unique_ptr<SubDivision> MeshSubDiv;
float fileScale = 0.0f;

void MyApp::PreInitialize()
{
	// Config rendering pass.
	mMsaaType = MSAATYPE::MSAAx4;
	mMaxRenderWidth = 1920;
	mMaxRenderHeight = 1080;

	// Initialize Google's logging library.
	FLAGS_log_dir = "./log/";
	google::InitGoogleLogging("handwork");

	// Import 3d model
	std::string file = "./data/hand.fbx";
	bool flag = ImportFbx(file, fileScale, MeshSkeleton, MeshVertices, MeshIndices);

	// Precompute for subdivision
	std::vector<Vector3f> positions(MeshVertices.size());
	std::transform(MeshVertices.begin(), MeshVertices.end(), positions.begin(), [](MeshVertex& a) {return a.Position; });

	MeshSubDiv = std::make_unique<SubDivision>(20, KernelType::kCPU, 1, positions.size(), MeshIndices.size() / 3, &MeshIndices[0]);
	MeshSubDiv->UpdateSrc(&positions[0].x);
	/*int a = 0;
	int b = 0;
	auto resa = MeshSubDiv->EvaluateLimit(a);
	auto resb = MeshSubDiv->EvaluateNormal(b);*/

	//MeshTopology topology(indices.size(), &indices[0], positions.size(), &positions[0]);
}

void MyApp::PostInitialize()
{
	// Set up camera.
	mCamera->LookAt(DirectX::XMFLOAT3(0.0f, 10.0f, -20.0f), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
}


void MyApp::AddRenderData()
{
#pragma region Add Materials

	// Add materials
	Material matyellow;
	matyellow.Name = "yellow";
	matyellow.DiffuseAlbedo = ConvertToXMFLOAT4(0.83f, 0.58f, 0.05f, 1.0f);
	matyellow.FresnelR0 = ConvertToXMFLOAT3(0.05f, 0.05f, 0.05f);
	matyellow.Roughness = 0.3f;
	mRenderResources->AddMaterial(matyellow);
	
	Material matred;
	matred.Name = "red";
	matred.DiffuseAlbedo = ConvertToXMFLOAT4(0.89f, 0.09f, 0.37f, 1.0f);
	matred.FresnelR0 = ConvertToXMFLOAT3(0.08f, 0.08f, 0.08f);
	matred.Roughness = 0.3f;
	mRenderResources->AddMaterial(matred);

	Material matblue;
	matblue.Name = "blue";
	matblue.DiffuseAlbedo = ConvertToXMFLOAT4(0.09f, 0.41f, 0.93f, 1.0f);
	matblue.FresnelR0 = ConvertToXMFLOAT3(0.07f, 0.07f, 0.07f);
	matblue.Roughness = 0.3f;
	mRenderResources->AddMaterial(matblue);

	Material matgreen;
	matgreen.Name = "green";
	matgreen.DiffuseAlbedo = ConvertToXMFLOAT4(0.07f, 0.78f, 0.27f, 1.0f);
	matgreen.FresnelR0 = ConvertToXMFLOAT3(0.06f, 0.06f, 0.06f);
	matgreen.Roughness = 0.3f;
	mRenderResources->AddMaterial(matgreen);

#pragma endregion Add Materials

#pragma region Add Geometry

	int nVertsSD;
	auto vertsData = reinterpret_cast<const Vector3f*>(MeshSubDiv->EvaluateNormal(nVertsSD));
	int level = MeshSubDiv->GetTopologyLevelNum() - 1;
	auto topology = MeshSubDiv->GetTopology(level);
	for (int i = 0; i < level; ++i)
	{
		vertsData += MeshSubDiv->GetTopology(i)->VertsNum;
	}

	// Add mesh geometry data
	SubmeshGeometry submesh;
	/*submesh.VertexCount = (UINT)MeshVertices.size();
	submesh.IndexCount = (UINT)MeshIndices.size();*/
	submesh.VertexCount = topology->VertsNum;
	submesh.IndexCount = topology->FacesNum * 3;
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	std::unordered_map<std::string, SubmeshGeometry> drawArgs;
	drawArgs["mesh"] = submesh;

	//std::vector<Vertex> vertices(MeshVertices.size());
	std::vector<Vertex> vertices(topology->VertsNum);
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		/*vertices[i].Pos = ConvertToXMFLOAT3(MeshVertices[i].Position);
		vertices[i].Normal = ConvertToXMFLOAT3(MeshVertices[i].Normal);
		vertices[i].TangentU = ConvertToXMFLOAT3(MeshVertices[i].Tangent);*/
		vertices[i].Pos = ConvertToXMFLOAT3(vertsData[i]);
		vertices[i].Normal = ConvertToXMFLOAT3(Vector3f(0.0f, 0.0f, 1.0f));
		vertices[i].TangentU = ConvertToXMFLOAT3(Vector3f(0.0f, 0.0f, 1.0f));
	}
	std::vector<std::uint32_t> indices;
	//indices.insert(indices.end(), std::begin(MeshIndices), std::end(MeshIndices));
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
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
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
	meshRitem.World = ConvertToXMFLOAT4X4(worldBase);
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
	for (int i = 0; i < nLimitSD; ++i)
	{
		auto& inst = pointInstItem.Instances[i];
		inst.MatName = "blue";
		inst.World = ConvertToXMFLOAT4X4(Matrix4x4::Mul(worldBase, Translate(*(limitsData + i * 3)).GetMatrix()));
		//inst.World = ConvertToXMFLOAT4X4(worldBase);
	}
	renderItems.push_back(pointInstItem);
	mRenderResources->AddRenderItem(renderItems, RenderLayer::OpaqueInst);


	renderItems.clear();
	std::vector<Matrix4x4> globalTransform(MeshSkeleton.size());
	std::vector<Vector3f> jointsWorldPos(MeshSkeleton.size());
	float jointScale = 4.0f;
	for (size_t i = 0; i < MeshSkeleton.size(); ++i)
	{
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
		if(MeshSkeleton[i].Parent >= 0)
		{
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
			boneRitem.World = ConvertToXMFLOAT4X4(Matrix4x4::Mul(worldBase, boneWorld));
			boneRitem.MatName = "green";
			boneRitem.GeoName = "shapeGeo";
			boneRitem.DrawArgName = "cylinder";
			boneRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderItems.push_back(boneRitem);
		}

		RenderItemData jointRitem;
		/*jointRitem.World = ConvertToXMFLOAT4X4(Matrix4x4::Mul(
			Matrix4x4::Mul(worldBase, globalTransform[i]), Scale(jointScale, jointScale, jointScale).GetMatrix()));*/
		jointRitem.World = ConvertToXMFLOAT4X4(Matrix4x4::Mul(
			Matrix4x4::Mul(worldBase, Translate(jointsWorldPos[i]).GetMatrix()), Scale(jointScale, jointScale, jointScale).GetMatrix()));
		jointRitem.MatName = "red";
		jointRitem.GeoName = "shapeGeo";
		jointRitem.DrawArgName = "sphere";
		jointRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItems.push_back(jointRitem);
	}

	mRenderResources->AddRenderItem(renderItems, RenderLayer::Opaque);

#pragma endregion Add Render Items
}


// Sample code to add render data.
//void MyApp::AddRenderData()
//{
//	// Add materials
//	Material bricks0;
//	bricks0.Name = "bricks0";
//	bricks0.DiffuseAlbedo = ConvertToXMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
//	bricks0.FresnelR0 = ConvertToXMFLOAT3(0.1f, 0.1f, 0.1f);
//	bricks0.Roughness = 0.3f;
//
//	Material tile0;
//	tile0.Name = "tile0";
//	tile0.MatCBIndex = 2;
//	tile0.DiffuseAlbedo = ConvertToXMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
//	tile0.FresnelR0 = ConvertToXMFLOAT3(0.2f, 0.2f, 0.2f);
//	tile0.Roughness = 0.1f;
//
//	Material mirror0;
//	mirror0.Name = "mirror0";
//	mirror0.DiffuseAlbedo = ConvertToXMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
//	mirror0.FresnelR0 = ConvertToXMFLOAT3(0.98f, 0.97f, 0.95f);
//	mirror0.Roughness = 0.1f;
//
//	Material skullMat;
//	skullMat.Name = "skullMat";
//	skullMat.DiffuseAlbedo = ConvertToXMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
//	skullMat.FresnelR0 = ConvertToXMFLOAT3(0.6f, 0.6f, 0.6f);
//	skullMat.Roughness = 0.2f;
//
//	Material sky;
//	sky.Name = "sky";
//	sky.DiffuseAlbedo = ConvertToXMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
//	sky.FresnelR0 = ConvertToXMFLOAT3(0.1f, 0.1f, 0.1f);
//	sky.Roughness = 1.0f;
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
//	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
//	{
//		vertices[k].Pos = box.Vertices[i].Position;
//		vertices[k].Normal = box.Vertices[i].Normal;
//		vertices[k].TangentU = box.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
//	{
//		vertices[k].Pos = grid.Vertices[i].Position;
//		vertices[k].Normal = grid.Vertices[i].Normal;
//		vertices[k].TangentU = grid.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
//	{
//		vertices[k].Pos = sphere.Vertices[i].Position;
//		vertices[k].Normal = sphere.Vertices[i].Normal;
//		vertices[k].TangentU = sphere.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
//	{
//		vertices[k].Pos = cylinder.Vertices[i].Position;
//		vertices[k].Normal = cylinder.Vertices[i].Normal;
//		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
//	}
//	for (size_t i = 0; i < quad.Vertices.size(); ++i, ++k)
//	{
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
//
//	// Add render items
//
//	std::vector<RenderItemData> renderItems;
//	RenderItemData quadRitem;
//	quadRitem.World = MathHelper::Identity4x4();
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
//	boxRitem.World = ConvertToXMFLOAT4X4(Matrix4x4::Mul(Translate(Vector3f(0.0f, 0.5f, 0.0f)).GetMatrix(), 
//		Scale(2.0f, 1.0f, 2.0f).GetMatrix()));
//	//XMStoreFloat4x4(&boxRitem.World, XMMatrixScaling(2.0f, 1.0f, 2.0f)*XMMatrixTranslation(0.0f, 0.5f, 0.0f));
//	boxRitem.MatName = "bricks0";
//	boxRitem.GeoName = "shapeGeo";
//	boxRitem.DrawArgName = "box";
//	boxRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//	renderItems.push_back(boxRitem);
//
//	RenderItemData gridRitem;
//	gridRitem.World = ConvertToXMFLOAT4X4(Matrix4x4());
//	//gridRitem.World = MathHelper::Identity4x4();
//	gridRitem.MatName = "tile0";
//	gridRitem.GeoName = "shapeGeo";
//	gridRitem.DrawArgName = "grid";
//	gridRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//	renderItems.push_back(gridRitem);
//
//	for (int i = 0; i < 5; ++i)
//	{
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
//		leftCylRitem.World = ConvertToXMFLOAT4X4(rightCylWorld);
//		//XMStoreFloat4x4(&leftCylRitem.World, rightCylWorld);
//		leftCylRitem.MatName = "bricks0";
//		leftCylRitem.GeoName = "shapeGeo";
//		leftCylRitem.DrawArgName = "cylinder";
//		leftCylRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//
//		rightCylRitem.World = ConvertToXMFLOAT4X4(leftCylWorld);
//		//XMStoreFloat4x4(&rightCylRitem.World, leftCylWorld);
//		rightCylRitem.MatName = "bricks0";
//		rightCylRitem.GeoName = "shapeGeo";
//		rightCylRitem.DrawArgName = "cylinder";
//		rightCylRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//		
//		leftSphereRitem.World = ConvertToXMFLOAT4X4(leftSphereWorld);
//		//XMStoreFloat4x4(&leftSphereRitem.World, leftSphereWorld);
//		leftSphereRitem.MatName = "mirror0";
//		leftSphereRitem.GeoName = "shapeGeo";
//		leftSphereRitem.DrawArgName = "sphere";
//		leftSphereRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//		
//		rightSphereRitem.World = ConvertToXMFLOAT4X4(rightSphereWorld);
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


