// Analyze original mesh to get topology related data.

#pragma once

#include "../utility/utility.h"
#include <set>
#include <map>
#include "../utility/geometry.h"

namespace handwork
{
	struct SDFace;
	struct SDVertex;

	struct SDVertex 
	{
		// SDVertex Constructor
		SDVertex(const Vector3f &p = Vector3f(0, 0, 0)) : p(p) {}

		// SDVertex Methods
		int valence();
		void oneRing(Vector3f *p);
		Vector3f p;
		SDFace *startFace = nullptr;
		bool regular = false, boundary = false;
	};

	struct SDFace 
	{
		// SDFace Constructor
		SDFace() 
		{
			for (int i = 0; i < 3; ++i) 
			{
				v[i] = nullptr;
				f[i] = nullptr;
			}
		}

		// SDFace Methods
		int vnum(SDVertex *vert) const 
		{
			for (int i = 0; i < 3; ++i)
				if (v[i] == vert) return i;
			LOG(FATAL) << "Basic logic error in SDFace::vnum()";
			return -1;
		}

		inline int NEXT(int i) { return (i + 1) % 3; }
		inline int PREV(int i) { return (i + 2) % 3; }

		SDFace *nextFace(SDVertex *vert) { return f[vnum(vert)]; }
		SDFace *prevFace(SDVertex *vert) { return f[PREV(vnum(vert))]; }
		SDVertex *nextVert(SDVertex *vert) { return v[NEXT(vnum(vert))]; }
		SDVertex *prevVert(SDVertex *vert) { return v[PREV(vnum(vert))]; }
		SDVertex *otherVert(SDVertex *v0, SDVertex *v1)
		{
			for (int i = 0; i < 3; ++i)
				if (v[i] != v0 && v[i] != v1) return v[i];
			LOG(FATAL) << "Basic logic error in SDVertex::otherVert()";
			return nullptr;
		}

		SDVertex *v[3];
		SDFace *f[3];
	};


	class MeshTopology
	{
	public:
		MeshTopology(int nIndices, const int* vertexIndices, int nVertices, const Vector3f *p);
		SDVertex* GetVertices() { return verts.get(); }
		SDFace* GetFaces() { return fs.get(); }

	private:
		inline int NEXT(int i) { return (i + 1) % 3; }
		inline int PREV(int i) { return (i + 2) % 3; }

	private:
		std::unique_ptr<SDVertex[]> verts;
		std::unique_ptr<SDFace[]> fs;
		int nv;
		int nf;
	};

}	// namespace handwork
