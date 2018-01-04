// Do efficient mesh subdivison using OpenSubDiv library.

#include "subdivision.h"
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/primvarRefiner.h>
#include <opensubdiv/far/patchTableFactory.h>
#include <opensubdiv/far/ptexIndices.h>
#include <opensubdiv/far/stencilTable.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>
#include <opensubdiv/osd/cpuEvaluator.h>
#include <opensubdiv/osd/ompEvaluator.h>
#include <opensubdiv/osd/mesh.h>
#include "../rendering/gametimer.h"
#include "../utility/stringprint.h"

namespace handwork
{
	using namespace OpenSubdiv;

	class StencilOutputCPU : public StencilOutputBase
	{
	public:
		StencilOutputCPU(
			std::shared_ptr<Far::StencilTable const>& controlNormalStencils,
			std::shared_ptr<Far::LimitStencilTable const>& controlLimitStencils,
			int numSrcVerts,
			bool omp = false) :
			srcDesc(/*offset*/ 0, /*length*/ 3, /*stride*/ 3),
			dstDesc(/*offset*/ 0, /*length*/ 3, /*stride*/ 9),
			duDesc( /*offset*/ 3, /*length*/ 3, /*stride*/ 9),
			dvDesc( /*offset*/ 6, /*length*/ 3, /*stride*/ 9),
			dstNorDesc(/*offset*/ 0, /*length*/ 3, /*stride*/ 3),
			numStencilsNormal(0),
			numStencilsLimit(0),
			useOMP(omp)
		{
			// src buffer  [ P(xyz) ]
			// dst buffer  [ P(xyz), du(xyz), dv(xyz) ]

			normalStencils = controlNormalStencils;
			limitStencils = controlLimitStencils; 

			if(normalStencils != nullptr)
				numStencilsNormal = normalStencils->GetNumStencils();
			if (limitStencils != nullptr)
				numStencilsLimit = limitStencils->GetNumStencils();

			srcData = std::unique_ptr<Osd::CpuVertexBuffer>(Osd::CpuVertexBuffer::Create(3, numSrcVerts, nullptr));
			if(numStencilsNormal != 0)
				dstDataNormal = std::unique_ptr<Osd::CpuVertexBuffer>(Osd::CpuVertexBuffer::Create(3, numStencilsNormal, nullptr));
			if(numStencilsLimit != 0)
				dstDataLimit = std::unique_ptr<Osd::CpuVertexBuffer>(Osd::CpuVertexBuffer::Create(9, numStencilsLimit, nullptr));
		}

		virtual int GetNumStencilsNormal() const override
		{
			return numStencilsNormal;
		}
		virtual int GetNumStencilsLimit() const override
		{
			return numStencilsLimit;
		}
		virtual void UpdateData(const float *src, int startVertex, int numVertices) override
		{
			srcData->UpdateData(src, startVertex, numVertices, nullptr);
		};
		virtual void EvalStencilsNormal() override
		{
			if (numStencilsNormal == 0)
				return;
			if (useOMP)
			{
				Osd::OmpEvaluator::EvalStencils(
					srcData.get(), srcDesc,
					dstDataNormal.get(), dstNorDesc,
					normalStencils.get(),
					nullptr,
					nullptr);
			}
			else
			{
				Osd::CpuEvaluator::EvalStencils(
					srcData.get(), srcDesc,
					dstDataNormal.get(), dstNorDesc,
					normalStencils.get(),
					nullptr,
					nullptr);
			}
		}
		virtual void EvalStencilsLimit() override
		{
			if (numStencilsLimit == 0)
				return;
			if(useOMP)
			{
				Osd::OmpEvaluator::EvalStencils(
					srcData.get(), srcDesc,
					dstDataLimit.get(), dstDesc,
					dstDataLimit.get(), duDesc,
					dstDataLimit.get(), dvDesc,
					limitStencils.get(),
					nullptr,
					nullptr);
			}
			else
			{
				Osd::CpuEvaluator::EvalStencils(
					srcData.get(), srcDesc,
					dstDataLimit.get(), dstDesc,
					dstDataLimit.get(), duDesc,
					dstDataLimit.get(), dvDesc,
					limitStencils.get(),
					nullptr,
					nullptr);
			}
		}
		virtual float* GetDstDataNormal() override
		{
			if (numStencilsNormal == 0) return nullptr;
			return dstDataNormal.get()->BindCpuBuffer();
		}
		virtual float* GetDstDataLimit() override
		{
			if (numStencilsLimit == 0) return nullptr;
			return dstDataLimit.get()->BindCpuBuffer();
		}

	private:
		std::unique_ptr<Osd::CpuVertexBuffer> srcData;
		std::unique_ptr<Osd::CpuVertexBuffer> dstDataNormal;
		std::unique_ptr<Osd::CpuVertexBuffer> dstDataLimit;
		Osd::BufferDescriptor srcDesc;
		Osd::BufferDescriptor dstDesc;
		Osd::BufferDescriptor duDesc;
		Osd::BufferDescriptor dvDesc;
		Osd::BufferDescriptor dstNorDesc;
		
		std::shared_ptr<Far::StencilTable const> normalStencils;
		std::shared_ptr<Far::LimitStencilTable const> limitStencils;
		int numStencilsNormal;
		int numStencilsLimit;

		bool useOMP;
	};


	SubDivision::SubDivision(int samples, KernelType type, int level, int vertsNum, int facesNum, int const* indices, bool leftHand)
		: samplesPerFace(samples), kernel(type), isolationLevel(level), nVerts(vertsNum)
	{
		typedef Far::LimitStencilTableFactory::LocationArray LocationArray;
		typedef Far::TopologyDescriptor Descriptor;

		LOG(INFO) << "Start precomputation for mesh subdivision.";
		rendering::GameTimer timer;	// Used for timing statistics.

		// create Far mesh (topology)
		Sdc::SchemeType sdcType = Sdc::SCHEME_CATMARK;
		Sdc::Options sdcOptions;
		sdcOptions.SetCreasingMethod(Sdc::Options::CREASE_CHAIKIN);
		sdcOptions.SetFVarLinearInterpolation(Sdc::Options::FVAR_LINEAR_CORNERS_ONLY);
		sdcOptions.SetTriangleSubdivision(Sdc::Options::TRI_SUB_SMOOTH);
		sdcOptions.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_ONLY);

		Descriptor desc;
		std::unique_ptr<int[]> vertsperface(new int[facesNum]);
		for (int i = 0; i < facesNum; ++i)
			vertsperface[i] = 3;
		desc.numVertices = vertsNum;
		desc.numFaces = facesNum;
		desc.numVertsPerFace = &vertsperface[0];
		desc.vertIndicesPerFace = indices;
		desc.isLeftHanded = leftHand;

		timer.Reset();
		// Instantiate a FarTopologyRefiner from the descriptor
		std::unique_ptr<Far::TopologyRefiner> refiner(Far::TopologyRefinerFactory<Descriptor>::Create(desc,
			Far::TopologyRefinerFactory<Descriptor>::Options(sdcType, sdcOptions)));
		// Adaptively refine the topology
		refiner->RefineAdaptive(Far::TopologyRefiner::AdaptiveOptions(isolationLevel));
		timer.Stop();
		LOG(INFO) << StringPrintf("Time for topology calculation in seconds: %f", timer.TotalTime());

		// Store the topology information.
		int nLevel = refiner->GetNumLevels();
		topologyInformation.resize(nLevel);
		auto& level0 = refiner->GetLevel(0);
		auto& topology0 = topologyInformation[0];
		topology0.VertsNum = level0.GetNumVertices();
		topology0.FacesNum = level0.GetNumFaces();
		topology0.Indices.resize(topology0.FacesNum * 3);
		for (int face = 0; face < topology0.FacesNum; ++face)
		{
			Far::ConstIndexArray fverts = level0.GetFaceVertices(face);
			assert(fverts.size() == 3);
			topology0.Indices[face * 3] = fverts[0];
			topology0.Indices[face * 3 + 1] = fverts[1];
			topology0.Indices[face * 3 + 2] = fverts[2];
		}
		for (int i = 1; i < nLevel; ++i)
		{
			auto& currentLevel = refiner->GetLevel(i);
			auto& currentItem = topologyInformation[i];
			currentItem.VertsNum = currentLevel.GetNumVertices();
			int quadFacesNum = currentLevel.GetNumFaces();
			currentItem.FacesNum = quadFacesNum * 2;
			currentItem.Indices.resize(currentItem.FacesNum * 3);
			for(int face = 0; face < quadFacesNum; ++face)
			{
				Far::ConstIndexArray fverts = currentLevel.GetFaceVertices(face);
				assert(fverts.size() == 4);
				currentItem.Indices[face * 3] = fverts[0];
				currentItem.Indices[face * 3 + 1] = fverts[1];
				currentItem.Indices[face * 3 + 2] = fverts[2];
				currentItem.Indices[face * 3 + 3] = fverts[0];
				currentItem.Indices[face * 3 + 4] = fverts[2];
				currentItem.Indices[face * 3 + 5] = fverts[3];
			}
		}

		timer.Reset();
		// Generate normal stencil table. Containes control points, intermediate points and final points.
		// Control points stencils and intermediate points stencils are required by limit stencils.
		Far::StencilTableFactory::Options stencilOptions;
		stencilOptions.generateIntermediateLevels = true;
		stencilOptions.generateControlVerts = true;
		stencilOptions.generateOffsets = true;
		stencilOptions.factorizeIntermediateLevels = true;
		stencilOptions.interpolationMode = Far::StencilTableFactory::INTERPOLATE_VERTEX;
		std::shared_ptr<Far::StencilTable const> normalStencils(Far::StencilTableFactory::Create(*refiner, stencilOptions));
		timer.Stop();
		LOG(INFO) << StringPrintf("Time for %d normal stencils calculation in seconds: %f", normalStencils->GetNumStencils(), timer.TotalTime());

		timer.Reset();
		// generate normal patch table
		Far::PatchTableFactory::Options patchTableOptions;
		patchTableOptions.SetEndCapType(Far::PatchTableFactory::Options::ENDCAP_GREGORY_BASIS);
		patchTableOptions.useInfSharpPatch = refiner->GetAdaptiveOptions().useInfSharpPatch;
		patchTableOptions.useSingleCreasePatch = refiner->GetAdaptiveOptions().useSingleCreasePatch;
		patchTableOptions.generateAllLevels = false;
		std::shared_ptr<Far::PatchTable const> patchTable(Far::PatchTableFactory::Create(*refiner, patchTableOptions));
		timer.Stop();
		LOG(INFO) << StringPrintf("Time for %d patches calculation in seconds: %f", patchTable->GetNumPatchesTotal(), timer.TotalTime());

		timer.Reset();
		// Append endcap stencils
		std::shared_ptr<Far::StencilTable const> normalExtStencils;
		if (Far::StencilTable const *localPointStencilTable = patchTable->GetLocalPointStencilTable()) 
		{
			normalExtStencils = std::shared_ptr<Far::StencilTable const>(
				Far::StencilTableFactory::AppendLocalPointStencilTable(
					*refiner, normalStencils.get(), localPointStencilTable, true));
		}
		else
		{
			normalExtStencils = normalStencils;
		}
		timer.Stop();
		LOG(INFO) << StringPrintf("Time for %d local point stencils appendent in seconds: %f",
			normalExtStencils->GetNumStencils() - normalStencils->GetNumStencils(), timer.TotalTime());

		// Generate limit stencil table
		Far::PtexIndices ptexIndices(*refiner);
		int nfaces = ptexIndices.GetNumFaces();
		std::unique_ptr<float[]> u(new float[samplesPerFace*nfaces]);
		std::unique_ptr<float[]> v(new float[samplesPerFace*nfaces]);
		float* uPtr = u.get();
		float* vPtr = v.get();
		std::vector<LocationArray> locs(nfaces);
		srand(static_cast<int>(2147483647)); // use a large Pell prime number
		for (int face = 0; face < nfaces; ++face)
		{
			LocationArray& larray = locs[face];
			larray.ptexIdx = face;
			larray.numLocations = samplesPerFace;
			larray.s = uPtr;
			larray.t = vPtr;

			for (int j = 0; j < samplesPerFace; ++j, ++uPtr, ++vPtr)
			{
				*uPtr = (float)rand() / (float)RAND_MAX;
				*vPtr = (float)rand() / (float)RAND_MAX;
			}
		}

		timer.Reset();
		// Limit stencils contains only parameter points (samplesPerFace * nfaces).
		std::shared_ptr<Far::LimitStencilTable const> limitStencils(Far::LimitStencilTableFactory::Create(*refiner, locs, normalExtStencils.get(), patchTable.get()));
		timer.Stop();
		LOG(INFO) << StringPrintf("Time for %d limit stencils calculation in seconds: %f", limitStencils->GetNumStencils(), timer.TotalTime());

		// Create stencil output
		if (kernel == KernelType::kCPU)
		{
			stencilOutput = std::unique_ptr<StencilOutputBase>(new StencilOutputCPU(normalStencils, limitStencils, vertsNum, false));
		}
		else if (kernel == KernelType::kOPENMP)
		{
			stencilOutput = std::unique_ptr<StencilOutputBase>(new StencilOutputCPU(normalStencils, limitStencils, vertsNum, true));
		}
		/*else if (kernel == KernelType::kGLCompute)
		{
			std::shared_ptr<Osd::EvaluatorCacheT<Osd::GLComputeEvaluator>> cache(new Osd::EvaluatorCacheT<Osd::GLComputeEvaluator>());
			auto temp = std::unique_ptr<StencilOutputBase>(new StencilOutput<Osd::GLVertexBuffer,
				Osd::GLVertexBuffer,
				Osd::GLStencilTableSSBO,
				Osd::GLComputeEvaluator>(controlStencils, vertsNum,	cache));
			stencilOutput = std::move(temp);
		}*/
		else
		{
			LOG(FATAL) << "Unsupport kernel type for subdivision.";
		}

		LOG(INFO) << "Finish precomputation for mesh subdivision.";
	}

	void SubDivision::UpdateSrc(const float* positions)
	{
		stencilOutput->UpdateData(positions, 0, nVerts);
	}

	const float* SubDivision::EvaluateNormal(int& num)
	{
		stencilOutput->EvalStencilsNormal();
		num = stencilOutput->GetNumStencilsNormal();
		return stencilOutput->GetDstDataNormal();
	}

	const float* SubDivision::EvaluateLimit(int& num)
	{
		stencilOutput->EvalStencilsLimit();
		num = stencilOutput->GetNumStencilsLimit();
		return stencilOutput->GetDstDataLimit();
	}

}	// namespace handwork