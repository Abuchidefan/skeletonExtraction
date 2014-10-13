// Face.h : subor pre pracu s facetmi
#pragma once
#include "Vertex.h"
#include "CSDF.h"
#include <windows.h>
#include <stdio.h>
#include <assert.h>

namespace MeshStructures
{
	using namespace std;
	using namespace GenericStructures;
	using namespace SDFStructures;

	class Face
	{
	public:
		Face(Vertex* v1, Vertex* v2, Vertex* v3);
		~Face();

		void ComputeNormal();
		void SetColor(int color);
		void ComputeSDFValue(const std::vector<float> &values, const std::vector<float> &inverse_Yangles);
		LinkedList<Face>* GetSusedia();
		void ComputeSusedov();

		Vertex*					v[3];
		Vector4					normal;
		Vector4					center;
		int						farba;						// pre picking
		CSDF*					quality;
		unsigned int			number;						// cislo v zozname
		bool					checked;					// �i smne skontrolovali pri robeni susedov
	private:
		LinkedList<Face>* susedia;
		//const struct aiFace*	assimp_ref;
	};
}