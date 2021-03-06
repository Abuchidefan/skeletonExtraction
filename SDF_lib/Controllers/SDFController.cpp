// SDFController.h : subor pre vypocitanie SDF funkcie
//#include "stdafx.h"

#define _USE_MATH_DEFINES

#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <cmath>

#include "SDFController.h"
#include "MathHelper.h"
#include "SDFOpenCL.h"
#include "mtrand.h"

#define FLOAT_MAX  99999.0f
#define SQRT_THREE 1.7320508075f
#define SQRT_TWO 1.4142135623f
#define FLD_NODE 0
#define BLD_NODE 1
#define FLT_NODE 2
#define BLT_NODE 3
#define FRD_NODE 4
#define BRD_NODE 5
#define FRT_NODE 6
#define BRT_NODE 7

namespace SDFController
{
	// konstruktor
	CSDFController::CSDFController(float dia, CAssimp* logg)
	{
		diagonal = dia;
		loggger = logg;
		prealocated_space = 100;
		/*fc_list = new LinkedList<Face>();
		fc_list->Preallocate(prealocated_space);*/
		fc_list = new HashTable<Face>(prealocated_space);
		oc_list = new LinkedList<Octree>();
		oc_list->Preallocate(100);
		InitKernel();
		//loggger->logInfo(MarshalString("diagonal: " + diagonal));
	}

	// destruktor
	CSDFController::~CSDFController()
	{
		delete fc_list;
		delete oc_list;

		EraseKernel();
	}
		
	// pocitanie funkcie pre vsetky trojuholniky, O(n2)
	void CSDFController::Compute(LinkedList<Face>* triangles, Octree* root, Vector4 o_min, Vector4 o_max)
	{
		int ticks1 = GetTickCount();
		float min = FLOAT_MAX;
		float max = 0.0;
		
		const unsigned int n_rays = Nastavenia->SDF_Rays;
		unsigned int counter = 0;

		//------------------prealocated variables------------------
		Vector4 tangens, normal, binormal;
		Mat4 t_mat;
		std::vector<float> rays;
		std::vector<float> weights;

		float* rndy = new float[n_rays];
		float* rndx = new float[n_rays];
		if(Nastavenia->SDF_Distribution == true)
			UniformPointsOnSphere(rndx, rndy);
		else
			RandomPointsOnSphere(rndx, rndy);

		for(unsigned int i = 0; i < n_rays; i++)
		{
			weights.push_back(180.0f - rndy[i]);
		}

		float dist = FLOAT_MAX;
		float dist2 = FLOAT_MAX;
		float theta = 0.0f;
		bool intersected = false;

		HashTable<Face>* face_list = NULL;
		Face** intersected_face = NULL;
		//------------------prealocated variables------------------

		LinkedList<Face>::Cell<Face>* current_face = triangles->start;
		int min_tri = 99999;
		int max_tri = 0;
		float avg = 0;
		float vypis_pocet = 0.0f;
		float total_pocet = (float)triangles->GetSize();
		while(current_face != NULL)
		{
			vypis_pocet = vypis_pocet + 1;
			// vypocet TNB vektorov a matice
			ComputeTNB(current_face->data, tangens, binormal, normal);
			t_mat = Mat4(tangens, normal, binormal);

			rays.clear();
			Nastavenia->SDF_STATUS = (unsigned int)((vypis_pocet / total_pocet) * 100.0f);
			for(unsigned int i = 0; i < n_rays; i++)
			{
				unsigned int repeat = 1;
				if(Nastavenia->SDF_Revert_Rays == true)
					repeat = 2;
				
				Vector4 ray = CalcRayFromAngle(rndx[i], rndy[i]) * t_mat;
				ray.Normalize();
				ray = ray * (-1.0f);

				dist = FLOAT_MAX;
				while(repeat > 0)
				{
					repeat--;
					ray = ray * (-1.0f);
					fc_list->Clear();
					oc_list->Clear();
					//fc_list->InsertToStart(current_face->data);
					fc_list->Insert(current_face->data);
					face_list = GetFaceList(triangles, root, current_face->data->center, ray, o_min, o_max);
					face_list->Delete(current_face->data);

					int sizi = face_list->GetSize();
					if(sizi < min_tri)
						min_tri = sizi;
					if(sizi > max_tri)
						max_tri = sizi;
					avg += sizi;

					intersected_face = face_list->start;
					if(face_list->GetSize() > 0)
					{
						for(unsigned int idx = 0; idx < face_list->prealocated; idx++)
						{				
							if(intersected_face[idx] == NULL)
								continue;
							dist2 = FLOAT_MAX;
							intersected = rayIntersectsTriangle(current_face->data->center, ray, intersected_face[idx]->v[0]->P, intersected_face[idx]->v[1]->P, intersected_face[idx]->v[2]->P, dist2);
							if(intersected == true)
							{
								if(Nastavenia->SDF_Revert_Rays == true)
								{
									if((dist2 > 0.0001f) && (dist2 < dist))
										dist = dist2;
								}
								else
								{
									theta = acos( (ray * intersected_face[idx]->normal) / (ray.Length() * intersected_face[idx]->normal.Length()) );
									theta = theta * float(180.0 / M_PI);
									//loggger->logInfo(MarshalString("pridany ray s thetou: " + theta));
									if((theta < 90.0f) && (dist2 > 0.0001f) && (dist2 < dist))
										dist = dist2;
								}
							}
							//intersected_face = intersected_face->next;
						}
					}
				}
				//if(dist < (FLOAT_MAX - 1.0f))
				{
					//loggger->logInfo(MarshalString("pridany ray s dlzkou: " + dist));
					if(dist >= (FLOAT_MAX - 1.0f))
						dist = 0;
					rays.push_back(dist);
				}
				//if(root != NULL)
					//delete face_list;						// generated list, bez prealokovania
			}
			if(rays.size() > 0)
			{
				current_face->data->ComputeSDFValue(rays, weights);
				if(current_face->data->quality->value < min)
					min = current_face->data->quality->value;
				if(current_face->data->quality->value > max)
					max = current_face->data->quality->value;
			}
			counter = counter + 1;
			current_face = current_face->next;
		}
		fc_list->Clear();
		oc_list->Clear();
		delete [] rndy;
		delete [] rndx;

		int ticks2 = GetTickCount();

		if(Nastavenia->SDF_Smooth_Projected == true)
			DoSmoothing2(triangles, min, max);
		else
			DoSmoothing(triangles, min, max);

		int ticks3 = GetTickCount();

		/*loggger->logInfo(MarshalString("SDF vypocitane v case: " + (ticks2 - ticks1)+ "ms"));
		loggger->logInfo(MarshalString("SDF hodnoty vyhladene v case: " + (ticks3 - ticks2)+ "ms"));
		loggger->logInfo(MarshalString("Celkovy vypocet trval: " + (ticks3 - ticks1)+ "ms, pre " + counter + " trojuholnikov"));
		//loggger->logInfo(MarshalString("pocet: " + counter));
		//loggger->logInfo(MarshalString("min a max pre SDF su: " + min + ", "+max));
		//loggger->logInfo(MarshalString("nmin a nmax pre SDF su: " + nmin + ", "+nmax));
		avg = avg / (triangles->GetSize() * n_rays);
		loggger->logInfo(MarshalString("min trianglov na luc: " + min_tri));
		loggger->logInfo(MarshalString("max trianglov na luc: " + max_tri));
		loggger->logInfo(MarshalString("avg trianglov na luc: " + avg));*/
	}

	// pocitanie funkcie pre vsetky trojuholniky v OpenCL
	void CSDFController::ComputeOpenCL(LinkedList<Vertex>* points, LinkedList<Face>* triangles, Octree* root, Vector4 o_min, Vector4 o_max)
	{
		using namespace OpenCLForm;
		int ticks1 = GetTickCount();

		//-------------------------------------------
		//---------------INIT OpenCL-------Begin-----
		//-------------------------------------------
		COpenCL* OpenCLko = new COpenCL();

		int err = EXIT_SUCCESS;
		err = OpenCLko->InitOpenCL();
		if(!CheckError(err)) return;

		err = OpenCLko->LoadKernel1((Nastavenia->CLKernelPath + string("sdf.cl")).c_str());
		if(!CheckError(err)) return;

		err = OpenCLko->BuildKernel1();
		if(!CheckError(err, OpenCLko->debug_buffer)) return;

		err = OpenCLko->LoadKernel2((Nastavenia->CLKernelPath + string("proces.cl")).c_str());
		if(!CheckError(err)) return;

		err = OpenCLko->BuildKernel2();
		if(!CheckError(err, OpenCLko->debug_buffer)) return;

		err = OpenCLko->LoadKernel3((Nastavenia->CLKernelPath + string("smooth.cl")).c_str());
		if(!CheckError(err)) return;

		err = OpenCLko->BuildKernel3();
		if(!CheckError(err, OpenCLko->debug_buffer)) return;

		err = OpenCLko->GetGPUVariables();
		if(!CheckError(err)) return;

		//-------------------------------------------
		//---------------INIT OpenCL-------End-------
		//-------------------------------------------

		// IMPORTANT!! Variables for memory allocation
		size_t n_workitems = (size_t)(OpenCLko->num_1D_work_items * Nastavenia->GPU_Work_Items);
		if(OpenCLko->num_ND_work_items[0] < n_workitems)
			n_workitems = (size_t)(OpenCLko->num_ND_work_items[0] * Nastavenia->GPU_Work_Items);

		size_t n_workgroups = (size_t)(OpenCLko->num_cores * Nastavenia->GPU_Work_Groups);
		
		OpenCLko->global = n_workgroups * n_workitems;
		OpenCLko->local = n_workitems;

		const unsigned int n_rays = Nastavenia->SDF_Rays;
		const unsigned int n_prealloc = prealocated_space;

		unsigned int n_triangles = triangles->GetSize();
		unsigned int n_vertices = points->GetSize();

		unsigned int n_rays_per_kernel = n_workitems * n_workgroups;
		unsigned int n_triangles_per_kernel = (unsigned int)(n_rays_per_kernel / n_rays);
		unsigned int n_triangles_at_end = n_triangles % n_triangles_per_kernel;
		unsigned int n_rays_at_end = n_triangles_at_end * n_rays;

		unsigned int n_kernels = (unsigned int)(n_triangles / n_triangles_per_kernel);
		if(n_triangles_at_end > 0) n_kernels++;

		//-------------------------------------------
		//---------------Memory Alloc------Begin-----
		//-------------------------------------------

		cl_uint3	*c_triangles;			// zoznam trojuholnikov obsahujucich IDX 3 vertexov
		cl_float4	*c_vertices;			// zoznam vertexov obsahujuci ich poziciu
		cl_uint4	c_params;				// potrebujem vediet n_workitems, n_rays, n_prealloc, max pocet lucov
		cl_uint		**c_origins;			// zoznamy trojuholnikov na strielanie lucov, ktore sa postupne vkladaju do OpenCL
		cl_float4	**c_rays;				// zoznamy lucov, ktore sa postupne vkladaju do OpenCL
		cl_uint		**c_targets;			// zoznamy trojuholnikov na kontrolu, ktore sa postupne vkladaju do OpenCL
		cl_float	**c_outputs;			// vzdialenost a vaha pre kazdy luc, ktore je mojim vysledkom co si zapisem

		unsigned int s_triangles = n_triangles * sizeof(cl_uint3);									// pocet trojuholnikov * 4 (nie 3!) * int
		unsigned int s_vertices = n_vertices * sizeof(cl_float4);									// pocet vertexov * 4 * float
		unsigned int s_origins = n_triangles_per_kernel * sizeof(cl_uint);							// trojuholniky pre kernel * int
		unsigned int s_rays = n_triangles_per_kernel * n_rays * sizeof(cl_float4);					// trojuholniky pre kernel * 30 * 4 * float
		unsigned int s_targets = n_triangles_per_kernel * n_rays * n_prealloc * sizeof(cl_uint);	// trojuholniky pre kernel * 30 * 100 * int
		unsigned int s_outputs = n_triangles_per_kernel * n_rays * sizeof(cl_float);				// trojuholniky pre kernel * 30  * float

		int ticks2 = GetTickCount();
		err = OpenCLko->SetupMemory(s_triangles, s_vertices, s_origins, s_rays, s_targets, s_outputs);
		if(!CheckError(err)) return;
		int ticks3 = GetTickCount();

		c_triangles = (cl_uint3*) malloc(s_triangles);
		c_vertices = (cl_float4*) malloc(s_vertices);
		c_origins = (cl_uint**) calloc(n_kernels, sizeof(cl_uint*));
		c_rays = (cl_float4**) calloc(n_kernels, sizeof(cl_float4*));
		c_targets = (cl_uint**) calloc(n_kernels, sizeof(cl_uint*));
		c_outputs = (cl_float**) calloc(n_kernels, sizeof(cl_float*));

		for(unsigned int i = 0; i < n_kernels; i++)
		{
			c_origins[i] = (cl_uint*) calloc(s_origins, 1);
			c_rays[i] = (cl_float4*) calloc(s_rays, 1);
			c_targets[i] = (cl_uint*) calloc(s_targets, 1);
			c_outputs[i] = (cl_float*) calloc(s_outputs, 1);
		}
		//-------------------------------------------
		//---------------Memory Alloc------End-------
		//-------------------------------------------

		//------------------prealocated variables------------------	
		std::vector<float> weights;

		Vector4 tangens, normal, binormal;
		Mat4 t_mat;

		float* rndy = new float[n_rays];
		float* rndx = new float[n_rays];
		if(Nastavenia->SDF_Distribution == true)
			UniformPointsOnSphere(rndx, rndy);
		else
			RandomPointsOnSphere(rndx, rndy);

		for(unsigned int i = 0; i < Nastavenia->SDF_Rays; i++)
		{
			weights.push_back(180.0f - rndy[i]);
		}

		//------------------prealocated variables------------------

		// -----------------------------------------------
		// vypocitaj dopredu zoznamy trojuholnikov a lucov
		// -----------------------------------------------
		int ticks4 = GetTickCount();
		//---------------copy variables--------------
		unsigned int pocet = 0;
		LinkedList<Face>::Cell<Face>* tmp_face = triangles->start;
		while(tmp_face != NULL)
		{
			c_triangles[pocet].s[0] = tmp_face->data->v[0]->number;
			c_triangles[pocet].s[1] = tmp_face->data->v[1]->number;
			c_triangles[pocet].s[2] = tmp_face->data->v[2]->number;
			tmp_face = tmp_face->next;
			pocet++;
		}

		pocet = 0;
		LinkedList<Vertex>::Cell<Vertex>* tmp_points = points->start;
		while(tmp_points != NULL)
		{
			c_vertices[pocet].s[0] = tmp_points->data->P.X;
			c_vertices[pocet].s[1] = tmp_points->data->P.Y;
			c_vertices[pocet].s[2] = tmp_points->data->P.Z;
			c_vertices[pocet].s[3] = tmp_points->data->P.W;
			tmp_points = tmp_points->next;
			pocet++;
		}
		//---------------copy variables--------------
		int ticks5 = GetTickCount();
		pocet = 0;
		LinkedList<Face>::Cell<Face>* current_face = triangles->start;
		Face** tmp_faces = NULL;
		while(current_face != NULL)
		{
			unsigned int kernel_num = (unsigned int)(pocet / n_triangles_per_kernel);
			unsigned int in_kernel_num = (unsigned int)(pocet % n_triangles_per_kernel);

			// TODO: zjednodusit
			c_origins[kernel_num][in_kernel_num] = pocet;

			// vypocet TNB vektorov a matice
			ComputeTNB(current_face->data, tangens, binormal, normal);
			t_mat = Mat4(tangens, normal, binormal);

			for(unsigned int i = 0; i < n_rays; i++)
			{
				Vector4 ray = CalcRayFromAngle(rndx[i], rndy[i]) * t_mat;
				ray.Normalize();

				c_rays[kernel_num][i+(n_rays*in_kernel_num)].s[0] = ray.X;
				c_rays[kernel_num][i+(n_rays*in_kernel_num)].s[1] = ray.Y;
				c_rays[kernel_num][i+(n_rays*in_kernel_num)].s[2] = ray.Z;
				c_rays[kernel_num][i+(n_rays*in_kernel_num)].s[3] = ray.W;

				fc_list->Clear();
				oc_list->Clear();
				//fc_list->InsertToStart(current_face->data);
				fc_list->Insert(current_face->data);
				HashTable<Face>* face_list = GetFaceList(triangles, root, current_face->data->center, ray, o_min, o_max);
				face_list->Delete(current_face->data);

				tmp_faces = face_list->start;
				unsigned int c_pocet = 0;
				if(face_list->GetSize() > 0)
				{
					for(unsigned int idx = 0; idx < face_list->prealocated; idx++)
					{	
						if(tmp_faces[idx] != NULL)
						{
							c_targets[kernel_num][c_pocet+(i+(n_rays*in_kernel_num))*n_prealloc] = tmp_faces[idx]->number + 1;		// naschval posunute
							//tmp_face = tmp_face->next;
							c_pocet++;
							if(c_pocet == prealocated_space) break;
						}
					}
				}
			}
			pocet = pocet + 1;
			current_face = current_face->next;
		}
		fc_list->Clear();
		oc_list->Clear();
		delete [] rndy;
		delete [] rndx;
		// -----------------------------------------------
		// vypocitaj dopredu zoznamy trojuholnikov a lucov
		// -----------------------------------------------


		// v tomto bode je uz pamet pripravena a nacitana
		// je nutne poslat ju do OpenCL a zahajit vypocet
		int ticks6 = GetTickCount();
		unsigned int pocet_trojuholnikov = n_triangles_per_kernel;
		for(unsigned int i = 0; i < n_kernels; i++)
		{
			if(i == (n_kernels-1)) 
				pocet_trojuholnikov = n_triangles_at_end;
			c_params.s[0] = n_workitems; c_params.s[1] = n_rays; c_params.s[2] = n_prealloc; c_params.s[3] = pocet_trojuholnikov;
			err = OpenCLko->LaunchKernel(c_triangles, c_vertices, c_origins[i], c_rays[i], c_targets[i], c_outputs[i], c_params);
			if(!CheckError(err)) return;
		}
		OpenCLko->WaitForFinish();

		int ticks7 = GetTickCount();

		// spracuj ziskane hodnoty
		float min = FLOAT_MAX;
		float max = 0.0;
		unsigned int cpocet = 0;
		pocet_trojuholnikov = n_triangles_per_kernel;
		std::vector<float> rays;
		std::vector<float> weightsx;
		current_face = triangles->start;
		for(unsigned int i = 0; i < n_kernels; i++)
		{
			if(i == (n_kernels-1)) 
				pocet_trojuholnikov = n_triangles_at_end;

			for(unsigned int j = 0; j < pocet_trojuholnikov; j++)
			{
				for(unsigned int k = 0; k < n_rays; k++)
				{
					float dist = c_outputs[i][k+j*n_rays];
					//loggger->logInfo(MarshalString("triangle: "+(i*n_triangles_per_kernel + j)+ " val: " + dist));
					if(dist < (FLOAT_MAX - 1.0f))
					{
						rays.push_back(c_outputs[i][k+j*n_rays]);
						weightsx.push_back(weights[k]);
					}
				}
				if(rays.size() > 0)
				{
					current_face->data->ComputeSDFValue(rays, weightsx);
					if(current_face->data->quality->value < min)
						min = current_face->data->quality->value;
					if(current_face->data->quality->value > max)
						max = current_face->data->quality->value;
				}
				cpocet++;
				current_face = current_face->next;
				rays.clear();
				weightsx.clear();
			}
		}

		int ticks8 = GetTickCount();

		if(Nastavenia->SDF_Smooth_Projected == true)
			DoSmoothing2(triangles, min, max);
		else
			DoSmoothing(triangles, min, max);

		int ticks9 = GetTickCount();


		/*loggger->logInfo(MarshalString("Inicializacia OpenCL: " + (ticks2 - ticks1)+ "ms"));
		loggger->logInfo(MarshalString("Alokovanie pamete v OpenCL: " + (ticks3 - ticks2)+ "ms"));
		loggger->logInfo(MarshalString("Alokovanie pamete v PC: " + (ticks4 - ticks3)+ "ms"));
		loggger->logInfo(MarshalString("Nacitanie zoznamu trojuholnikov: " + (ticks5 - ticks4)+ "ms"));
		loggger->logInfo(MarshalString("Prehladavanie Octree: " + (ticks6 - ticks5)+ "ms"));
		loggger->logInfo(MarshalString("!!VYPOCET OpenCL!!: " + (ticks7 - ticks6)+ "ms"));
		loggger->logInfo(MarshalString("Spracovanie: " + (ticks8 - ticks7)+ "ms"));
		loggger->logInfo(MarshalString("Smoothing: " + (ticks9 - ticks8)+ "ms"));
		loggger->logInfo(MarshalString("Celkovy vypocet trval: " + (ticks9 - ticks1)+ "ms, pre " + pocet + " trojuholnikov"));*/
		//loggger->logInfo(MarshalString("pocet: " + pocet));
		//loggger->logInfo(MarshalString("min a max pre SDF su: " + min + ", "+max));
		//loggger->logInfo(MarshalString("nmin a nmax pre SDF su: " + nmin + ", "+nmax));


		// Delete OpenCL to free GPU
		delete OpenCLko;

		// Free host memory
		for(unsigned int i = 0; i < n_kernels; i++)
		{
			free(c_origins[i]);
			free(c_rays[i]);
			free(c_targets[i]);
			free(c_outputs[i]);
		}
		free(c_triangles);
		free(c_vertices);
		free(c_origins);
		free(c_rays);
		free(c_targets);
		free(c_outputs);
	}

	// vypocitaj normalizovany 1D kernel pre gaussian
	float* CSDFController::ComputeGaussianKernel(int radius)
	{
		float* matrix = new float [radius*2+1];
		if(radius == 0)
		{
			matrix[0] = 1.0f;
			return matrix;
		}
		float sigma = (float)radius/2.0f;
		float norm = 1.0f / float(sqrt(2*M_PI) * sigma);
		float coeff = 2*sigma*sigma;
		float total=0;
		for(int x = -radius; x <= radius; x++)
		{
			float g = norm * exp( (-x*x)/coeff );
			matrix[x+radius] = g;
			total+=g;
		}
		for(int x=0; x<=2*radius; x++)
			matrix[x]=(matrix[x]/total) * 1000.0f;

		return matrix;
	}

	// vypocitaj gaussian hodnotu pre dane x
	float CSDFController::ComputeGaussian(int radius, float val, float maxval)
	{
		if(radius == 0)
		{
			return 1.0f;
		}
		float sigma = (float)radius/2.0f;
		float norm = 1.0f / float(sqrt(2*M_PI) * sigma);
		float coeff = 2*sigma*sigma;

		float num = min((val / maxval), 1.0f);
		float g = norm * exp( (-num*num)/coeff)  *100.0f;

		return g;
	}

	void CSDFController::Smooth(Face* tmp, float* kernel, int kernel_size)
	{
		gauss_sus[0]->InsertToEnd(tmp);
		tmp->checked = true;
		for(int i=1; i <= kernel_size; i++)				// bacha na posunutie
		{
			//gauss_sus[i] = new LinkedList<Face>();	// prealokovane
			LinkedList<Face>::Cell<Face>* tm = gauss_sus[i-1]->start;
			while(tm != NULL)
			{
				LinkedList<Face>* t = tm->data->GetSusedia();
				LinkedList<Face>::Cell<Face>* tc = t->start;
				while(tc != NULL)
				{
					if(tc->data->checked == true)
					{
						tc = tc->next;
						continue;
					}
					gauss_sus[i]->InsertToEnd(tc->data);
					tc->data->checked = true;
					tc = tc->next;
				}
				tm = tm->next;
			}
		}
		std::vector<float> _values;
		std::vector<float> _weights;

		for(int i=0; i <= kernel_size; i++)
		{
			int _size = gauss_sus[i]->GetSize();
			if(_size != 0)
			{
				float _weight = kernel[kernel_size + i];// / (float)_size;
				LinkedList<Face>::Cell<Face>* tc = gauss_sus[i]->start;
				while(tc != NULL)
				{
					_values.push_back(tc->data->quality->value);
					_weights.push_back(_weight);
					tc->data->checked = false;
					tc = tc->next;
				}
			}
			gauss_sus[i]->Clear();
		}
		//delete [] gauss_sus;

		tmp->quality->Smooth(_values, _weights);
	}

	void CSDFController::Smooth2(PPoint* pointik, ROctree* m_root, LinkedList<ROctree>* ro_list, unsigned int poradie)
	{
		float maxval = pointik->ref->quality->value * 0.5f;
		RadiusSearch2(pointik->P, maxval, m_root, ro_list);

		float weight = 0.0f;//ComputeGaussian(Nastavenia->SDF_Smoothing_Radius, 0, maxval) * 5.0f;
		float sum_values = 0.0f;//pointik->ref->quality->value * weight;
		//Vector4 sum_position = Vector4();
		float sum_weights = 0.0f;//weight;


		LinkedList<ROctree>::Cell<ROctree>* tmp = ro_list->start;
		while(tmp != NULL)
		{
			weight = (float)tmp->data->count;
			//weight = 1.0;

			float cube_radius = tmp->data->size * SQRT_THREE;
			float distanc_act = pointik->P.Dist(tmp->data->origin) - cube_radius - maxval;
			if(distanc_act < 0.0f)
			{
				distanc_act = -distanc_act;
				float pct = (distanc_act / (cube_radius + cube_radius)) * 100.0f;	// kolko % je vnutri
				if(pct > 100.0f)									// nesmu byt priliz velke
					pct = 100.0f;
				weight *= pct;
			}

			//weight = (float)(Nastavenia->OCTREE_Depth + 1 - tmp->data->depth) ;
			//weight = ComputeGaussian(Nastavenia->SDF_Smoothing_Radius, distanc, maxval);
			sum_values += tmp->data->value * weight;
			//sum_position = sum_position + (tmp->data->value_center * weight);
			sum_weights += weight;

			tmp = tmp->next;
		}
		/*sum_values = (sum_values / sum_weights) * 2.0f;
		sum_values += pointik->ref->quality->value;
		sum_values = sum_values / 3.0f;*/
		sum_weights += 4;
		sum_values += pointik->ref->quality->value * 4;
		//sum_position = sum_position + (pointik->P * 4);
		sum_values = sum_values / sum_weights;
		//sum_position = sum_position / sum_weights;

		pointik->diameter = sum_values;
		/*pointik->diameter = sum_position.Dist(pointik->ref->center) * 2.0f;
		pointik->ref->normal = pointik->ref->center - sum_position;
		pointik->ref->normal.Normalize();*/
	}

	HashTable<Face>* CSDFController::GetFaceList(LinkedList<Face>* triangles, Octree* root, Vector4 center, Vector4 ray, Vector4 o_min, Vector4 o_max)
	{
		/*if (root == NULL)
			return triangles;*/
		if (root == NULL)
			return NULL;

		LinkedList<Octree>* octrees = oc_list;
		HashTable<Face>* faces = fc_list;
		//octrees->Clear();
		//faces->Clear();
		//center = center - (ray * diagonal);						// hack

		ray_octree_traversal(root, ray, center, octrees, o_min, o_max);
		//ray_octree_traversal2(root, ray, center, octrees);

		// create triangle list
		LinkedList<Octree>::Cell<Octree>* tmp = octrees->start;
		while(tmp != NULL)
		{
			for(unsigned int i = 0; i < tmp->data->count; i++)
			{
				if(faces->GetSize() >= prealocated_space)
					break;
				//if(!faces->Contains(tmp->data->triangles[i]))
				faces->Insert(tmp->data->triangles[i]);
			}
			if(faces->GetSize() >= prealocated_space)
				break;
			tmp = tmp->next;
		}
		//delete octrees;								// bez prealokovania
		return faces;
	}

	void CSDFController::ComputeTNB(Face* tmp, Vector4& tang, Vector4& binor, Vector4& norm)
	{
		// compute tanget space matrix
		/*Vector4 U = Vector4(tmp->v[1]->P - tmp->v[0]->P);
		Vector4 V = Vector4(tmp->v[2]->P - tmp->v[0]->P);
		norm = (U % V) * (-1.0);
		norm.Normalize();*/
		norm = tmp->normal * (-1.0);

		tang = Vector4(tmp->v[0]->P - tmp->v[2]->P);
		tang.Normalize();
		binor = tang % norm;
		binor.Normalize();
	}

	int CSDFController::first_node(float tx0, float ty0, float tz0, float txm, float tym, float tzm)
	{
		unsigned char answer = 0;   // initialize to 00000000
		// select the entry plane and set bits
		if(tx0 > ty0)
		{
			if(tx0 > tz0)					// PLANE YZ
			{
				if(tym < tx0) answer|=2;    // set bit at position 1
				if(tzm < tx0) answer|=1;    // set bit at position 0
				return (int) answer;
			}  
		} else
		{      
			if(ty0 > tz0)					// PLANE XZ
			{
				if(txm < ty0) answer|=4;    // set bit at position 2
				if(tzm < ty0) answer|=1;    // set bit at position 0
				return (int) answer;
			}
		}
											// PLANE XY
		if(txm < tz0) answer|=4;			// set bit at position 2
		if(tym < tz0) answer|=2;			// set bit at position 1
		return (int) answer;
	}

	int CSDFController::first_node2(Vector4 t0, Vector4 tm)
	{
		unsigned char answer = 0;   // initialize to 00000000
		// select the entry plane and set bits
		if(t0.X > t0.Y)
		{
			if(t0.X > t0.Z)					// PLANE YZ
			{
				if(tm.Y < t0.X) answer|=2;    // set bit at position 1
				if(tm.Z < t0.X) answer|=1;    // set bit at position 0
				return (int) answer;
			}  
		} else
		{      
			if(t0.Y > t0.Z)					// PLANE XZ
			{
				if(tm.X < t0.Y) answer|=4;    // set bit at position 2
				if(tm.Z < t0.Y) answer|=1;    // set bit at position 0
				return (int) answer;
			}
		}
		// PLANE XY
		if(tm.X < t0.Z) answer|=4;			// set bit at position 2
		if(tm.Y < t0.Z) answer|=2;			// set bit at position 1
		return (int) answer;
	}

	int CSDFController::new_node(float txm, int x, float tym, int y, float tzm, int z)
	{
		if(txm < tym)
		{
			if(txm < tzm) return x;			// YZ plane
		}
		else
		{
			if(tym < tzm) return y;			// XZ plane
		}
		return z;							// XY plane;
	}

	void CSDFController::proc_subtree (unsigned char idx, float tx0, float ty0, float tz0, float tx1, float ty1, float tz1, Octree* node, LinkedList<Octree>* octrees)
	{
		if(node == NULL)
			return;

		float txm, tym, tzm;
		int currNode;

		if((tx1 < 0.0) || (ty1 < 0.0) || (tz1 < 0.0)) return;

		if(node->isLeaf)
		{
			//loggger->logInfo("Reached leaf node");
			if(node->count > 0)
				octrees->InsertToEnd(node);
			return;
		}
		
		txm = 0.5f*(tx0 + tx1);
		tym = 0.5f*(ty0 + ty1);
		tzm = 0.5f*(tz0 + tz1);
		currNode = first_node(tx0,ty0,tz0,txm,tym,tzm);
		do{
			switch (currNode)
			{
			case 0: {
				proc_subtree(idx, tx0,ty0,tz0,txm,tym,tzm,node->son[idx], octrees);
				currNode = new_node(txm,4,tym,2,tzm,1);
				break;}
			case 1: {
				proc_subtree(idx, tx0,ty0,tzm,txm,tym,tz1,node->son[1^idx], octrees);
				currNode = new_node(txm,5,tym,3,tz1,8);
				break;}
			case 2: {
				proc_subtree(idx, tx0,tym,tz0,txm,ty1,tzm,node->son[2^idx], octrees);
				currNode = new_node(txm,6,ty1,8,tzm,3);
				break;}
			case 3: {
				proc_subtree(idx, tx0,tym,tzm,txm,ty1,tz1,node->son[3^idx], octrees);
				currNode = new_node(txm,7,ty1,8,tz1,8);
				break;}
			case 4: {
				proc_subtree(idx, txm,ty0,tz0,tx1,tym,tzm,node->son[4^idx], octrees);
				currNode = new_node(tx1,8,tym,6,tzm,5);
				break;}
			case 5: {
				proc_subtree(idx, txm,ty0,tzm,tx1,tym,tz1,node->son[5^idx], octrees);
				currNode = new_node(tx1,8,tym,7,tz1,8);
				break;}
			case 6: {
				proc_subtree(idx, txm,tym,tz0,tx1,ty1,tzm,node->son[6^idx], octrees);
				currNode = new_node(tx1,8,ty1,8,tzm,7);
				break;}
			case 7: {
				proc_subtree(idx, txm,tym,tzm,tx1,ty1,tz1,node->son[7^idx], octrees);
				currNode = 8;
				break;}
			}
		} while (currNode < 8);
	}

	void CSDFController::ray_octree_traversal(Octree* octree, Vector4 ray, Vector4 Center, LinkedList<Octree>* octrees, Vector4 o_min, Vector4 o_max)
	{
		unsigned char idx = 0;

		// avoid division by zero
		float Bias = 0.0001f;
		if (fabs(ray.X) < Bias) ray.X = ray.X < 0.0 ? -Bias : Bias;
		if (fabs(ray.Y) < Bias) ray.Y = ray.Y < 0.0 ? -Bias : Bias;
		if (fabs(ray.Z) < Bias) ray.Z = ray.Z < 0.0 ? -Bias : Bias;

		float invdirx = 1.0f / fabs(ray.X);
		float invdiry = 1.0f / fabs(ray.Y);
		float invdirz = 1.0f / fabs(ray.Z);

		float tx0 ,tx1, ty0, ty1, tz0, tz1;

		// fixes for rays with negative direction
		if(ray.X < 0.0)
		{
			tx0 = (o_max.X - Center.X) * -invdirx;
			tx1 = (o_min.X - Center.X) * -invdirx;
			idx |= 4 ; //bitwise OR (latest bits are XYZ)
		}
		else
		{
			tx0 = (o_min.X - Center.X) * invdirx;
			tx1 = (o_max.X - Center.X) * invdirx;
		}
		if(ray.Y < 0.0)
		{
			ty0 = (o_max.Y - Center.Y) * -invdiry;
			ty1 = (o_min.Y - Center.Y) * -invdiry;
			idx |= 2 ;
		}
		else
		{
			ty0 = (o_min.Y - Center.Y) * invdiry;
			ty1 = (o_max.Y - Center.Y) * invdiry;
		}
		if(ray.Z < 0.0)
		{
			tz0 = (o_max.Z - Center.Z) * -invdirz;
			tz1 = (o_min.Z - Center.Z) * -invdirz;
			idx |= 1 ;
		}
		else
		{
			tz0 = (o_min.Z - Center.Z) * invdirz;
			tz1 = (o_max.Z - Center.Z) * invdirz;
		}

		if( max(max(tx0,ty0),tz0) < min(min(tx1,ty1),tz1) )
		{ 
			proc_subtree(idx, tx0,ty0,tz0,tx1,ty1,tz1,octree, octrees);
//			proc_subtree4(idx, Vector4(tx0,ty0,tz0),Vector4(tx1,ty1,tz1),octree, octrees);
		}
	}
	void CSDFController::ray_octree_traversal2(Octree* octree, Vector4 ray, Vector4 Center, LinkedList<Octree>* octrees)
	{
		proc_subtree3(Center,Center,ray,octree, octrees);
	}

	struct ActualState
	{
		float tx0;
		float tx1;
		float ty0;
		float ty1;
		float tz0;
		float tz1;
	};
	void CSDFController::proc_subtree2 (unsigned char idx, float tx0, float ty0, float tz0, float tx1, float ty1, float tz1, Octree* node, LinkedList<Octree>* octrees)
	{
		if(node == NULL)
			return;

		if((tx1 < 0.0) || (ty1 < 0.0) || (tz1 < 0.0)) return;

		if(node->isLeaf)
		{
			//loggger->logInfo("Reached leaf node");
			octrees->InsertToEnd(node);
			return;
		}

		int currNode = 0;
		float txm, tym, tzm;
	
		ActualState state;
		ActualState newState;

		state.tx0 = tx0; state.ty0 = ty0; state.tz0 = tz0;
		state.tx1 = tx1; state.ty1 = ty1; state.tz1 = tz1;	

		int stateLookUp = 0;

		txm = 0.5f * (state.tx0 + state.tx1);
		tym = 0.5f * (state.ty0 + state.ty1);
		tzm = 0.5f * (state.tz0 + state.tz1);

		currNode = first_node(state.tx0, state.ty0, state.tz0, txm, tym, tzm);
		do 
		{
			int case1 = 8;
			int case2 = 8;
			int case3 = 8;		
			switch (currNode)
			{
				case FLD_NODE:
					newState.tx0 = state.tx0; newState.ty0 = state.ty0; newState.tz0 = state.tz0;
					newState.tx1 = txm;		  newState.ty1 = tym;		newState.tz1 = tzm;						
					stateLookUp = FLD_NODE ^ idx;
					case1 = FRD_NODE; case2 = FLT_NODE; case3 = BLD_NODE;
					break;
				case BLD_NODE:
					newState.tx0 = state.tx0; newState.ty0 = state.ty0; newState.tz0 = tzm;
					newState.tx1 = txm;		  newState.ty1 = tym;		newState.tz1 = state.tz1;										
					stateLookUp = BLD_NODE ^ idx;
					case1 = BRD_NODE; case2 = BLT_NODE;
					break;
				case FLT_NODE:
					newState.tx0 = state.tx0; newState.ty0 = tym;		newState.tz0 = state.tz0;
					newState.tx1 = txm;		  newState.ty1 = state.ty1; newState.tz1 = tzm;							
					stateLookUp = FLT_NODE ^ idx;
					case1 = FRT_NODE; case3 = BLT_NODE;
					break;
				case BLT_NODE:
					newState.tx0 = state.tx0; newState.ty0 = tym;		newState.tz0 = tzm;
					newState.tx1 = txm;		  newState.ty1 = state.ty1; newState.tz1 = state.tz1;							
					stateLookUp = BLT_NODE ^ idx;
					case1 = BRT_NODE;
					break;
				case FRD_NODE:
					newState.tx0 = txm;		  newState.ty0 = state.ty0; newState.tz0 = state.tz0;
					newState.tx1 = state.tx1; newState.ty1 = tym;		newState.tz1 = tzm;						
					stateLookUp = FRD_NODE ^ idx;
					case2 = FRT_NODE; case3 = BRD_NODE;
					break;
				case BRD_NODE:
					newState.tx0 = txm;		  newState.ty0 = state.ty0; newState.tz0 = tzm;
					newState.tx1 = state.tx1; newState.ty1 = tym;		newState.tz1 = state.tz1;						
					stateLookUp = BRD_NODE ^ idx;
					case2 = BRT_NODE;
					break;
				case FRT_NODE:
					newState.tx0 = txm;		  newState.ty0 = tym;		newState.tz0 = state.tz0;
					newState.tx1 = state.tx1; newState.ty1 = state.ty1; newState.tz1 = tzm;						
					stateLookUp = FRT_NODE ^ idx;
					case3 = BRT_NODE;
					break;
				case BRT_NODE:
					newState.tx0 = txm;		  newState.ty0 = tym;		newState.tz0 = tzm;
					newState.tx1 = state.tx1; newState.ty1 = state.ty1; newState.tz1 = state.tz1;						
					stateLookUp = BRT_NODE ^ idx;
					break;
			}

			currNode = new_node(newState.tx1, case1, 
								newState.ty1, case2,
								newState.tz1, case3);
			proc_subtree2(idx, newState.tx0, newState.ty0, newState.tz0, newState.tx1, newState.ty1, newState.tz1,node->son[stateLookUp], octrees);
		} while (currNode < 8);
	}

	bool CSDFController::CheckError(int err, char extra_debug[32768])
	{
		switch(err)
		{
			case 0: return true;
			case 1: loggger->logInfo("Error: Failed to find a platform!"); return false;
			case 2: loggger->logInfo("Error: Failed to create a device group!"); return false;
			case 3: loggger->logInfo("Error: Failed to create a compute context!"); return false;
			case 4: loggger->logInfo("Error: Failed to create a command commands!"); return false;
			case 5: loggger->logInfo("Error: Failed to create compute program!"); return false;
			case 6: loggger->logInfo("Error: Failed to build program executable!"); loggger->logInfo(extra_debug); return false;
			case 7: loggger->logInfo("Error: Failed to create compute kernel!"); return false;
			case 8: loggger->logInfo("Error: Failed to get device info!"); return false;
			case 9: loggger->logInfo("Error: Failed to allocate memory!"); return false;
			case 10: loggger->logInfo("Error: Failed to write to source array!"); return false;
			case 11: loggger->logInfo("Error: Failed to set kernel arguments!"); return false;
			case 12: loggger->logInfo("Error: Failed to execute kernel!"); return false;
			case 13: loggger->logInfo("Error: Failed to Load .cl file!"); return false;
			case 14: loggger->logInfo("Error: Failed to read from source array!"); return false;
			default: return true;
		}
	}

	void CSDFController::UniformPointsOnSphere(float* rndx, float * rndy)
	{
		unsigned int n_rays = Nastavenia->SDF_Rays;
		float N = (float)n_rays;
		
		float mull = 4.0f / (2.0f * (1.0f - cos( (Nastavenia->SDF_Cone / 2.0f) * (float)M_PI / 180.0f )));
		N = N * mull;

		float inc = (float)M_PI * (3.0f - sqrt(5.0f));
		float off = (2.0f / N);
		for(unsigned int k = 0; k < n_rays; k++)
		{
			float y = k * off - 1 + (off / 2.0f);
			float r = sqrt(1 - y*y);
			float phi = k * inc;
			Vector4 ray = Vector4(cos(phi)*r, -y, sin(phi)*r);
			ray.Normalize();
			CalcAnglesFromRay(ray, rndx[k], rndy[k]);
			GetDegrees(rndx[k], rndy[k]);
			if(rndy[k] < 0.5)
				rndy[k] = 0.5;
		}
	}

	void CSDFController::RandomPointsOnSphere(float* rndx, float * rndy)
	{
		unsigned int n_rays = Nastavenia->SDF_Rays;
		// precompute those N rays
		srand (123);											// initial seed for random number generator
		for(unsigned int i = 0; i < n_rays; i++)
		{
			rndy[i] = float(rand()%int(Nastavenia->SDF_Cone / 2));
			rndx[i] = float(rand()%(360));
			if(rndy[i] < 0.5)
				rndy[i] = 0.5;
		}
	}
	void CSDFController::InitKernel()
	{
		kernel_size = Nastavenia->SDF_Smoothing_Radius;
		gauss_sus = new LinkedList<Face>*[kernel_size + 1];
		// preallocate smoothing arrays
		for(int i=0; i <= kernel_size; i++)				// bacha na posunutie
		{
			gauss_sus[i] = new LinkedList<Face>();
			switch(i)
			{
				case 0: gauss_sus[i]->Preallocate(1); break;
				case 1: gauss_sus[i]->Preallocate(10); break;
				case 2: gauss_sus[i]->Preallocate(100); break;
				default: gauss_sus[i]->Preallocate(1000); break;
			}
		}
	}
	void CSDFController::EraseKernel()
	{
		// delete smoothing arrays
		for(int i=0; i <= kernel_size; i++)
		{
			delete gauss_sus[i];
		}
		delete [] gauss_sus;
	}



	typedef unsigned char       U8;
	typedef unsigned short      U16;
	typedef unsigned int        U32;
	typedef signed char         S8;
	typedef signed short        S16;
	typedef signed int          S32;
	typedef float               F32;
	typedef double              F64;
	typedef unsigned long       U64;
	typedef signed long         S64;
	typedef void                (*FuncPtr)(void);
	int c_popc8LUT[] =
	{
		0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
		1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
		1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
		2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
		1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
		2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
		2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
		3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
		1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
		2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
		2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
		3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
		2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
		3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
		3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
		4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
	};
	inline int popc8(U32 mask)
	{
		//((void)0);
		return c_popc8LUT[mask];
	}
	int __float_as_int(float in)
	{
		union fi { int i; float f; } conv;
		conv.f = in;
		return conv.i;
	}
	float __int_as_float(int a)
	{
		union {int a; float b;} u;
		u.a = a;
		return u.b;
	}
	#define CAST_STACK_DEPTH        23
	#define MAX_RAYCAST_ITERATIONS  10000
	struct cll_float3
	{
		float       x;
		float       y;
		float       z;
	};
	struct cll_int2
	{
		int       x;
		int       y;
	};
	cll_float3 make_float3(float xx, float yy, float zz)
	{
		cll_float3 result;
		result.x = xx;
		result.y = yy;
		result.z = zz;
		return result;
	};
	cll_int2 make_int2(int xx, int yy)
	{
		cll_int2 result;
		result.x = xx;
		result.y = yy;
		return result;
	};
	struct Ray
	{
		cll_float3      orig;
		cll_float3      orig_real;
		cll_float3      dir;
	};
	struct CastResult
	{
		float       t;
		cll_float3      pos;
		int         iter;

		void*        node;
		int         childIdx;
		int         stackPtr;
	};

	//------------------------------------------------------------------------

	class CastStack
	{
	public:
		CastStack   (void)
		{
			for(int i = 0; i < 24; i++)
			{
				tstack[i] = 0;
				ostack[i] = NULL;
			}
		}

		//Octree* read        (int idx, F32& tmax) const      { U64 e = stack[idx]; tmax = __int_as_float((U32)(e >> 32)); return (Octree*)(U32)e; }
		//void write       (int idx, Octree* node, F32 tmax)  { stack[idx] = (U32)node | ((U64)__float_as_int(tmax) << 32); }
		Octree* read     (int idx, F32& tmax) const         { tmax = tstack[idx]; return ostack[idx]; }
		void write       (int idx, Octree* node, F32 tmax)  { ostack[idx] = node; tstack[idx] = tmax; }

	private:
		F32             tstack[24];
		Octree*         ostack[24];
	};
	float copysignf(float a, float b)
	{
		if(b < 0)
			return -a;
		else
			return a;
	};
	float fmaxf(float a, float b)
	{
		if(a > b)
			return a;
		else
			return b;
	};
	float fminf(float a, float b)
	{
		if(a < b)
			return a;
		else
			return b;
	};
	void CSDFController::proc_subtree3 (Vector4 o, Vector4 or, Vector4 d, Octree* node, LinkedList<Octree>* octrees)
	{
		Ray ray;
		ray.orig.x = o.X; ray.orig.y = o.Y; ray.orig.z = o.Z;
		ray.orig_real.x = or.X; ray.orig_real.y = or.Y; ray.orig_real.z = or.Z;
		ray.dir.x = d.X; ray.dir.y = d.Y; ray.dir.z = d.Z;

		CastStack stack;
		const float epsilon = 0.000001f;
		//float ray_orig_sz = ray.orig_sz;
		int iter = 0;

		// Get rid of small ray direction components to avoid division by zero.

		if (fabsf(ray.dir.x) < epsilon) ray.dir.x = copysignf(epsilon, ray.dir.x);
		if (fabsf(ray.dir.y) < epsilon) ray.dir.y = copysignf(epsilon, ray.dir.y);
		if (fabsf(ray.dir.z) < epsilon) ray.dir.z = copysignf(epsilon, ray.dir.z);

		// Precompute the coefficients of tx(x), ty(y), and tz(z).
		// The octree is assumed to reside at coordinates [1, 2].

		float tx_coef = 1.0f / -fabs(ray.dir.x);
		float ty_coef = 1.0f / -fabs(ray.dir.y);
		float tz_coef = 1.0f / -fabs(ray.dir.z);

		float tx_bias = tx_coef * ray.orig.x;
		float ty_bias = ty_coef * ray.orig.y;
		float tz_bias = tz_coef * ray.orig.z;

		// Select octant mask to mirror the coordinate system so
		// that ray direction is negative along each axis.

		int octant_mask = 7;
		if (ray.dir.x > 0.0f) octant_mask ^= 1, tx_bias = 3.0f * tx_coef - tx_bias;
		if (ray.dir.y > 0.0f) octant_mask ^= 2, ty_bias = 3.0f * ty_coef - ty_bias;
		if (ray.dir.z > 0.0f) octant_mask ^= 4, tz_bias = 3.0f * tz_coef - tz_bias;

		// Initialize the active span of t-values.

		float t_min = fmaxf(fmaxf(2.0f * tx_coef - tx_bias, 2.0f * ty_coef - ty_bias), 2.0f * tz_coef - tz_bias);
		float t_max = fminf(fminf(tx_coef - tx_bias, ty_coef - ty_bias), tz_coef - tz_bias);
		float h = t_max;
		t_min = fmaxf(t_min, 0.0f);
		t_max = fminf(t_max, 1.0f);

		// Initialize the current voxel to the first child of the root.
		// sme prvym synom roota, tj parent = m_root, zistime ktory sme az neskor
		Octree*   parent        = node;
		int child_descriptor = 0; // invalid until fetched
		int    idx              = 0;
		cll_float3 pos          = make_float3(1.0f, 1.0f, 1.0f);
		int    scale            = CAST_STACK_DEPTH - 1;
		float  scale_exp2       = 0.5f; // exp2f(scale - s_max)

		if (1.5f * tx_coef - tx_bias > t_min) idx ^= 1, pos.x = 1.5f;
		if (1.5f * ty_coef - ty_bias > t_min) idx ^= 2, pos.y = 1.5f;
		if (1.5f * tz_coef - tz_bias > t_min) idx ^= 4, pos.z = 1.5f;

		// Traverse voxels along the ray as long as the current voxel
		// stays within the octree.

		while (scale < CAST_STACK_DEPTH)
		{
			iter++;
			if (iter > MAX_RAYCAST_ITERATIONS)
				break;

			// Fetch child descriptor unless it is already valid.
			// nacitame si hodnoty nasho otca, tj link na prveho syna a ci su synovia validny/listnaty
			if (child_descriptor == 0)
			{
				//child_descriptor = (*parent)<<24;
				child_descriptor = parent->sons;
			}
			if (child_descriptor == 0)
			{
				//sme v leafe
				// ray intersect
				octrees->InsertToEnd(parent);
			}
			// Determine maximum t-value of the cube by evaluating
			// tx(), ty(), and tz() at its corner.

			float tx_corner = pos.x * tx_coef - tx_bias;
			float ty_corner = pos.y * ty_coef - ty_bias;
			float tz_corner = pos.z * tz_coef - tz_bias;
			float tc_max = fminf(fminf(tx_corner, ty_corner), tz_corner);

			// Process voxel if the corresponding bit in valid mask is set
			// and the active t-span is non-empty.

			int child_shift = idx ^ octant_mask; // permute child slots based on the mirroring
			int child_masks = child_descriptor << child_shift;
			if (((child_masks & 0x0080) != 0) && (t_min <= t_max))
			{
				// Terminate if the voxel is small enough.

				/*if (tc_max * ray.dir_sz + ray_orig_sz >= scale_exp2)
					break; // at t_min*/

				// INTERSECT
				// Intersect active t-span with the cube and evaluate
				// tx(), ty(), and tz() at the center of the voxel.

				float tv_max = fminf(t_max, tc_max);
				float half = scale_exp2 * 0.5f;
				float tx_center = half * tx_coef + tx_corner;
				float ty_center = half * ty_coef + ty_corner;
				float tz_center = half * tz_coef + tz_corner;

				// Descend to the first child if the resulting t-span is non-empty.

				if (t_min <= tv_max)
				{
					// Terminate if the corresponding bit in the non-leaf mask is not set.
					// sme v dakom leafe
					/*if (child_descriptor == 0)
						break; // at t_min (overridden with tv_min).*/

					// PUSH
					// Write current parent to the stack.

					if (tc_max < h)
					{
						stack.write(scale, parent, t_max);
					}
					h = tc_max;

					// Find child descriptor corresponding to the current voxel.

					/*int ofs = (unsigned int)(*parent) >> 8; // child pointer
					ofs += popc8((child_masks<<8) & 0x7F);
					parent += ofs;*/
					int ofs = popc8((child_masks) & 0x7F);
					parent = parent->son[ofs];

					// Select child voxel that the ray enters first.

					idx = 0;
					scale--;
					scale_exp2 = half;

					if (tx_center > t_min) idx ^= 1, pos.x += scale_exp2;
					if (ty_center > t_min) idx ^= 2, pos.y += scale_exp2;
					if (tz_center > t_min) idx ^= 4, pos.z += scale_exp2;

					// Update active t-span and invalidate cached child descriptor.

					t_max = tv_max;
					child_descriptor = 0;
					continue;
				}
			}

			// ADVANCE
			// Step along the ray.


			int step_mask = 0;
			if (tx_corner <= tc_max) step_mask ^= 1, pos.x -= scale_exp2;
			if (ty_corner <= tc_max) step_mask ^= 2, pos.y -= scale_exp2;
			if (tz_corner <= tc_max) step_mask ^= 4, pos.z -= scale_exp2;

			// Update active t-span and flip bits of the child slot index.

			t_min = tc_max;
			idx ^= step_mask;

			// Proceed with pop if the bit flips disagree with the ray direction.

			if ((idx & step_mask) != 0)
			{
				// POP
				// Find the highest differing bit between the two positions.

				unsigned int differing_bits = 0;
				if ((step_mask & 1) != 0) differing_bits |= __float_as_int(pos.x) ^ __float_as_int(pos.x + scale_exp2);
				if ((step_mask & 2) != 0) differing_bits |= __float_as_int(pos.y) ^ __float_as_int(pos.y + scale_exp2);
				if ((step_mask & 4) != 0) differing_bits |= __float_as_int(pos.z) ^ __float_as_int(pos.z + scale_exp2);
				scale = (__float_as_int((float)differing_bits) >> 23) - 127; // position of the highest bit
				scale_exp2 = __int_as_float((scale - CAST_STACK_DEPTH + 127) << 23); // exp2f(scale - s_max)

				// Restore parent voxel from the stack.

				parent = stack.read(scale, t_max);

				// Round cube position and extract child slot index.

				int shx = __float_as_int(pos.x) >> scale;
				int shy = __float_as_int(pos.y) >> scale;
				int shz = __float_as_int(pos.z) >> scale;
				pos.x = __int_as_float(shx << scale);
				pos.y = __int_as_float(shy << scale);
				pos.z = __int_as_float(shz << scale);
				idx  = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);

				// Prevent same parent from being stored again and invalidate cached child descriptor.

				h = 0.0f;
				child_descriptor = 0;
			}
		}
	}

	bool CSDFController::CheckValid(int mask, int num)
	{
		return((mask >> num) & 1);
	}
	void CSDFController::proc_subtree4 (unsigned char idx, Vector4 t0, Vector4 t1, Octree* node, LinkedList<Octree>* octrees)
	{
		Vector4 tm = Vector4();
		int currNode = -1;
		CastStackx stack;
		int scale = 0;
		while (true)
		{
			// konec toho spodneho switchu, berem otca
			if(currNode == 8)
			{
				if(scale > 0)
				{
					scale--;
					node = stack.read(scale, t0, t1, currNode);
					continue;
				}
				else
					break;
			}
			// mame novy nody
			else if(currNode == -1)
			{
				if((t1.X < 0.0) || (t1.Y < 0.0) || (t1.Z < 0.0))
				{
					if(scale > 0)
					{
						scale--;
						node = stack.read(scale, t0, t1, currNode);
						continue;
					}
					else
						break;
				}
				unsigned char synovia = node->sons;
				if(synovia == 0)
				{
					//loggger->logInfo("Reached leaf node");
					//if(node->count > 0)
					octrees->InsertToEnd(node);

					scale--;
					node = stack.read(scale, t0, t1, currNode);
					continue;
				}
				tm = 0.5*(t0 + t1);
				
				currNode = first_node2(t0,tm);
			}
			else
				tm = 0.5*(t0 + t1);

			switch (currNode)
			{
			case 0:
				if(CheckValid(node->sons, idx))
				{
					currNode = new_node(tm.X,4,tm.Y,2,tm.Z,1);
					stack.write(scale, t0, t1, currNode, node);
					t1 = tm;
					//proc_subtree4(tx0,ty0,tz0,txm,tym,tzm,node->son[aa], octrees);
					//currNode = new_node(txm,4,tym,2,tzm,1);
					currNode = -1;
					node = node->son[idx];
				}
				else
				{
					currNode = new_node(tm.X,4,tm.Y,2,tm.Z,1);
					scale--;
				}
				break;
			case 1: 
				if(CheckValid(node->sons, 1^idx))
				{
					currNode = new_node(tm.X,5,tm.Y,3,t1.Z,8);
					stack.write(scale, t0, t1, currNode, node);
					t0.Z = tm.Z;
					t1.X = tm.X;
					t1.Y = tm.Y;
					//proc_subtree4(tx0,ty0,tzm,txm,tym,tz1,node->son[1^aa], octrees);
					//currNode = new_node(txm,5,tym,3,tz1,8);
					currNode = -1;
					node = node->son[1^idx];
				}
				else
				{
					currNode = new_node(tm.X,5,tm.Y,3,t1.Z,8);
					scale--;
				}
				break;
			case 2:
				if(CheckValid(node->sons, 2^idx))
				{
					currNode = new_node(tm.X,6,t1.Y,8,tm.Z,3);
					stack.write(scale, t0, t1, currNode, node);
					t0.Y = tm.Y;
					t1.X = tm.X;
					t1.Z = tm.Z;
					//proc_subtree4(tx0,tym,tz0,txm,ty1,tzm,node->son[2^aa], octrees);
					//currNode = new_node(txm,6,ty1,8,tzm,3);
					currNode = -1;
					node = node->son[2^idx];
				}
				else
				{
					currNode = new_node(tm.X,6,t1.Y,8,tm.Z,3);
					scale--;
				}
				break;
			case 3:
				if(CheckValid(node->sons, 3^idx))
				{
					currNode = new_node(tm.X,7,t1.Y,8,t1.Z,8);
					stack.write(scale, t0, t1, currNode, node);
					t0.Y = tm.Y;
					t0.Z = tm.Z;
					t1.X = tm.X;
					//proc_subtree4(tx0,tym,tzm,txm,ty1,tz1,node->son[3^aa], octrees);
					//currNode = new_node(txm,7,ty1,8,tz1,8);
					currNode = -1;
					node = node->son[3^idx];
				}
				else
				{
					currNode = new_node(tm.X,7,t1.Y,8,t1.Z,8);
					scale--;
				}
				break;
			case 4:
				if(CheckValid(node->sons, 4^idx))
				{
					currNode = new_node(t1.X,8,tm.Y,6,tm.Z,5);
					stack.write(scale, t0, t1, currNode, node);
					t0.X = tm.X;
					t1.Y = tm.Y;
					t1.Z = tm.Z;
					//proc_subtree4(txm,ty0,tz0,tx1,tym,tzm,node->son[4^aa], octrees);
					//currNode = new_node(tx1,8,tym,6,tzm,5);
					currNode = -1;
					node = node->son[4^idx];
				}
				else
				{
					currNode = new_node(t1.X,8,tm.Y,6,tm.Z,5);
					scale--;
				}
				break;
			case 5:
				if(CheckValid(node->sons, 5^idx))
				{
					currNode = new_node(t1.X,8,tm.Y,7,t1.Z,8);
					stack.write(scale, t0, t1, currNode, node);
					t0.X = tm.X;
					t0.Z = tm.Z;
					t1.Y = tm.Y;
					//proc_subtree4(txm,ty0,tzm,tx1,tym,tz1,node->son[5^aa], octrees);
					//currNode = new_node(tx1,8,tym,7,tz1,8);
					currNode = -1;
					node = node->son[5^idx];
				}
				else
				{
					currNode = new_node(t1.X,8,tm.Y,7,t1.Z,8);
					scale--;
				}
				break;
			case 6:
				if(CheckValid(node->sons, 6^idx))
				{
					currNode = new_node(t1.X,8,t1.Y,8,tm.Z,7);
					stack.write(scale, t0, t1, currNode, node);
					t0.X = tm.X;
					t0.Y = tm.Y;
					t1.Z = tm.Z;
					//proc_subtree4(txm,tym,tz0,tx1,ty1,tzm,node->son[6^aa], octrees);
					//currNode = new_node(tx1,8,ty1,8,tzm,7);
					currNode = -1;
					node = node->son[6^idx];
				}
				else
				{
					currNode = new_node(t1.X,8,t1.Y,8,tm.Z,7);
					scale--;
				}
				break;
			case 7:
				if(CheckValid(node->sons, 7^idx))
				{
					currNode = 8;
					stack.write(scale, t0, t1, currNode, node);
					t0.X = tm.X;
					t0.Y = tm.Y;
					t0.Z = tm.Z;
					//proc_subtree4(txm,tym,tzm,tx1,ty1,tz1,node->son[7^aa], octrees);
					//currNode = 8;
					currNode = -1;
					node = node->son[7^idx];
				}
				else
				{
					currNode = 8;
					scale--;
				}
				break;
			}
			
			scale++;
		}
	}

	void CSDFController::ComputeOpenCL2(LinkedList<Vertex>* points, LinkedList<Face>* triangles, Octree* root, Vector4 o_min, Vector4 o_max, unsigned int nodeCount, unsigned int leafCount, unsigned int triangleCount)
	{
		using namespace OpenCLForm;
		int ticks1 = GetTickCount();

		//-------------------------------------------
		//---------------INIT OpenCL-------Begin-----
		//-------------------------------------------
		COpenCL* OpenCLko = new COpenCL();

		int err = EXIT_SUCCESS;
		err = OpenCLko->InitOpenCL();
		if(!CheckError(err)) return;

		err = OpenCLko->LoadKernel1((Nastavenia->CLKernelPath + string("sdf2.cl")).c_str());
		if(!CheckError(err)) return;

		err = OpenCLko->BuildKernel1();
		if(!CheckError(err, OpenCLko->debug_buffer)) return;

		err = OpenCLko->LoadKernel2((Nastavenia->CLKernelPath + string("proces.cl")).c_str());
		if(!CheckError(err)) return;

		err = OpenCLko->BuildKernel2();
		if(!CheckError(err, OpenCLko->debug_buffer)) return;

		err = OpenCLko->LoadKernel3((Nastavenia->CLKernelPath + string("smooth.cl")).c_str());
		if(!CheckError(err)) return;

		err = OpenCLko->BuildKernel3();
		if(!CheckError(err, OpenCLko->debug_buffer)) return;

		err = OpenCLko->GetGPUVariables();
		if(!CheckError(err)) return;

		//-------------------------------------------
		//---------------INIT OpenCL-------End-------
		//-------------------------------------------


		const unsigned int n_rays = Nastavenia->SDF_Rays;
		const unsigned int n_triangles = triangles->GetSize();
		const unsigned int n_nodes = nodeCount;
		const unsigned int n_leaves = leafCount;
		const unsigned int n_node_tria = triangleCount;
		//unsigned int n_vertices = points->GetSize();

		OpenCLko->global = (n_triangles * n_rays);

		//-------------------------------------------
		//---------------Memory Alloc-1----Begin-----
		//-------------------------------------------
		cl_float4	*c_triangles;			// zoznam trojuholnikov obsahujucich 3 vertexy
		cl_uint		*c_nodes;				// zoznam nodov v octree
		cl_uint		*c_node_tria;			// zoznam trojuholnikov v nodoch v octree
		cl_float4	*c_rays;				// zoznamy 30 lucov, ktore sa postupne vkladaju do OpenCL
		cl_float	*c_outputs;				// vzdialenost pre kazdy luc
		cl_float	*c_weights;				// vahy

		unsigned int s_triangles = n_triangles * 3 * sizeof(cl_float4);					// pocet trojuholnikov * 4 * 3 vertexy * float
		unsigned int s_nodes = n_nodes * sizeof(cl_uint);								// pocet nodov * int
		unsigned int s_node_tria = (n_leaves +  n_node_tria) * sizeof(cl_uint);			// (pocet nodov (velkosti) + pocet trojuholnikov) * int
		unsigned int s_rays = n_rays * sizeof(cl_float4);								// 30 * 4 * float
		unsigned int s_outputs = n_triangles * n_rays * sizeof(cl_float);				// trojuholniky * 30 * float
		unsigned int s_weights = n_rays * sizeof(cl_float);								// 30 * float

		int ticks2 = GetTickCount();
		err = OpenCLko->SetupMemory2(s_triangles, s_nodes, s_node_tria, s_rays, s_outputs);
		if(!CheckError(err)) return;
		int ticks3 = GetTickCount();

		c_triangles = (cl_float4*) malloc(s_triangles);
		c_nodes = (cl_uint*) malloc(s_nodes);//o_array;
		c_node_tria = (cl_uint*) malloc(s_node_tria);//t_array;
		c_rays = (cl_float4*) malloc(s_rays);
		c_outputs = (cl_float*) malloc(s_outputs);
		c_weights = (cl_float*) malloc(s_weights);

		//-------------------------------------------
		//---------------Memory Alloc-1----End-------
		//-------------------------------------------

		//------------------prealocated variables------------------	
		Vector4 tangens, normal, binormal;
		Mat4 t_mat;

		float* rndy = new float[n_rays];
		float* rndx = new float[n_rays];
		if(Nastavenia->SDF_Distribution == true)
			UniformPointsOnSphere(rndx, rndy);
		else
			RandomPointsOnSphere(rndx, rndy);

		for(unsigned int i = 0; i < n_rays; i++)
		{
			Vector4 ray = CalcRayFromAngle(rndx[i], rndy[i]);
			ray.Normalize();
			c_rays[i].s[0] = ray.X;
			c_rays[i].s[1] = ray.Y;
			c_rays[i].s[2] = ray.Z;
			c_rays[i].s[3] = 0.0f;
			c_weights[i] = 180.0f - rndy[i];
		}
		delete [] rndy;
		delete [] rndx;
		//------------------prealocated variables------------------

		// -----------------------------------------------
		// vypocitaj dopredu zoznamy trojuholnikov a lucov
		// -----------------------------------------------
		int ticks4 = GetTickCount();
		//---------------copy variables--------------
		unsigned int pocet = 0;
		LinkedList<Face>::Cell<Face>* tmp_face = triangles->start;
		while(tmp_face != NULL)
		{
			for(int ii = 0; ii < 3; ii++)
			{
				c_triangles[pocet].s[0] = tmp_face->data->v[ii]->P.X;
				c_triangles[pocet].s[1] = tmp_face->data->v[ii]->P.Y;
				c_triangles[pocet].s[2] = tmp_face->data->v[ii]->P.Z;
				c_triangles[pocet].s[3] = tmp_face->data->v[ii]->P.W;
				pocet++;
			}
			tmp_face = tmp_face->next;
		}


		{
			Octree** oc_array = new Octree*[nodeCount];

			Octree* node = root;
			unsigned int end = 0;
			oc_array[0] = node;
			unsigned int tidx = 0;
			bool jeden_krat = true;
			for(unsigned int idx = 0; idx < nodeCount; idx++)
			{
				node = oc_array[idx];
				if(node->isLeaf)
				{
					unsigned int safe_tidx = tidx << 8;
					c_nodes[idx] = safe_tidx;
					c_node_tria[tidx] = node->count;
					tidx++;
					for(unsigned int j = 0; j < node->count; j++)
					{
						c_node_tria[tidx] = node->triangles[j]->number;
						tidx++;
					}
					continue;
				}
				jeden_krat = true;
				for(int i = 0; i < 8; i++)
				{
					if((node->sons >> i) & 1)
					{
						end++;
						if(jeden_krat == true)
						{
							unsigned int safe_end = end<<8;
							safe_end = safe_end +  node->sons;
							c_nodes[idx] = safe_end;
							jeden_krat = false;
						}
						oc_array[end] = node->son[i];
					}
				}
			}
			delete [] oc_array;
		}
		//---------------copy variables--------------
		

		// v tomto bode je uz pamet pripravena a nacitana
		// je nutne poslat ju do OpenCL a zahajit vypocet
		int ticks5 = GetTickCount();
		cl_float4 oo_min; oo_min.s[0] = o_min.X; oo_min.s[1] = o_min.Y; oo_min.s[2] = o_min.Z; oo_min.s[3] = o_min.W;
		cl_float4 oo_max; oo_max.s[0] = o_max.X; oo_max.s[1] = o_max.Y; oo_max.s[2] = o_max.Z; oo_max.s[3] = o_max.W;
		cl_float bias = root->size * 2.0f * 0.00001f;

		OpenCLko->debugger->max_outputs = n_triangles * n_rays;
		OpenCLko->debugger->nodeCount = n_nodes;
		OpenCLko->debugger->triangleCount = n_leaves +  n_node_tria;
		OpenCLko->debugger->nn_triangles = n_triangles * 3;

		err = OpenCLko->LaunchKernel2(c_triangles, c_nodes, c_node_tria, oo_min, oo_max, bias, c_rays, n_rays, n_triangles, c_outputs);
		if(!CheckError(err)) return;

		OpenCLko->WaitForFinish();

		int ticks6 = GetTickCount();

		//-------------------------------------------
		//---------------Memory Alloc-2----Begin-----
		//-------------------------------------------

		cl_float	*c_results;				// vzdialenost pre kazdy face, ktore je mojim vysledkom co si zapisem

		unsigned int s_results = n_triangles * sizeof(cl_float);						// trojuholniky * float

		err = OpenCLko->SetupMemory3(s_results, s_weights);
		if(!CheckError(err)) return;

		c_results = (cl_float*) malloc(s_results);

		//-------------------------------------------
		//---------------Memory Alloc-2----End-------
		//-------------------------------------------

		float* c_cache = new float[n_rays];
		for(unsigned int i = 0; i < n_rays; i++)
			c_cache[i] = 0;

		OpenCLko->global = n_triangles;
		err = OpenCLko->LaunchKernel3(c_outputs, c_results, c_weights, n_triangles);
		if(!CheckError(err)) return;

		OpenCLko->WaitForFinish();

		// spracuj ziskane hodnoty (spracovane nizsie)
		float min = FLOAT_MAX;
		float max = 0.0f;

		LinkedList<Face>::Cell<Face>* current_face = triangles->start;
		for(unsigned int i = 0; i < n_triangles; i++)
		{
			current_face->data->quality->value = c_results[i];
			if(current_face->data->quality->value < min)
				min = current_face->data->quality->value;
			if(current_face->data->quality->value > max)
				max = current_face->data->quality->value;
			
			current_face = current_face->next;
		}

		//CopySDF_Faces_to_Vertices(points);
		//CopySDF_Vertices_to_Faces(triangles);

		int ticks7 = GetTickCount();
		int ticks8 = GetTickCount();
		int ticks9 = GetTickCount();
		if(Nastavenia->SDF_Smooth_Projected == true)
		{
			int tmp_threshold = Nastavenia->OCTREE_Threshold;
			Nastavenia->OCTREE_Threshold = max((n_triangles / 450), 4);
			/*Nastavenia->OCTREE_Depth = Nastavenia->OCTREE_Depth - 2;
			DoSmoothing2(triangles, min, max);
			Nastavenia->OCTREE_Depth = Nastavenia->OCTREE_Depth + 2;*/

			current_face = triangles->start;

			if(Nastavenia->SDF_Smoothing_Radius > 0)
			{
				LinkedList<PPoint>* point_list = new LinkedList<PPoint>();
				const unsigned int tsize = triangles->GetSize();
				PPoint **point_array = new PPoint*[tsize];
				unsigned int count = 0;
				while(current_face != NULL)
				{
					// projektnute body
					PPoint* tmp = new PPoint(current_face->data->center + ((current_face->data->normal * -1.0f) * current_face->data->quality->value) / 2.0f, current_face->data);
					tmp->diameter = current_face->data->quality->value;
					point_array[count] = tmp;
					if(tmp->diameter >= 0.01f)
						point_list->InsertToEnd(tmp);
					count++;
					current_face = current_face->next;
				}

				float b_size;
				unsigned int n_pnodes = 0;
				Nastavenia->OCTREE_Depth = Nastavenia->OCTREE_Depth - 2;
				Vector4 b_stred = ComputePointBoundary(point_list, b_size);
				ROctree* m_root = CreateROctree(point_list, b_size, b_stred, n_pnodes);
				Nastavenia->OCTREE_Depth = Nastavenia->OCTREE_Depth + 2;

				ticks8 = GetTickCount();

				//-------------------------------------------
				//---------------Memory Alloc-3----Begin-----
				//-------------------------------------------

				cl_float4	*c_points;				// zoznam pointov obsahujucich X, Y, Z, SDF
				cl_float4	*c_pnode_origins;		// zoznam stredov nodov
				cl_uint		*c_pnodes;				// zoznam nodov v octree
				cl_float	*c_pnode_values;		// zoznam hodnot v nodoch octree
				cl_uint		*c_pnode_counts;		// pocty hodnot v nodoch octree

				unsigned int s_points = tsize * sizeof(cl_float4);							// pocet trojuholnikov * 4  * float
				unsigned int s_pnode_origins = n_pnodes * sizeof(cl_float4);				// pocet nodov * 4  * float
				unsigned int s_pnodes = n_pnodes * sizeof(cl_uint);							// pocet nodov * uint
				unsigned int s_pnode_values = n_pnodes * sizeof(cl_float);					// pocet nodov * float
				unsigned int s_pnode_counts = n_pnodes * sizeof(cl_uint);					// pocet nodov * uint

				err = OpenCLko->SetupMemory4(s_points, s_pnodes, s_pnode_origins, s_pnode_values, s_pnode_counts);
				if(!CheckError(err)) return;

				c_points = (cl_float4*) malloc(s_points);
				c_pnode_origins = (cl_float4*) malloc(s_pnode_origins);
				c_pnodes = (cl_uint*) malloc(s_pnodes);
				c_pnode_values = (cl_float*) malloc(s_pnode_values);
				c_pnode_counts = (cl_uint*) malloc(s_pnode_counts);

				//-------------------------------------------
				//---------------Memory Alloc-3----End-------
				//-------------------------------------------

				for (unsigned int i = 0; i < tsize; i++)
				{
					for(int ii = 0; ii < 3; ii++)
					{
						c_points[i].s[0] = point_array[i]->P.X;
						c_points[i].s[1] = point_array[i]->P.Y;
						c_points[i].s[2] = point_array[i]->P.Z;
						c_points[i].s[3] = point_array[i]->diameter;
						point_array[i]->number = i;
					}
				}


				{
					ROctree** oc_array = new ROctree*[n_pnodes];

					ROctree* node = m_root;
					unsigned int end = 0;
					oc_array[0] = m_root;
					unsigned int tidx = 0;
					bool jeden_krat = true;
					for(unsigned int idx = 0; idx < n_pnodes; idx++)
					{
						node = oc_array[idx];
						if(node->isLeaf())
						{
							unsigned int safe_end = idx << 8;
							c_pnodes[idx] = safe_end;
							c_pnode_values[idx] = node->value;
							c_pnode_counts[idx] = node->count;
							for(int ii = 0; ii < 3; ii++)
							{
								c_pnode_origins[idx].s[0] = node->origin.X;
								c_pnode_origins[idx].s[1] = node->origin.Y;
								c_pnode_origins[idx].s[2] = node->origin.Z;
								c_pnode_origins[idx].s[3] = 1.0f;
							}
							continue;
						}
						jeden_krat = true;
						for(int i = 0; i < 8; i++)
						{
							if((node->sons >> i) & 1)
							{
								end++;
								if(jeden_krat == true)
								{
									unsigned int safe_end = end << 8;
									safe_end = safe_end + node->sons;
									c_pnodes[idx] = safe_end;
									c_pnode_values[idx] = node->value;
									c_pnode_counts[idx] = node->count;
									for(int ii = 0; ii < 3; ii++)
									{
										c_pnode_origins[idx].s[0] = node->origin.X;
										c_pnode_origins[idx].s[1] = node->origin.Y;
										c_pnode_origins[idx].s[2] = node->origin.Z;
										c_pnode_origins[idx].s[3] = 1.0f;
									}
									jeden_krat = false;
								}
								oc_array[end] = node->son[i];
							}
						}
					}
					delete [] oc_array;
				}
				ticks9 = GetTickCount();
				//cl_float wei = (float)Nastavenia->SDF_Smoothing_Radius;
				err = OpenCLko->LaunchKernel4(c_points, c_pnode_origins, c_pnodes, c_pnode_values, c_pnode_counts, c_results, tsize, b_size);
				if(!CheckError(err)) return;

				OpenCLko->WaitForFinish();
				min = FLOAT_MAX;
				max = 0.0f;

				for(unsigned int i = 0; i < tsize; i++)
				{
					if(c_results[i] < min)
						min = c_results[i];
					if(c_results[i] > max)
						max = c_results[i];
				}

				for(unsigned int i = 0; i < tsize; i++)
				{
					point_array[i]->ref->quality->smoothed = c_results[i];
					//point_array[i]->ref->quality->value = c_results[i];
					point_array[i]->ref->quality->Normalize(min, max, 4.0);
				}

				for(unsigned int i = 0; i < tsize; i++)
					delete point_array[i];
				delete [] point_array;
				delete point_list;
				delete m_root;

				free(c_points);
				free(c_pnodes);
				free(c_pnode_origins);
				free(c_pnode_values);
				free(c_pnode_counts);
			}
			else
			{
				while(current_face != NULL)
				{
					current_face->data->quality->smoothed = current_face->data->quality->value;
					current_face->data->quality->Normalize(min, max, 4.0);
					current_face = current_face->next;
				}
			}

			Nastavenia->OCTREE_Threshold = tmp_threshold;
		}
		else
			DoSmoothing(triangles, min, max);

		Nastavenia->DEBUG_Min_SDF = min;
		Nastavenia->DEBUG_Max_SDF = max;

		int ticks10 = GetTickCount();


		/*loggger->logInfo(MarshalString("Inicializacia OpenCL: " + (ticks2 - ticks1)+ "ms"));
		loggger->logInfo(MarshalString("Alokovanie pamete v OpenCL: " + (ticks3 - ticks2)+ "ms"));
		loggger->logInfo(MarshalString("Alokovanie pamete v PC: " + (ticks4 - ticks3)+ "ms"));
		loggger->logInfo(MarshalString("Nacitanie zoznamu trojuholnikov: " + (ticks5 - ticks4)+ "ms"));
		loggger->logInfo(MarshalString("!!VYPOCET OpenCL!!: " + (ticks6 - ticks5)+ "ms"));
		loggger->logInfo(MarshalString("Spracovanie: " + (ticks7 - ticks6)+ "ms"));
		if(Nastavenia->SDF_Smoothing_Radius > 0)
		{
			loggger->logInfo(MarshalString("Smoothing - Octree Creation: " + (ticks8 - ticks7)+ "ms"));
			loggger->logInfo(MarshalString("Smoothing - OpenCL Setup: " + (ticks9 - ticks8)+ "ms"));
			loggger->logInfo(MarshalString("Smoothing - Octree Parsing: " + (ticks10 - ticks9)+ "ms"));
		}
		loggger->logInfo(MarshalString("Smoothing: " + (ticks10 - ticks7)+ "ms"));
		loggger->logInfo(MarshalString("Celkovy vypocet trval: " + (ticks10 - ticks1)+ "ms, pre " + n_triangles + " trojuholnikov"));*/
		//loggger->logInfo(MarshalString("pocet: " + pocet));
		//loggger->logInfo(MarshalString("min a max pre SDF su: " + min + ", "+max));
		//loggger->logInfo(MarshalString("nmin a nmax pre SDF su: " + nmin + ", "+nmax));


		// Delete OpenCL to free GPU
		delete OpenCLko;

		// Free host memory
		free(c_triangles);
		free(c_rays);
		free(c_outputs);
		free(c_results);
		free(c_nodes);
		free(c_node_tria);
		free(c_weights);
	}

	void CSDFController::DoSmoothing(LinkedList<Face> *triangles, float min, float max)
	{
		int ticks1 = GetTickCount();
		LinkedList<Face>::Cell<Face>* current_face = triangles->start;
		if(Nastavenia->SDF_Smoothing_Radius > 0)
		{
			// postprocessing - smoothing and normalization
			//float kernel[] = {1.0,4.0,6.0,4.0,1.0};
			EraseKernel();
			InitKernel();
			float* kernel = ComputeGaussianKernel(kernel_size);
			while(current_face != NULL)
			{
				Smooth(current_face->data, kernel, kernel_size);
				current_face->data->quality->Normalize(min, max, 4.0);
				// pokus pouzit diagonalu, pripadne avg a pod, ale dako nefungovalo
				//tmp->data->diameter->Normalizex(0, diagonal, 4.0);

				current_face = current_face->next;
			}

			delete kernel;
		}
		else
		{
			while(current_face != NULL)
			{
				current_face->data->quality->smoothed = current_face->data->quality->value;
				current_face->data->quality->Normalize(min, max, 4.0);
				current_face = current_face->next;
			}
		}
		Nastavenia->DEBUG_Min_SDF = min;
		Nastavenia->DEBUG_Max_SDF = max;
		int ticks2 = GetTickCount();
		//loggger->logInfo(MarshalString("Smoothing na meshi: " + (ticks2 - ticks1)+ "ms"));
	}

	void CSDFController::DoSmoothing2(LinkedList<Face> *triangles, float min, float max)
	{
		int ticks1 = GetTickCount();
		int ticks2 = 0;
		int ticks3 = 0;

		LinkedList<Face>::Cell<Face>* current_face = triangles->start;

		if(Nastavenia->SDF_Smoothing_Radius > 0)
		{
			LinkedList<PPoint>* point_list = new LinkedList<PPoint>();
			const unsigned int tsize = triangles->GetSize();
			PPoint **point_array = new PPoint*[tsize];
			unsigned int count = 0;
			while(current_face != NULL)
			{
				// projektnute body
				PPoint* tmp = new PPoint(current_face->data->center + ((current_face->data->normal * -1.0f) * current_face->data->quality->value) / 2.0f, current_face->data);
				tmp->diameter = current_face->data->quality->value;
				point_array[count] = tmp;
				current_face = current_face->next;
				if(tmp->diameter >= 0.1f)
					point_list->InsertToEnd(point_array[count]);
				count++;
			}

			ticks2 = GetTickCount();
			float b_size; unsigned int n_pnodes;
			Vector4 b_stred = ComputePointBoundary(point_list, b_size);
			ROctree* m_root = CreateROctree(point_list, b_size, b_stred, n_pnodes);

			LinkedList<ROctree>* ro_list = new LinkedList<ROctree>();
			ro_list->Preallocate(10000);

			ticks3 = GetTickCount();
			// toto je to co robim
			for(unsigned int i = 0; i < tsize; i++)
			{
				Smooth2(point_array[i], m_root, ro_list, i);
				point_array[i]->ref->quality->smoothed = point_array[i]->diameter;
				point_array[i]->ref->quality->value = point_array[i]->diameter;
				point_array[i]->ref->quality->Normalize(min, max, 4.0);
				ro_list->Clear();
			}
			delete ro_list;
			for(unsigned int i = 0; i < tsize; i++)
				delete point_array[i];
			delete [] point_array;
			delete point_list;
			delete m_root;
		}
		else
		{
			current_face = triangles->start;
			while(current_face != NULL)
			{
				current_face->data->quality->smoothed = current_face->data->quality->value;
				current_face->data->quality->Normalize(min, max, 4.0);
				current_face = current_face->next;
			}
		}
		Nastavenia->DEBUG_Min_SDF = min;
		Nastavenia->DEBUG_Max_SDF = max;

		int ticks4 = GetTickCount();

		/*if(Nastavenia->SDF_Smoothing_Radius > 0)
		{
			loggger->logInfo(MarshalString("Smoothing - Data Allocation: " + (ticks2 - ticks1)+ "ms"));
			loggger->logInfo(MarshalString("Smoothing - Octree Creation: " + (ticks3 - ticks2)+ "ms"));
			loggger->logInfo(MarshalString("Smoothing - Octree Parsing: " + (ticks4 - ticks3)+ "ms"));
		}*/
	}

	// recursive
	void CSDFController::RadiusSearch1(Vector4 center, float dist, ROctree* node, LinkedList<ROctree>* octrees)
	{
		if(node->isLeaf() == true)
		{
			octrees->InsertToEnd(node);
		}
		else
		{
			for(int i = 0; i < 8; i++)
			{
				if(CheckValid(node->sons, i) == true)
				{
					float vzdialenost = center.Dist(node->son[i]->origin) - dist;
					float cube_radius = node->son[i]->size * SQRT_THREE;
					if(vzdialenost <= (-cube_radius))
						octrees->InsertToEnd(node);
					else if(vzdialenost <= cube_radius)
						RadiusSearch1(center, dist, node->son[i], octrees);
				}
			}
		}	
	}

	class CastStackz
	{
	public:
		CastStackz   (void)
		{
			for(int i = 0; i < 10; i++)
			{
				ostack[i] = NULL;
				tstack[i] = 0;
			}
		}

		ROctree* read     (int idx, U8& id) const         { id = tstack[idx]; return ostack[idx]; }
		void write       (int idx, ROctree* node, U8 id)  { ostack[idx] = node; tstack[idx] = id; }

	private:
		ROctree*         ostack[10];
		U8              tstack[10];
	};

	// funguje to tak ze mi to najde dalsi index na ktory mam ist
	const unsigned char c_pipc8LUT[] =
	{
		8, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
		4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0
	};
	unsigned char pipc8(unsigned char mask)
	{
		return c_pipc8LUT[mask];
	}

	// stack based
	void CSDFController::RadiusSearch2(Vector4 center, float dist, ROctree* node, LinkedList<ROctree>* octrees)
	{
		CastStackz stack;
		ROctree* tmp = NULL;
		int idx = 0;
		U8 id = 0;
		U8 sons = 0;
		stack.write(idx, node, 0); 
		while (idx >= 0)
		{
			tmp = stack.read(idx, id);	// nacitame si momentalne robeny node (node v rekurzivnom)
			if(tmp->sons == 0)		// leaf
			{
				octrees->InsertToEnd(tmp);
				idx--;
			}
			else
			{
				if(id >= 8)
				{
					idx--;
					continue;
				}
				//if(CheckValid(tmp->sons, id) == true)
				{
					float vzdialenost = center.Dist(tmp->son[id]->origin) - dist;
					float cube_radius = tmp->son[id]->size * SQRT_THREE;
					if(vzdialenost <= (-cube_radius))
					{
						octrees->InsertToEnd(tmp->son[id]);
						//idx--;
						sons = tmp->sons;
						sons = sons >> (id + 1);
						sons = sons << (id + 1);
						id = pipc8(sons);
						stack.write(idx, tmp, id);
					}
					else if(vzdialenost <= cube_radius)
					{
						stack.write(idx + 1, tmp->son[id], pipc8(tmp->son[id]->sons));
						sons = tmp->sons;
						sons = sons >> (id + 1);
						sons = sons << (id + 1);
						id = pipc8(sons);
						stack.write(idx, tmp, id);
						idx = idx + 1;
					}
					else
					{
						sons = tmp->sons;
						sons = sons >> (id + 1);
						sons = sons << (id + 1);
						id = pipc8(sons);
						stack.write(idx, tmp, id); 
					}
				}
			}
		}
	}

	Vector4 CSDFController::ComputePointBoundary(LinkedList<PPoint>* point_list, float &b_size)
	{
		float minx = 99999.0, miny = 99999.0, minz = 99999.0;
		float maxx = -99999.0, maxy = -99999.0, maxz = -99999.0;

		LinkedList<PPoint>::Cell<PPoint>* tmp = point_list->start;
		while(tmp != NULL)
		{
			if(tmp->data->P.X < minx)
				minx = tmp->data->P.X;
			if(tmp->data->P.Y < miny)
				miny = tmp->data->P.Y;
			if(tmp->data->P.Z < minz)
				minz = tmp->data->P.Z;

			if(tmp->data->P.X > maxx)
				maxx = tmp->data->P.X;
			if(tmp->data->P.Y > maxy)
				maxy = tmp->data->P.Y;
			if(tmp->data->P.Z > maxz)
				maxz = tmp->data->P.Z;

			tmp = tmp->next;
		}

		Vector4 b_stred = Vector4((minx+maxx) / 2.0f, (miny+maxy) / 2.0f, (minz+maxz) / 2.0f);
		float sizex = 0;
		float sizey = 0;
		float sizez = 0;

		if(((minx<=0.0)&&(maxx<=0.0)) || ((minx>=0.0)&&(maxx>=0.0)))
			sizex = abs(maxx-minx);
		else
			sizex = abs(minx-maxx);

		if(((miny<=0.0)&&(maxy<=0.0)) || ((miny>=0.0)&&(maxy>=0.0)))
			sizey = abs(maxy-miny);
		else
			sizey = abs(miny-maxy);

		if(((minz<=0.0)&&(maxz<=0.0)) || ((minz>=0.0)&&(maxz>=0.0)))
			sizez = abs(maxz-minz);
		else
			sizez = abs(minz-maxz);

		b_size = max(max(sizex, sizey), sizez) / 2.0f + 0.005f;
		return b_stred;
	}

	Vector4 CSDFController::ComputePointBoundary2(PPoint **point_list, unsigned int psize, float &b_size)
	{
		float minx = 99999.0, miny = 99999.0, minz = 99999.0;
		float maxx = -99999.0, maxy = -99999.0, maxz = -99999.0;

		for(unsigned int i = 0; i < psize; i++)
		{
			if(point_list[i]->P.X < minx)
				minx = point_list[i]->P.X;
			if(point_list[i]->P.Y < miny)
				miny = point_list[i]->P.Y;
			if(point_list[i]->P.Z < minz)
				minz = point_list[i]->P.Z;

			if(point_list[i]->P.X > maxx)
				maxx = point_list[i]->P.X;
			if(point_list[i]->P.Y > maxy)
				maxy = point_list[i]->P.Y;
			if(point_list[i]->P.Z > maxz)
				maxz = point_list[i]->P.Z;
		}

		Vector4 b_stred = Vector4((minx+maxx) / 2.0f, (miny+maxy) / 2.0f, (minz+maxz) / 2.0f);
		float sizex = 0;
		float sizey = 0;
		float sizez = 0;

		if(((minx<=0.0)&&(maxx<=0.0)) || ((minx>=0.0)&&(maxx>=0.0)))
			sizex = abs(maxx-minx);
		else
			sizex = abs(minx-maxx);

		if(((miny<=0.0)&&(maxy<=0.0)) || ((miny>=0.0)&&(maxy>=0.0)))
			sizey = abs(maxy-miny);
		else
			sizey = abs(miny-maxy);

		if(((minz<=0.0)&&(maxz<=0.0)) || ((minz>=0.0)&&(maxz>=0.0)))
			sizez = abs(maxz-minz);
		else
			sizez = abs(minz-maxz);

		b_size = max(max(sizex, sizey), sizez) / 2.0f + 0.005f;
		return b_stred;
	}

	// vytvori Octree strukturu
	ROctree* CSDFController::CreateROctree(LinkedList<PPoint>* point_list, float b_size, Vector4 b_stred, unsigned int &n_pnodes)
	{
		ROctree* m_root = new ROctree(1, b_size, b_stred);

		unsigned int siz = point_list->GetSize();
		if(siz > 0)
		{
			PPoint** pointiky = new PPoint* [siz];
			LinkedList<PPoint>::Cell<PPoint>* tmp = point_list->start;
			int i = 0;
			while(tmp != NULL)
			{
				pointiky[i] = tmp->data;
				tmp = tmp->next;
				i++;
			}
			unsigned int triangleCount = 0, leafCount = 0;
			m_root->Build2(pointiky, 0, siz, n_pnodes, triangleCount, leafCount);
			m_root->DoValueSmoothing();

			delete [] pointiky;
		}
		else
			m_root->Build(NULL, 0);

		return m_root;
	}

	ROctree* CSDFController::CreateROctree2(PPoint** pointiky, unsigned int siz, float b_size, Vector4 b_stred, unsigned int &n_pnodes)
	{
		ROctree* m_root = new ROctree(1, b_size, b_stred);

		if(siz > 0)
		{
			unsigned int leafCount = 0, triangleCount = 0;
			m_root->Build2(pointiky, 0, siz, n_pnodes, triangleCount, leafCount);
			m_root->DoValueSmoothing();
		}
		else
			m_root->Build(NULL, 0);

		return m_root;
	}
	void CSDFController::RandomShuffle(PPoint **c_array, unsigned int size)
	{
		PPoint* temp = NULL;
		int ridx = 0;
		MTRand_int32 irand(123456);

		for(unsigned int i = size-1; i > 0; i--) // one pass through array
		{
			ridx = irand()%(i+1);// index = 0 to j
			temp = c_array[ridx];// value will be moved to end element
			c_array[ridx] = c_array[i];// end element value in random spot
			c_array[i] = temp;// selected element moved to end. This value is final
		}
		return;
	}

	void CSDFController::CopySDF_Vertices_to_Faces(LinkedList<Face>* triangles)
	{
		LinkedList<Face>::Cell<Face>* tmp = triangles->start;
		while(tmp != NULL)
		{
			float h_value		= 0;

			float total = 3.0f;
			for(int i = 0; i<3; i++)
			{
				h_value += tmp->data->v[i]->quality->value;
			}

			h_value = h_value / total;

			tmp->data->quality->value = h_value;

			tmp = tmp->next;
		}
	}

	void CSDFController::CopySDF_Faces_to_Vertices(LinkedList<Vertex>* points)
	{
		LinkedList<Vertex>::Cell<Vertex>* tmp = points->start;
		while(tmp != NULL)
		{
			float h_value		= 0;

			float total = float(tmp->data->susedia->GetSize());
			LinkedList<void>::Cell<void>* tm = tmp->data->susedia->start;
			while(tm != NULL)
			{
				h_value += ((Face*)tm->data)->quality->value;

				tm = tm->next;
			}
			h_value = h_value / total;

			tmp->data->quality->value = h_value;

			tmp = tmp->next;
		}
	}

	/*void CSDFController::SmoothTexture(float** textur)
	{
		GLsizei width = Nastavenia->SDF_Smoothing_Texture;
		GLsizei height = Nastavenia->SDF_Smoothing_Texture;
		GLfloat radius = (float)Nastavenia->SDF_Smoothing_Radius;
		int ticksx1 = GetTickCount();
		CPUSmooth(textur, width, height, radius);
		int ticksx2 = GetTickCount();
		loggger->logInfo(MarshalString("Smoothing v Texture " + width + "x" + height + " na CPU: " + (ticksx2 - ticksx1)+ "ms"));
	}*/

	/*void CSDFController::SmoothTexture(float** textur, LinkedList<Face>* triangles)
			{
		GLsizei width = Nastavenia->SDF_Smoothing_Texture;
		GLsizei height = Nastavenia->SDF_Smoothing_Texture;
		GLfloat radius = (float)Nastavenia->SDF_Smoothing_Radius;

		GLenum err = glewInit();
		if (GLEW_OK != err)
		{
			/* Problem: glewInit failed, something is seriously wrong. */
			//fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
			/*string str = string((char*)glewGetErrorString(err));
			loggger->logInfo("Error: " + str);
			assert(false);
		}

		int ticks1 = GetTickCount();
		glDisable( GL_LIGHTING );
		glDisable( GL_BLEND );
		glDisable( GL_DEPTH_TEST );
		//glEnable(GL_TEXTURE_2D);

		FrameBufferObject* FBO;
		FBO = new FrameBufferObject( width, height );

		GLfloat* mojaTextura = (GLfloat*)calloc(width * height * 4, sizeof(GLfloat));
		GLfloat* mojaTextura1 = (GLfloat*)calloc(width * height * 4, sizeof(GLfloat));
		GLfloat* mojaTextura2 = (GLfloat*)calloc(width * height * 4, sizeof(GLfloat));

		glLoadIdentity();
		glViewport( 0, 0, width, height );     //treba nastavi� viewport
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);      //z�vis� od kontextu, ale v��inou chceme za�a� s �ist�mi text�rami

		FBO->RenderHere();

		FBO->SetLayer( 0, FRAMEBUFFER_LAYER_TYPE_RGBA_32 );    //vytvor�m layer 0 a 1 (zatia� s� len alokovan�, nie s� automaticky attachnut�)
		FBO->SetLayer( 1, FRAMEBUFFER_LAYER_TYPE_RGBA_32 );

		FBO->BindLayer( 0, FBO->LayerHandle( 0 ) );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, mojaTextura1 );
		FBO->BindLayer( 1, FBO->LayerHandle( 1 ) );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, mojaTextura2 );

		FBO->AssignLayer( 0, 0 );    //teraz ich attachnem do akt�vneho FBO
		FBO->AssignLayer( 1, 1 );

		FBO->RenderHere();
		glClearColor(0.0, 0.0, 0.0, 0.0);

		glPushAttrib( GL_ALL_ATTRIB_BITS );
		glMatrixMode( GL_PROJECTION );
		glPushMatrix();
		glLoadIdentity();
		glOrtho( 0.0, 1.0, 0.0, 1.0, 0.0, 1.0 );
		glMatrixMode( GL_MODELVIEW );
		glPushMatrix();
		glLoadIdentity();
		glDisable( GL_DEPTH_TEST );
		glDepthMask( GL_FALSE );

		FBO->DrawBuffers(0);

		glBegin(GL_TRIANGLES);
		LinkedList<Face>::Cell<Face>* tmp = triangles->start;
		while(tmp != NULL)
		{
			if(tmp->data == NULL)
			{
				tmp = tmp->next;
				continue;
			}
			glColor4f(GetNormalizedvalue(tmp->data->v[0]->quality, true),0.0f,0.0f,1.0f);
			glVertex2f(tmp->data->v[0]->texCoord_U, tmp->data->v[0]->texCoord_V);
			glColor4f(GetNormalizedvalue(tmp->data->v[1]->quality, true),0.0f,0.0f,1.0f);
			glVertex2f(tmp->data->v[1]->texCoord_U, tmp->data->v[1]->texCoord_V);
			glColor4f(GetNormalizedvalue(tmp->data->v[2]->quality, true),0.0f,0.0f,1.0f);
			glVertex2f(tmp->data->v[2]->texCoord_U, tmp->data->v[2]->texCoord_V);
			tmp = tmp->next;
		}
		glEnd();

		glMatrixMode( GL_MODELVIEW );
		glPopMatrix();
		glMatrixMode( GL_PROJECTION );
		glPopMatrix();	
		glPopAttrib();

		BlurObject* BO = new BlurObject();

		//blur 9x9 d�t z 0-tej text�ry framebuffera s vyu�it�m pomocnej 1-ej text�ry
		BO->Apply( radius, Vector4(1.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f/float(width), 0.0f, 0.0f, 0.0f), FBO->LayerHandle( 0 ), FBO->Target( 1 ) );
		BO->Apply( radius, Vector4(1.0f, 0.0f, 0.0f, 0.0f), Vector4(0.0f, 1.0f/float(height), 0.0f, 0.0f),FBO->LayerHandle( 1 ), FBO->Target( 0 ) );

		//FBO->ReadBuffer(0);
		//glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, mojaTextura);
		FBO->BindLayer( 0, FBO->LayerHandle( 0 ) );
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, mojaTextura);

		FBO->StopRenderingToFBO();   //v�stup sa odteraz presmeruje do zvy�ajn�ho backbuffera

		for(unsigned int i = 0; i < height; i++)
		{
			for(unsigned int j = 0; j < width; j++)
			{
				textur[i][j] = mojaTextura[(i * width + j)*4 + 0];
			}
		}
		int ticks3 = GetTickCount();
		loggger->logInfo(MarshalString("Smoothing v Texture " + width + "x" + height + " na GPU: " + (ticks3 - ticks1)+ "ms"));

		free(mojaTextura);
		free(mojaTextura1);
		free(mojaTextura2);

		glClearColor(1.0f, 1.0f, 1.0f, 0.5f);				// Background
		//glDisable(GL_TEXTURE_2D);
		glEnable( GL_LIGHTING );
		glEnable( GL_BLEND );
		glEnable( GL_DEPTH_TEST );

		delete BO;
		delete FBO;
	}*/


	/*void CSDFController::CPUSmooth(float** textur, GLsizei width, GLsizei height, float SamplesCount)
	{
		float** textur2 = new float*[height];
		for(int i = 0; i < height; i++)
			textur2[i] = new float[width];

		for(int i = 0; i < height; i++)
		{
			for(int j = 0; j < width; j++)
			{
				CPUBlur(textur, textur2, j, i, SamplesCount, 1, 0, width, height);
			}
		}
		for(int i = 0; i < height; i++)
		{
			for(int j = 0; j < width; j++)
			{
				CPUBlur(textur2, textur, j, i, SamplesCount, 0, 1, width, height);
			}
		}
		for(int i = 0; i < height; i++)
		{
			delete [] textur2[i];
		}
		delete [] textur2;
	}*/

	/*void CSDFController::CPUBlur(float** textur, float** textur2, int x, int y, float SamplesCount, int x_dir, int y_dir, GLsizei width, GLsizei height)
	{
		float mixedValue = 0.0f;  
		float weightSum = 0.0f;

		float samplePositionMultiplier = -SamplesCount;
		while ( samplePositionMultiplier <= SamplesCount )
		{
			int samplePosition_x = x + (int)( x_dir * samplePositionMultiplier );
			int samplePosition_y = y + (int)( y_dir * samplePositionMultiplier );
			float weight = 1.0f - ( abs(samplePositionMultiplier) / (SamplesCount+1) );
			if(samplePosition_x < 0) samplePosition_x = 0; if(samplePosition_x >= width) samplePosition_x = width-1;
			if(samplePosition_y < 0) samplePosition_y = 0; if(samplePosition_y >= height) samplePosition_y = height-1;
			float sampleValue = textur[samplePosition_y][samplePosition_x];
			samplePositionMultiplier += 1.0;
			if(sampleValue < 0.0f)
			{
				continue;
			}
			mixedValue += weight * sampleValue;
			weightSum += weight;
		}
		if(weightSum < 0.00001f)
		{
			mixedValue += 1.0f * textur[y][x];
			weightSum += 1.0f;
		}

		float finalValue = mixedValue / weightSum;
		textur2[y][x] = finalValue;
	}*/

	float** CSDFController::GetTexture(LinkedList<Face>* triangles, bool normalized)
	{
		int ticks1 = GetTickCount();
		float** result = new float*[Nastavenia->SDF_Smoothing_Texture];
		unsigned int** X_kontura = new unsigned int*[Nastavenia->SDF_Smoothing_Texture];
		float** XX_kontura = new float*[Nastavenia->SDF_Smoothing_Texture];
		for(unsigned int i = 0; i < Nastavenia->SDF_Smoothing_Texture; i++)
		{
			result[i] = new float[Nastavenia->SDF_Smoothing_Texture];
			X_kontura[i] = new unsigned int[2];
			XX_kontura[i] = new float[2];
		}

		for(unsigned int i = 0; i < Nastavenia->SDF_Smoothing_Texture; i++)
		{
			for(unsigned int j = 0; j < Nastavenia->SDF_Smoothing_Texture; j++)
			{
				result[i][j] = -1.0f;
			}
		}
		
		LinkedList<Face>::Cell<Face>* tmp = triangles->start;
		while(tmp != NULL)
		{
			DrawTriangle(result, XX_kontura, X_kontura, tmp->data, normalized);
			tmp = tmp->next;
		}
		int ticks2 = GetTickCount();
		//loggger->logInfo(MarshalString("Projektnute trojuholniky do textury " + Nastavenia->SDF_Smoothing_Texture + "x" + Nastavenia->SDF_Smoothing_Texture + ": " + (ticks2 - ticks1)+ "ms"));

		return result;
	}

	/*void clamp(int &num, int min, int max)
	{
		if(num > max)
			num = max;
		else if(num < min)
			num = min;
	}*/
	/*void clamp(float &num, float min, float max)
	{
		if(num > max)
			num = max;
		else if(num < min)
			num = min;
	}*/

	void CSDFController::ApplyTexture(LinkedList<Face>* triangles, float** textur, bool normalized)
	{
		LinkedList<Face>::Cell<Face>* tmp = triangles->start;
		float x0, y0, x1, y1, x2, y2;
		int x, y;
		float min = FLOAT_MAX;
		float max = 0.0f;
		while(tmp != NULL)
		{
			x0 = tmp->data->v[0]->texCoord_U;
			y0 = tmp->data->v[0]->texCoord_V;
			x1 = tmp->data->v[1]->texCoord_U;
			y1 = tmp->data->v[1]->texCoord_V;
			x2 = tmp->data->v[2]->texCoord_U;
			y2 = tmp->data->v[2]->texCoord_V;
			x = (int)(((x0 + x1 + x2) / 3.0f) * (Nastavenia->SDF_Smoothing_Texture - 1));
			y = (int)(((y0 + y1 + y2) / 3.0f) * (Nastavenia->SDF_Smoothing_Texture - 1));

			float val = textur[y][x];
			if(val < 0.0f)
				val = 0.0f;
			if(val < min)
				min = val;
			if(val > max)
				max = val;
			SetNormalizedvalue(tmp->data->quality, val, normalized);

			tmp = tmp->next;
		}
		Nastavenia->DEBUG_Min_SDF = min;
		Nastavenia->DEBUG_Max_SDF = max;

		if(normalized == false)
		{
			tmp = triangles->start;
			while(tmp != NULL)
			{
				tmp->data->quality->Normalize(min, max, 4.0f);
				tmp = tmp->next;
			}
		}
	}

	void CSDFController::ApplyTexture(LinkedList<Vertex>* points, float** textur, bool normalized)
	{
		LinkedList<Vertex>::Cell<Vertex>* tmp = points->start;
		int x, y;
	    float min = FLOAT_MAX;
		float max = 0.0f;
		while(tmp != NULL)
		{
			x = (int)(tmp->data->texCoord_U * (Nastavenia->SDF_Smoothing_Texture - 1));
			y = (int)(tmp->data->texCoord_V * (Nastavenia->SDF_Smoothing_Texture - 1));

			float val = textur[y][x];
			if(val < 0.0f)
				val = 0.0f;
			if(val < min)
				min = val;
			if(val > max)
				max = val;
			SetNormalizedvalue(tmp->data->quality, val, normalized);
			tmp = tmp->next;
		}

		Nastavenia->DEBUG_Min_SDF = min;
		Nastavenia->DEBUG_Max_SDF = max;

		if(normalized == false)
		{
			tmp = points->start;
			while(tmp != NULL)
			{
				tmp->data->quality->Normalize(min, max, 4.0f);
				tmp = tmp->next;
			}
		}

	}

	float CSDFController::GetNormalizedvalue(CSDF* quality, bool normalized)
	{
		if(normalized == false)
			return quality->value;

		switch(Nastavenia->VISUAL_SDF_Type)
		{
		case VISUAL_NORMALIZED_0_1:
			return quality->normalized1;
		case VISUAL_NORMALIZED_MIN_1:
			return quality->normalized2;
		case VISUAL_NORMALIZED_0_MAX:
			return quality->normalized3;
		case VISUAL_NORMALIZED_MIN_MAX:
			return quality->normalized4;
		default: break;
		}
		return 0.0f;
	}

	void CSDFController::SetNormalizedvalue(CSDF* quality, float value, bool normalized)
	{
		if(normalized == false)
		{
			quality->smoothed = value;
			return;
		}

		switch(Nastavenia->VISUAL_SDF_Type)
		{
		case VISUAL_NORMALIZED_0_1:
			quality->normalized1 = value;
			break;
		case VISUAL_NORMALIZED_MIN_1:
			quality->normalized2 = value;
			break;
		case VISUAL_NORMALIZED_0_MAX:
			quality->normalized3 = value;
			break;
		case VISUAL_NORMALIZED_MIN_MAX:
			quality->normalized4 = value;
			break;
		default: break;
		}
	}

	void CSDFController::DrawTriangle(float** textur, float** XX_kontura, unsigned int** X_kontura, Face* triangle, bool normalized)
	{
		for(unsigned int i = 0; i < Nastavenia->SDF_Smoothing_Texture; i++)
		{
			X_kontura[i][0] = Nastavenia->SDF_Smoothing_Texture;
			X_kontura[i][1] = 0;
			XX_kontura[i][0] = 0.0f;
			XX_kontura[i][1] = 0.0f;
		}

		int x0, y0, x1, y1, x2, y2;
		x0 = (int)(triangle->v[0]->texCoord_U * (Nastavenia->SDF_Smoothing_Texture - 1)); //clamp(x0, 0, Nastavenia->SDF_Smoothing_Texture-1);
		y0 = (int)(triangle->v[0]->texCoord_V * (Nastavenia->SDF_Smoothing_Texture - 1)); //clamp(y0, 0, Nastavenia->SDF_Smoothing_Texture-1);
		x1 = (int)(triangle->v[1]->texCoord_U * (Nastavenia->SDF_Smoothing_Texture - 1)); //clamp(x1, 0, Nastavenia->SDF_Smoothing_Texture-1);
		y1 = (int)(triangle->v[1]->texCoord_V * (Nastavenia->SDF_Smoothing_Texture - 1)); //clamp(y1, 0, Nastavenia->SDF_Smoothing_Texture-1);
		x2 = (int)(triangle->v[2]->texCoord_U * (Nastavenia->SDF_Smoothing_Texture - 1)); //clamp(x2, 0, Nastavenia->SDF_Smoothing_Texture-1);
		y2 = (int)(triangle->v[2]->texCoord_V * (Nastavenia->SDF_Smoothing_Texture - 1)); //clamp(y2, 0, Nastavenia->SDF_Smoothing_Texture-1);
		float f0, f1, f2;
		f0 = GetNormalizedvalue(triangle->v[0]->quality, normalized);
		f1 = GetNormalizedvalue(triangle->v[1]->quality, normalized);
		f2 = GetNormalizedvalue(triangle->v[2]->quality, normalized);
		ScanLine(X_kontura, XX_kontura, x0, y0, x1, y1, f0, f1);
		ScanLine(X_kontura, XX_kontura, x1, y1, x2, y2, f1, f2);
		ScanLine(X_kontura, XX_kontura, x2, y2, x0, y0, f2, f0);

		int miny = min(min(y0,y1),y2);
		int maxy = max(max(y0,y1),y2);
		for (unsigned int y = miny; y <= maxy; y++)
		{
			if (X_kontura[y][1] >= X_kontura[y][0])
			{
				unsigned int x = X_kontura[y][0];
				unsigned int len = 1 + X_kontura[y][1] - X_kontura[y][0];
				float val = XX_kontura[y][0];
				float step = 0.0f;
				if(len > 1)
					step = (XX_kontura[y][1] - XX_kontura[y][0]) / (float)(len-1);
				else
					val = (XX_kontura[y][0] + XX_kontura[y][1]) / 2.0f;

				if(val < 0.0f)
				{
					// keby nahodou zlyhala interpolacia
					assert(false);
				}

				while (len--)
				{
					DrawPixel(textur, triangle, x++, y, x0, x1, x2, y0, y1, y2, val);
					val += step;
				}
			}
		}
	}

	void CSDFController::ScanLine(unsigned int** X_kontura, float** XX_kontura, int x1, int y1, int x2, int y2, float f1, float f2)
	{
		int sx, sy, dx1, dy1, dx2, dy2, x, y, m, n, k, cnt, ref_val;
		float dif_f, step_f, val_f;

		step_f = 0.0f;

		sx = x2 - x1;
		sy = y2 - y1;
		dif_f = f2 - f1;
		val_f = f1;

		if ((sy < 0) || ((sy == 0) && (sx < 0)))
		{
			k = x1; x1 = x2; x2 = k;
			k = y1; y1 = y2; y2 = k;
			sx = -sx;
			sy = -sy;
			dif_f = -dif_f;
			val_f = f2;
		}

		if (sx > 0) dx1 = 1;
		else if (sx < 0) dx1 = -1;
		else dx1 = 0;

		if (sy > 0) dy1 = 1;
		else if (sy < 0) dy1 = -1;
		else dy1 = 0;

		// ideme podla x
		m = abs(sx);
		n = abs(sy);
		ref_val = x1;

		dx2 = dx1;
		dy2 = 0;

		// ideme podla y
		if (m < n)
		{
			m = abs(sy);
			n = abs(sx);
			dx2 = 0;
			dy2 = dy1;
			ref_val = y1;
		}

		// zaciname v x1 (uz pripadne prehodene)
		x = x1; y = y1;
		cnt = m + 1;
		k = n / 2;

		if(m > 0)
			step_f = dif_f / (float)(m);
		while (cnt--)
		{
			if ((y >= 0) && (y < Nastavenia->SDF_Smoothing_Texture))
			{
				if ((x >= 0) && (x < Nastavenia->SDF_Smoothing_Texture))
				{
					if (x < X_kontura[y][0])
					{
						X_kontura[y][0] = x;
						XX_kontura[y][0] = val_f;
					}
					if (x > X_kontura[y][1])
					{
						X_kontura[y][1] = x;
						XX_kontura[y][1] = val_f;
					}
				}
			}

			k += n;
			if (k < m)
			{
				x += dx2;
				y += dy2;
			}
			else
			{
				k -= m;
				x += dx1;
				y += dy1;
			}
			val_f += step_f;
		}
	}

	void CSDFController::DrawPixel(float** textur, Face* triangle, unsigned int x, unsigned int y, int x1, int x2, int x3, int y1, int y2, int y3, bool normalized)
	{
		if ((x >= Nastavenia->SDF_Smoothing_Texture) || (y >= Nastavenia->SDF_Smoothing_Texture))
		{
			return;
		}
		// interpolovanie bodov.. nefunguje dako extra
		/*float t[3]; t[0] = 0.0f; t[1] = 0.0f; t[2] = 0.0f;
		float m[3]; m[0] = 0.0f; m[1] = 0.0f; m[2] = 0.0f;

		t[0] = GetNormalizedvalue(triangle->v[0]->quality);
		t[1] = GetNormalizedvalue(triangle->v[1]->quality);
		t[2] = GetNormalizedvalue(triangle->v[2]->quality);

		float det = (float)((y2-y3) * (x1-x3) + (x3-x2) * (y1-y3));
		if(abs(det) <= 0.0001f)
		{
			m[0] = 0.333f;
			m[1] = 0.333f;
		}
		else
		{
			m[0] = (float)((y2-y3) * (x-x3) + (x3-x2) * (y-y3)) / det;
			m[1] = (float)((y3-y1) * (x-x3) + (x1-x3) * (y-y3)) / det;
		}
		clamp(m[0], 0.0f, 1.0f);
		clamp(m[1], 0.0f, 1.0f);

		m[2] = 1.0f - m[0] - m[1];
		textur[y][x] = t[0] * m[0] + t[1] * m[1] + t[2] * m[2];*/

		textur[y][x] = GetNormalizedvalue(triangle->quality, normalized);
	}

	void CSDFController::DrawPixel(float** textur, Face* triangle, unsigned int x, unsigned int y, int x1, int x2, int x3, int y1, int y2, int y3, float value)
	{
		if ((x >= Nastavenia->SDF_Smoothing_Texture) || (y >= Nastavenia->SDF_Smoothing_Texture))
		{
			return;
		}

		textur[y][x] = value;
	}
}