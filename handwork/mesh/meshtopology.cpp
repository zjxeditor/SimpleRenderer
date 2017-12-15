// Analyze original mesh to get topology related data.

#include "meshtopology.h"

namespace handwork
{
	// Local struct
	struct SDEdge
	{
		// SDEdge Constructor
		SDEdge(SDVertex *v0 = nullptr, SDVertex *v1 = nullptr)
		{
			v[0] = std::min(v0, v1);
			v[1] = std::max(v0, v1);
			f[0] = f[1] = nullptr;
			f0edgeNum = -1;
		}

		// SDEdge Comparison Function
		bool operator<(const SDEdge &e2) const
		{
			if (v[0] == e2.v[0]) return v[1] < e2.v[1];
			return v[0] < e2.v[0];
		}

		SDVertex *v[2];
		SDFace *f[2];
		int f0edgeNum;
	};

	int SDVertex::valence()
	{
		SDFace *f = startFace;
		if (!boundary)
		{
			// Compute valence of interior vertex
			int nf = 1;
			while ((f = f->nextFace(this)) != startFace) ++nf;
			return nf;
		}
		else
		{
			// Compute valence of boundary vertex
			int nf = 1;
			while ((f = f->nextFace(this)) != nullptr) ++nf;
			f = startFace;
			while ((f = f->prevFace(this)) != nullptr) ++nf;
			return nf + 1;
		}
	}

	void SDVertex::oneRing(Vector3f *p)
	{
		if (!boundary)
		{
			// Get one-ring vertices for interior vertex
			SDFace *face = startFace;
			do
			{
				*p++ = face->nextVert(this)->p;
				face = face->nextFace(this);
			} while (face != startFace);
		}
		else
		{
			// Get one-ring vertices for boundary vertex
			SDFace *face = startFace, *f2;
			while ((f2 = face->nextFace(this)) != nullptr) face = f2;
			*p++ = face->nextVert(this)->p;
			do
			{
				*p++ = face->prevVert(this)->p;
				face = face->prevFace(this);
			} while (face != nullptr);
		}
	}

	MeshTopology::MeshTopology(int nIndices, const int* vertexIndices, int nVertices, const Vector3f *p)
	{
		std::vector<SDVertex*> vertices;
		std::vector<SDFace*> faces;

		// Allocate vertices and faces
		verts = std::unique_ptr<SDVertex[]>(new SDVertex[nVertices]);
		for (int i = 0; i < nVertices; ++i) 
		{
			verts[i] = SDVertex(p[i]);
			vertices.push_back(&verts[i]);
		}
		int nFaces = nIndices / 3;
		fs = std::unique_ptr<SDFace[]>(new SDFace[nFaces]);
		for (int i = 0; i < nFaces; ++i) faces.push_back(&fs[i]);
		nv = nVertices;
		nf = nFaces;

		// Set face to vertex pointers
		const int *vp = vertexIndices;
		for (int i = 0; i < nFaces; ++i, vp += 3) 
		{
			SDFace *f = faces[i];
			for (int j = 0; j < 3; ++j) 
			{
				SDVertex *v = vertices[vp[j]];
				f->v[j] = v;
				v->startFace = f;
			}
		}

		// Set neighbor pointers in _faces_
		std::set<SDEdge> edges;
		for (int i = 0; i < nFaces; ++i) 
		{
			SDFace *f = faces[i];
			for (int edgeNum = 0; edgeNum < 3; ++edgeNum) 
			{
				// Update neighbor pointer for _edgeNum_
				int v0 = edgeNum, v1 = NEXT(edgeNum);
				SDEdge e(f->v[v0], f->v[v1]);
				if (edges.find(e) == edges.end()) 
				{
					// Handle new edge
					e.f[0] = f;
					e.f0edgeNum = edgeNum;
					edges.insert(e);
				}
				else 
				{
					// Handle previously seen edge
					e = *edges.find(e);
					e.f[0]->f[e.f0edgeNum] = f;
					f->f[edgeNum] = e.f[0];
					edges.erase(e);
				}
			}
		}

		// Finish vertex initialization
		for (int i = 0; i < nVertices; ++i) 
		{
			SDVertex *v = vertices[i];
			SDFace *f = v->startFace;
			do 
			{
				f = f->nextFace(v);
			} while (f && f != v->startFace);
			v->boundary = (f == nullptr);
			if (!v->boundary && v->valence() == 6)
				v->regular = true;
			else if (v->boundary && v->valence() == 4)
				v->regular = true;
			else
				v->regular = false;
		}
	}
	
}	// namespace handwork

