// Do efficient mesh subdivison using OpenSubDiv library.

#pragma once

#include "../utility/utility.h"
#include "../utility/geometry.h"

namespace handwork
{
	enum class KernelType
	{
		kCPU = 0,
		kOPENMP
	};

	class StencilOutputBase
	{
	public:
		virtual ~StencilOutputBase() {}
		virtual void UpdateData(const float *src, int startVertex, int numVertices) = 0;
		virtual void EvalStencilsNormal() = 0;
		virtual void EvalStencilsLimit() = 0;
		virtual int GetNumStencilsNormal() const = 0;
		virtual int GetNumStencilsLimit() const = 0;
		virtual float* GetDstDataNormal() = 0;
		virtual float* GetDstDataLimit() = 0;
	};

	struct TopologyInfo
	{
		int VertsNum;
		int FacesNum;
		std::vector<int> Indices;
	};

	class SubDivision
	{
	public:
		SubDivision(int samples, KernelType type, int level, int vertsNum, int facesNum, int const* indices, bool leftHand = false);

		// Data format is [ P(xyz) ].
		void UpdateSrc(const float* positions);

		// Data format is [ P(xyz) ].
		const float* EvaluateNormal(int& num);

		// Data format is [ P(xyz), du(xyz), dv(xyz) ].
		const float* EvaluateLimit(int& num);

		int GetTopologyLevelNum() const { return (int)topologyInformation.size(); }

		const TopologyInfo* GetTopology(int level) { return &topologyInformation[level]; }

	private:
		int samplesPerFace = 2000;
		KernelType kernel = KernelType::kCPU;
		int isolationLevel = 2;	// max level of extraordinary feature isolation
		int nVerts = 0;
		std::unique_ptr<StencilOutputBase> stencilOutput;

		// Store the topology infomation
		std::vector<TopologyInfo> topologyInformation;
	};

}	// namespace handwork
