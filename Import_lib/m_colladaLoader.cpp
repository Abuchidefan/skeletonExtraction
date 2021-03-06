#include "m_colladaLoader.h"

using namespace std;
//using namespace  System::Runtime::InteropServices;


// IMPORT

// This function checks to see if a user data object has already been attached to
// the DOM object. If so, that object is casted from void* to the appropriate type
// and returned, otherwise the object is created and attached to the DOM object
// via the setUserData method.
/*template<typename MyType, typename DomType>
MyType& lookup(DomType& domObject) {
	if (!domObject.getUserData())
		domObject.setUserData(new MyType(domObject));
	return *(MyType*)(domObject.getUserData());
}

// This function traverses all the DOM objects of a particular type and frees
// destroys the associated user data object.
template<typename MyType, typename DomType>
void freeConversionObjects(DAE& dae) {
	vector<daeElement*> elts = dae.getDatabase()->typeLookup(DomType::ID());
	for (size_t i = 0; i < elts.size(); i++)
		delete (MyType*)elts[i]->getUserData();
}

 unsigned int getMaxOffset( domInput_local_offset_Array &input_array ) {
   	unsigned int maxOffset = 0;
  	for ( unsigned int i = 0; i < input_array.getCount(); i++ ) {
   		if ( input_array[i]->getOffset() > maxOffset ) {
   			maxOffset = (unsigned int)input_array[i]->getOffset();
   		}
   	}
   	return maxOffset;
  }


Node::Node(domNode& node) {
	// Recursively convert all child nodes. First iterate over the <node> elements.
	for (size_t i = 0; i < node.getNode_array().getCount(); i++)
		childNodes.push_back(&lookup<Node, domNode>(*node.getNode_array()[i]));

	for (size_t i = 0; i < node.getMatrix_array().getCount(); i++){
		domMatrix * m = node.getMatrix_array()[i];
		domFloat4x4 matrix = m->getValue();
		vMatrix.push_back(matrix);
	}

	for (size_t i = 0; i < node.getTranslate_array().getCount(); i++){
		domTranslate * t = node.getTranslate_array()[i];
		domFloat3 translate = t->getValue();
		vTranslate.push_back(translate);
	}

	for (size_t i = 0; i < node.getRotate_array().getCount(); i++){
		domRotate * r = node.getRotate_array()[i];
		domFloat4 rotate = r->getValue();
		vRotate.push_back(rotate);
	}

	for (size_t i = 0; i < node.getScale_array().getCount(); i++){
		domScale * s = node.getScale_array()[i];
		domFloat3 scale = s->getValue();
		vScale.push_back(scale);
	}

	// Then iterate over the <instance_node> elements.
	for (size_t i = 0; i < node.getInstance_node_array().getCount(); i++) {
		domNode* child = daeSafeCast<domNode>(
			node.getInstance_node_array()[i]->getUrl().getElement());
			childNodes.push_back(&lookup<Node, domNode>(*child));
	}

	// If it is a skeleton node
	isBone =  (node.getType() == NODE_ENUM_JOINT && node.getSid());

	if (isBone){
		sId = node.getSid();
	}

	// Then iterate over the <instance_controller> elements.
	for (size_t i = 0; i < node.getInstance_controller_array().getCount(); i++) {
		domInstance_controller * dictrl = node.getInstance_controller_array()[i];

		domController* child = daeSafeCast<domController>(node.getInstance_controller_array()[i]->getUrl().getElement());
		controllers.push_back(&lookup<Controller, domController>(*child));

		// load materials
		if (dictrl->getBind_material() != NULL && dictrl->getBind_material()->getTechnique_common() != NULL)
			for (size_t j = 0; j < dictrl->getBind_material()->getTechnique_common()->getInstance_material_array().getCount(); j++) {	
				domInstance_material* instanceMtl = daeSafeCast<domInstance_material>(
				dictrl->getBind_material()->getTechnique_common()->getInstance_material_array()[j]);
				if (instanceMtl != NULL){
					controllers.back()->symbols.push_back(instanceMtl->getSymbol());
					domMaterial* mtl = daeSafeCast<domMaterial>(instanceMtl->getTarget().getElement());
					Material& convertedMtl = lookup<Material, domMaterial>(*mtl);
					controllers.back()->materials.push_back(convertedMtl);
				}
			}
	}

	skinning = (node.getInstance_controller_array().getCount() > 0);

	// Iterate over all the <instance_geometry> elements
	for (size_t i = 0; i < node.getInstance_geometry_array().getCount(); i++) {
		domInstance_geometry* instanceGeom = node.getInstance_geometry_array()[i];
		domGeometry* geom = daeSafeCast<domGeometry>(instanceGeom->getUrl().getElement());

		// Lookup the material that we should apply to the <geometry>. In a real app
		// we'd need to worry about having multiple <instance_material>s, but in this
		// test let's just convert the first <instance_material> we find.
		domInstance_material* instanceMtl = daeSafeCast<domInstance_material>(
			instanceGeom->getDescendant("instance_material"));

		// Now convert the geometry, add the result to our list of meshes, and assign
		// the mesh a material.
		meshes.push_back(&lookup<CMesh, domGeometry>(*geom));
		if (instanceMtl != NULL){
			domMaterial* mtl = daeSafeCast<domMaterial>(instanceMtl->getTarget().getElement());
			Material& convertedMtl = lookup<Material, domMaterial>(*mtl);
			meshes.back()->mtl = &convertedMtl;
		} else
			meshes.back()->mtl = NULL;
		
	}

}

BYTE* sRGBColor::toByte3(){
	BYTE color[3];
	color[0] = (int)(fRed * 255.0);
	color[1] = (int)(fGreen * 255.0);
	color[2] = (int)(fBlue * 255.0);
	return color;
}

sRGBColor cot2rgb(domFx_common_color_or_texture* ct){
	if (ct != NULL && ct->getColor() != NULL) {
		domFx_color col = ct->getColor()->getValue();
		return sRGBColor(col[0],col[1],col[2],col[3]);
    }
	return NULL;	
}

float fop2float(domFx_common_float_or_param* fp){
	if (fp != NULL && fp->getFloat() != NULL)
		return fp->getFloat()->getValue();
	return 0.0;
}

Controller::Controller(domController& ctrl){
	domSkin * skin = ctrl.getSkin();
	if (skin != NULL){
		// bindshape matrix
		if (skin->getBind_shape_matrix() != NULL){
			bindShape = ctrl.getSkin()->getBind_shape_matrix()->getValue();
		}
		// joints and bind pose matrices
		if (skin->getJoints() != NULL){
			for(int i=0;i<ctrl.getSkin()->getJoints()->getInput_array().getCount();i++){
				int numberOfInputs = 2;

				if(strcmpi(ctrl.getSkin()->getJoints()->getInput_array()[i]->getSemantic(), "JOINT")==0){
					domSource * joints = (domSource *)(daeElement*)ctrl.getSkin()->getJoints()->getInput_array()[i]->getSource().getElement();
					for (int j=0; j<joints->getName_array()->getValue().getCount(); j++){
						CBone bone;
						bone.id = joints->getName_array()->getValue().get(j);
						bones.push_back(bone);
						boneIndices[bone.id] = bones.size() - 1;
					}
				}
				if(strcmpi(ctrl.getSkin()->getJoints()->getInput_array()[i]->getSemantic(), "INV_BIND_MATRIX")==0){
					domSource * ibmatrices = (domSource *)(daeElement*)ctrl.getSkin()->getJoints()->getInput_array()[i]->getSource().getElement();
					int stride = ibmatrices->getTechnique_common()->getAccessor()->getStride();
					for (int j=0; j<ibmatrices->getFloat_array()->getValue().getCount() / stride; j++){
						bones[j].m_InverseBindMatrix = TNT::Array2D<float>(4,4,0.0f);
						for (int m=0; m<4; m++)
							for (int n=0; n<4; n++)
								bones[j].m_InverseBindMatrix[m][n] = ibmatrices->getFloat_array()->getValue().get(j * stride + m * 4 + n);
					}
				}
			}
		}
		// indices and weights
		if (skin->getVertex_weights() != NULL){
			int jointoffset = -255, weightoffset = -255;
			for(int i=0;i<ctrl.getSkin()->getVertex_weights()->getInput_array().getCount();i++){					
				int numberOfInputs = (int)getMaxOffset(ctrl.getSkin()->getVertex_weights()->getInput_array()) + 1;

				if(strcmpi(ctrl.getSkin()->getVertex_weights()->getInput_array()[i]->getSemantic(), "JOINT")==0)
					jointoffset = ctrl.getSkin()->getVertex_weights()->getInput_array()[i]->getOffset();
				if(strcmpi(ctrl.getSkin()->getVertex_weights()->getInput_array()[i]->getSemantic(), "WEIGHT")==0)
					weightoffset = ctrl.getSkin()->getVertex_weights()->getInput_array()[i]->getOffset();

				domSource * weightsource = NULL;
				for (int j=0; j<ctrl.getSkin()->getVertex_weights()->getInput_array().getCount();j++)
					if(strcmpi(ctrl.getSkin()->getVertex_weights()->getInput_array()[j]->getSemantic(), "WEIGHT")==0)
						weightsource = (domSource *)(daeElement*)ctrl.getSkin()->getVertex_weights()->getInput_array()[j]->getSource().getElement();

				domList_of_uints vc = ctrl.getSkin()->getVertex_weights()->getVcount()->getValue();
				domList_of_ints v = ctrl.getSkin()->getVertex_weights()->getV()->getValue();

				int index = 0;
				int vcCount = vc.getCount();

				weights = new float[vcCount * config.NUM_OF_CTRL_BONES];
				indices = new float[vcCount * config.NUM_OF_CTRL_BONES];

				for (int k=0; k < vcCount; k++){
					int numOfCtrPoints = vc.get(k);

					int intq = config.NUM_OF_CTRL_BONES / numOfCtrPoints;
					int mod = config.NUM_OF_CTRL_BONES % numOfCtrPoints;
					for (int l=0; l<numOfCtrPoints; l++){
						for (int m=0; m<intq; m++){
							weights[k * config.NUM_OF_CTRL_BONES + (l * intq) + m] = weightsource->getFloat_array()->getValue().get(v.get((index + l) * numberOfInputs + 1));
							indices[k * config.NUM_OF_CTRL_BONES + (l * intq) + m] = v.get((index + l) * numberOfInputs) ;
						}
					}
					for (int l=0; l<mod; l++){
						weights[k * config.NUM_OF_CTRL_BONES + (intq * numOfCtrPoints) + l] = weightsource->getFloat_array()->getValue().get(v.get((index + (config.NUM_OF_CTRL_BONES - l)) * numberOfInputs + 1));
						indices[k * config.NUM_OF_CTRL_BONES + (intq * numOfCtrPoints) + l] = v.get((index + (config.NUM_OF_CTRL_BONES - l)) * numberOfInputs);
					}
					
					// normilize weights to have sum 1.0
					float wsum = 0.0f;
					for (int l=0; l<numOfCtrPoints; l++)
						wsum += weights[k * config.NUM_OF_CTRL_BONES + l];
					if (wsum != 1.0f)
						for (int l=0; l<numOfCtrPoints; l++)
							weights[k * config.NUM_OF_CTRL_BONES + l] /= wsum;
					
					index += numOfCtrPoints;
				}
				for (int j = 0; j < vcCount; j++) {
					for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
						logg.log(LOG_LEVEL_DUMP, "indexy pre vertex z fajlu : " ,j);
						logg.log(LOG_LEVEL_DUMP, "index", indices[j * config.NUM_OF_CTRL_BONES + k]);
					}

					for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
						logg.log(LOG_LEVEL_DUMP, "vahy pre vertex z fajlu : " , j);
						logg.log(LOG_LEVEL_DUMP, "vaha ",weights[j * config.NUM_OF_CTRL_BONES + k]);
					}
				}
			}
		}
		geoElement = skin->getSource().getElement();
	}
}

Material::Material(domMaterial& mtl) {
		// Grab the <effect> from the <material> and initalize the parameters
	xsAnyURI* puriEffect = &(mtl.getInstance_effect()->getUrl());
	puriEffect->resolveElement();
	domEffect* pdomEff = (domEffect*)(daeElement*)puriEffect->getElement();
	

	// find <profile_COMMON>
	domProfile_common* pProfCMN = 0;
        domFx_profile_Array* pProfAry = &pdomEff->getFx_profile_array();
        const unsigned int nProf = (unsigned int)pProfAry->getCount();
        for(unsigned int iProf = 0; iProf < nProf; iProf ++)
        {
            domFx_profile* pProf = pProfAry->get(iProf).cast();
			pProfCMN = pProf->getProfile_COMMON();
            //if(strcmp(pProf->getTypeName(), "profile_COMMON") == 0)
              //     pProfCMN = (domProfile_common*)pProf;
        }

		if(pProfCMN == 0){
	        // profile common not found
			logg.log(LOG_LEVEL_ERROR, "profile common not found");
			return;
		}
	
	        domProfile_common::domTechnique* pTech = pProfCMN->getTechnique();
			if(pTech == 0){
	            // profile tecnique not found
				logg.log(LOG_LEVEL_ERROR, "profile tecnique not found");
			}
	
			domProfile_common::domTechnique::domPhong* pPhong = pTech->getPhong();
			domProfile_common::domTechnique::domBlinn* pBlinn = pTech->getBlinn();
			if(pPhong != 0){
				// phong found

				domFx_common_color_or_texture* pDiffuse = pPhong->getDiffuse();
				rgbDiffuse = cot2rgb(pDiffuse);	

				if (pDiffuse->getTexture() != NULL){
					textureID = 0;
					for (int j=0; j<pProfCMN->getNewparam_array().getCount(); j++){
						daeElement * surface = pProfCMN->getNewparam_array()[j]->getDescendant("surface");
						if (surface != NULL){
							daeElement * ifrom = surface->getDescendant("init_from");
							if (ifrom != NULL)
								refToTexture = ifrom->getCharData();
						}
					}

				} else
					textureID = -1;

				domFx_common_color_or_texture* pAmbient = pPhong->getAmbient();
				rgbAmbient = cot2rgb(pAmbient);

				domFx_common_color_or_texture* pSpecular = pPhong->getSpecular();
				rgbSpecular = cot2rgb(pSpecular);

				domFx_common_float_or_param* pShinnenes = pPhong->getShininess();
		
				shininnes = fop2float(pShinnenes);
			} else
				if(pBlinn != 0){
					// blinn found

					domFx_common_color_or_texture* pDiffuse = pBlinn->getDiffuse();
					rgbDiffuse = cot2rgb(pDiffuse);	

					if (pDiffuse->getTexture() != NULL){
						textureID = 0;
						for (int j=0; j<pProfCMN->getNewparam_array().getCount(); j++){
							daeElement * surface = pProfCMN->getNewparam_array()[j]->getDescendant("surface");
							if (surface != NULL){
								daeElement * ifrom = surface->getDescendant("init_from");
								if (ifrom != NULL)
									refToTexture = ifrom->getCharData();
							}
						}

					} else
						textureID = -1;

					domFx_common_color_or_texture* pAmbient = pBlinn->getAmbient();
					rgbAmbient = cot2rgb(pAmbient);

					domFx_common_color_or_texture* pSpecular = pBlinn->getSpecular();
					rgbSpecular = cot2rgb(pSpecular);

					domFx_common_float_or_param* pShinnenes = pBlinn->getShininess();
			
					shininnes = fop2float(pShinnenes);
				}
			strMaterialName = pdomEff->getID();
			initialized	 = true;
}

Material::Material(string str){
	strMaterialName = str; 
	initialized = false;
}

void CMesh::ConstructTristrips(domMesh *thisMesh, domTristrips *thisTristrip)
 {
	string materialStr;
	thisTristrip->getAttribute("material", materialStr);

	if (materialStr != "")
	mtl = new Material(materialStr);
	
 	domTriangles *thisTriangles = (domTriangles *)thisMesh->createAndPlace("triangles");
	unsigned int triangles = 0;
 	thisTriangles->setMaterial(thisTristrip->getMaterial());
 	domP* p_triangles = (domP*)thisTriangles->createAndPlace("p");
 
 	for(int i=0; i<(int)(thisTristrip->getInput_array().getCount()); i++)
 	{
		thisTriangles->placeElement( thisTristrip->getInput_array()[i]->clone() );
 	}

 	// Get the number of inputs and primitives for the polygons array
  	int numberOfInputs = (int)getMaxOffset(thisTristrip->getInput_array()) + 1;
 
 	unsigned int offset = 0;
 
	// TrifanTriangles
	int numberOfPrimitives = (int)(thisTristrip->getP_array().getCount());
	for(int j = 0; j < numberOfPrimitives; j++)
 	{	
		domP * primitive = thisTristrip->getP_array()[j];
		int triangleCount = primitive->getValue().getCount() / numberOfInputs - 2;
		// Write out the primitives as triangles, just fan using the first element as the base
 		for(int k = 0; k < triangleCount; k++)
 		{
			Poly poly;
 			// First vertex
			for(int l = 0; l < numberOfInputs; l++){
 				p_triangles->getValue().append(primitive->getValue()[k * numberOfInputs + l]);
			}
			poly.pindices[0] = primitive->getValue()[k * numberOfInputs];
 			// Second vertex
			for(int l = 0; l < numberOfInputs; l++){
 				p_triangles->getValue().append(primitive->getValue()[(k + 1) * numberOfInputs + l]);
			}
			poly.pindices[1] = primitive->getValue()[(k + 1) * numberOfInputs];
 			// Third vertex
			for(int l = 0; l < numberOfInputs; l++){
  				p_triangles->getValue().append(primitive->getValue()[(k + 2) * numberOfInputs + l]);
			}
			poly.pindices[2] = primitive->getValue()[(k + 2) * numberOfInputs];
			vPolygons.push_back(poly);
			triangles++;
 		}
	}
 
 	thisTriangles->setCount( triangles );
 	int triangleVerticesCount = p_triangles->getValue().getCount() / numberOfInputs;
	iVerticesCount += triangleVerticesCount;
  
  
 	DWORD veroffset = 0;
 	int texoffset = -255, noroffset = -255;
 	for(int i=0;i<thisTriangles->getInput_array().getCount();i++)
 	{
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "VERTEX")==0)
 			veroffset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "TEXCOORD")==0)
 			texoffset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "NORMAL")==0)
  			noroffset = thisTriangles->getInput_array()[i]->getOffset();
  	}
  
 
 	for(int i =0;i<triangleVerticesCount;i++)
 	{
 		vIndices.push_back((int)p_triangles->getValue()[i*numberOfInputs+veroffset]);
 
 		if(texoffset!=-255)
 		{
  			vTexVerts.push_back(CVector2(thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2)
				,thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2+1
 				)));
  		}
 
  		if(noroffset!=-255)
  		{
 		
			vNormals.push_back(CVector3(
  				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3
				),
  				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+1
  				),
 				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+2
 				)
  			));
  		}
 	}
}

void CMesh::ConstructTrifans(domMesh *thisMesh, domTrifans *thisTrifans)
 {
	string materialStr;
	thisTrifans->getAttribute("material", materialStr);

	if (materialStr != "")
	mtl = new Material(materialStr);
	
 	domTriangles *thisTriangles = (domTriangles *)thisMesh->createAndPlace("triangles");
	unsigned int triangles = 0;
 	thisTriangles->setMaterial(thisTrifans->getMaterial());
 	domP* p_triangles = (domP*)thisTriangles->createAndPlace("p");
 
 	for(int i=0; i<(int)(thisTrifans->getInput_array().getCount()); i++)
 	{
		thisTriangles->placeElement( thisTrifans->getInput_array()[i]->clone() );
 	}

 	// Get the number of inputs and primitives for the polygons array
  	int numberOfInputs = (int)getMaxOffset(thisTrifans->getInput_array()) + 1;

 	unsigned int offset = 0;
 
	// TrifanTriangles
	int numberOfPrimitives = (int)(thisTrifans->getP_array().getCount());
	for(int j = 0; j < numberOfPrimitives; j++)
 	{	

		domP * primitive = thisTrifans->getP_array()[j];
		int triangleCount = primitive->getValue().getCount() / numberOfInputs - 2;

		// Write out the primitives as triangles, just fan using the first element as the base
 		for(int k = 0; k < triangleCount; k++)
 		{
			Poly poly;
 			// First vertex
			for(int l = 0; l < numberOfInputs; l++){
  				p_triangles->getValue().append(primitive->getValue()[l]);
			}
			poly.pindices[0] = primitive->getValue()[0];
 			// Second vertex
			for(int l = 0; l < numberOfInputs; l++){  				
 				p_triangles->getValue().append(primitive->getValue()[(k + 1) * numberOfInputs + l]);
			}
			poly.pindices[1] = primitive->getValue()[(k + 1) * numberOfInputs];
 			// Third vertex
			for(int l = 0; l < numberOfInputs; l++){
				p_triangles->getValue().append(primitive->getValue()[(k + 2) * numberOfInputs + l]);
			}
			poly.pindices[2] = primitive->getValue()[(k + 2) * numberOfInputs];
			triangles++;
			vPolygons.push_back(poly);
 		}
	}
 
 	thisTriangles->setCount( triangles );
 	int triangleVerticesCount = p_triangles->getValue().getCount() / numberOfInputs;
	iVerticesCount += triangleVerticesCount;
  
  
 	DWORD veroffset = 0;
 	int texoffset = -255, noroffset = -255;
 	for(int i=0;i<thisTriangles->getInput_array().getCount();i++)
 	{
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "VERTEX")==0)
 			veroffset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "TEXCOORD")==0)
 			texoffset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "NORMAL")==0)
  			noroffset = thisTriangles->getInput_array()[i]->getOffset();
  	}
  
 
 	for(int i =0;i<triangleVerticesCount;i++)
 	{
 		vIndices.push_back((int)p_triangles->getValue()[i*numberOfInputs+veroffset]);
 		if(texoffset!=-255)
 		{
			vTexVerts.push_back(CVector2(thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2)
				,thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2+1
 				)));
  		}
 
  		if(noroffset!=-255)
  		{
 			vNormals.push_back(CVector3(
  				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3
				),
  				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+1
  				),
 				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+2
 				)
  			));
  		}
 	}
}

 void CMesh::ConstructTriangles(domMesh *thisMesh, domTriangles *thisTriangles)
 {
	string materialStr;
	thisTriangles->getAttribute("material", materialStr);

	if (materialStr != "")
	mtl = new Material(materialStr);
	
 	int numberOfInputs = (int)getMaxOffset(thisTriangles->getInput_array()) +1;
 	int numberOfTriangleP = (int)(thisTriangles->getP()->getValue().getCount());	
 	int triangleVerticesCount = numberOfTriangleP / (numberOfInputs);
	iVerticesCount += triangleVerticesCount;
  
	
 	DWORD offset = 0;
 	int texoffset = -255, noroffset = -255;
 	for(int i=0;i<thisTriangles->getInput_array().getCount();i++)
 	{
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "VERTEX")==0)
 			offset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "TEXCOORD")==0)
 			texoffset = thisTriangles->getInput_array()[i]->getOffset();
		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "NORMAL")==0)
  			noroffset = thisTriangles->getInput_array()[i]->getOffset();
  	}

	
	for(int p=0;p<triangleVerticesCount / 3;p++){
		Poly poly;
 		for(int i=0;i<3;i++)
  		{
			vIndices.push_back(thisTriangles->getP()->getValue().get((p*3 + i)*numberOfInputs+offset));
			
			poly.pindices[i] = thisTriangles->getP()->getValue().get((p*3 + i)*numberOfInputs+offset);

  
  			if(texoffset!=-255)
  			{

				vTexVerts.push_back(CVector2(thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
 					thisTriangles->getP()->getValue().get((p*3 + i)*numberOfInputs+texoffset)*2)
					,thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
  					thisTriangles->getP()->getValue().get((p*3 + i)*numberOfInputs+texoffset)*2+1
 					)));
	  		
  			}
			if(noroffset!=-255)
  			{
	 		
				vNormals.push_back(CVector3(
  					thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
  					thisTriangles->getP()->getValue().get((p*3 + i)*numberOfInputs+noroffset)*3
					),
  					thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 					thisTriangles->getP()->getValue().get((p*3 + i)*numberOfInputs+noroffset)*3+1
  					),
 					thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 					thisTriangles->getP()->getValue().get((p*3 + i)*numberOfInputs+noroffset)*3+2
 					)
  				));
  			}
		}
  	vPolygons.push_back(poly);
	}
}
  
 
  void CMesh::ConstructPolylist(domMesh *thisMesh, domPolylist *thisPolylist)
  {
	string materialStr;
	thisPolylist->getAttribute("material", materialStr);

	if (materialStr != "")
	mtl = new Material(materialStr);

   	domTriangles *thisTriangles = (domTriangles *)thisMesh->createAndPlace("triangles");
 	unsigned int triangles = 0;
 	thisTriangles->setMaterial(thisPolylist->getMaterial());
 	domP* p_triangles = (domP*)thisTriangles->createAndPlace("p");
 
 	for(int i=0; i<(int)(thisPolylist->getInput_array().getCount()); i++)
 	{
 		thisTriangles->placeElement( thisPolylist->getInput_array()[i]->clone() );
 	}

 	// Get the number of inputs and primitives for the polygons array
  	int numberOfInputs = (int)getMaxOffset(thisPolylist->getInput_array()) + 1;
 	int numberOfPrimitives = (int)(thisPolylist->getVcount()->getValue().getCount());
 
 	unsigned int offset = 0;
 
	// PolylistTriangles
  	for(int j = 0; j < numberOfPrimitives; j++)
 	{	
  		int triangleCount = thisPolylist->getVcount()->getValue()[j] -2;
		// Write out the primitives as triangles, just fan using the first element as the base
 		int idx = numberOfInputs;
 		for(int k = 0; k < triangleCount; k++)
 		{
			Poly poly;
 			// First vertex
 			for(int l = 0; l < numberOfInputs; l++)
 			{
 				p_triangles->getValue().append(thisPolylist->getP()->getValue()[offset + l]);
  			}
			poly.pindices[0] = thisPolylist->getP()->getValue()[offset];
 			// Second vertex
 			for(int l = 0; l < numberOfInputs; l++)
 			{
 				p_triangles->getValue().append(thisPolylist->getP()->getValue()[offset + idx + l]);
			}
			poly.pindices[1] = thisPolylist->getP()->getValue()[offset + idx];
 			// Third vertex
 			idx += numberOfInputs;
  			for(int l = 0; l < numberOfInputs; l++)
 			{
 				p_triangles->getValue().append(thisPolylist->getP()->getValue()[offset + idx + l]);
			}
			poly.pindices[2] = thisPolylist->getP()->getValue()[offset + idx];
  			//thisTriangles->setCount(thisTriangles->getCount()+1);
 			triangles++;
			vPolygons.push_back(poly);
 		}
 		offset += thisPolylist->getVcount()->getValue()[j] * numberOfInputs;
 	}
 
 	thisTriangles->setCount( triangles );
 	int triangleVerticesCount = p_triangles->getValue().getCount() / numberOfInputs;
	iVerticesCount += triangleVerticesCount;
  
  
 	DWORD veroffset = 0;
 	int texoffset = -255, noroffset = -255;
 	for(int i=0;i<thisTriangles->getInput_array().getCount();i++)
 	{
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "VERTEX")==0)
 			veroffset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "TEXCOORD")==0)
 			texoffset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "NORMAL")==0)
  			noroffset = thisTriangles->getInput_array()[i]->getOffset();
  	}
  
 
 	for(int i =0;i<triangleVerticesCount;i++)
 	{
 		vIndices.push_back((int)p_triangles->getValue()[i*numberOfInputs+veroffset]);
 
 		if(texoffset!=-255)
 		{
  			vTexVerts.push_back(CVector2(thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2)
				,thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2+1
 				)));
  		}
 
  		if(noroffset!=-255)
  		{
 		
			vNormals.push_back(CVector3(
  				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3
				),
  				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+1
  				),
 				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+2
 				)
  			));
  		}
 	}
  }

void CMesh::ConstructPolygon(domMesh *thisMesh, domPolygons *thisPolygons)
{  
	string materialStr;
	thisPolygons->getAttribute("material", materialStr);

	if (materialStr != "")
	mtl = new Material(materialStr);
 
  	domTriangles *thisTriangles = (domTriangles *)thisMesh->createAndPlace("triangles");
  	thisTriangles->setCount( 0 );
  	thisTriangles->setMaterial(thisPolygons->getMaterial());
  	domP* p_triangles = (domP*)thisTriangles->createAndPlace("p");
  
 	// Give the new <triangles> the same <input> and <parameters> as the old <polygons>
  	for(int i=0; i<(int)(thisPolygons->getInput_array().getCount()); i++)
  	{
  		thisTriangles->placeElement( thisPolygons->getInput_array()[i]->clone() );
  	}
  	// Get the number of inputs and primitives for the polygons array
  	int numberOfInputs = (int)getMaxOffset(thisPolygons->getInput_array()) +1;
  	int numberOfPrimitives = (int)(thisPolygons->getP_array().getCount());
  
  	// Polygons?Triangles
  	for(int j = 0; j < numberOfPrimitives; j++)
  	{
 		// Check the polygons for consistancy (some exported files have had the wrong number of indices)
  		domP * thisPrimitive = thisPolygons->getP_array()[j];
  		int elementCount = (int)(thisPrimitive->getValue().getCount());
  		if((elementCount%numberOfInputs) != 0)
  		{
  //			cerr<<"Primitive "<<j<<" has an element count "<<elementCount<<" not divisible by the number of inputs "<<numberOfInputs<<"\n";
  			continue;
 		}
  		else
  		{
  			int triangleCount = (elementCount/numberOfInputs)-2;
  			// Write out the primitives as triangles, just fan using the first element as the base
  			int idx = numberOfInputs;
  			for(int k = 0; k < triangleCount; k++)
  			{
				Poly poly;
  				// First vertex
  				for(int l = 0; l < numberOfInputs; l++)
  				{
  					p_triangles->getValue().append(thisPrimitive->getValue()[l]);
				}
				poly.pindices[0] = thisPrimitive->getValue()[0];
  				// Second vertex
  				for(int l = 0; l < numberOfInputs; l++)
  				{
  					p_triangles->getValue().append(thisPrimitive->getValue()[idx + l]);
				}
				poly.pindices[1] = thisPrimitive->getValue()[idx];
  				// Third vertex
  				idx += numberOfInputs;
  				for(int l = 0; l < numberOfInputs; l++)
  				{
  					p_triangles->getValue().append(thisPrimitive->getValue()[idx + l]);
 				}
				poly.pindices[1] = thisPrimitive->getValue()[idx];
  				thisTriangles->setCount(thisTriangles->getCount()+1);
				vPolygons.push_back(poly);
  			}
  		}
 	}
  	
  	int triangleVerticesCount = p_triangles->getValue().getCount() / numberOfInputs;
	iVerticesCount += triangleVerticesCount;
 
 	DWORD veroffset = 0;
  	int texoffset = -255, noroffset = -255;
  	for(int i=0;i<thisTriangles->getInput_array().getCount();i++)
  	{
  		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "VERTEX")==0)
 			veroffset = thisTriangles->getInput_array()[i]->getOffset();
  		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "TEXCOORD")==0)
  			texoffset = thisTriangles->getInput_array()[i]->getOffset();
 		if(strcmpi(thisTriangles->getInput_array()[i]->getSemantic(), "NORMAL")==0)
  			noroffset = thisTriangles->getInput_array()[i]->getOffset();
  	}
  
  
  	for(int i =0;i<triangleVerticesCount;i++)
  	{
		vIndices.push_back((int)p_triangles->getValue()[i*numberOfInputs+veroffset]);
  
  		if(texoffset!=-255)
  		{
  			vTexVerts.push_back(CVector2( thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2)
  				,
  			thisMesh->getSource_array()[2]->getFloat_array()->getValue().get(
  				thisTriangles->getP()->getValue().get(i*numberOfInputs+texoffset)*2+1
  				)));
  		}
  
  		if(noroffset!=-255)
  		{
 			vNormals.push_back(CVector3(
 				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3
 				),
 				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+1
 				),
 				thisMesh->getSource_array()[1]->getFloat_array()->getValue().get(
 				thisTriangles->getP()->getValue().get(i*numberOfInputs+noroffset)*3+2
				)
 			));
 		}
 	}
 
 
}
 




CMesh::CMesh(domGeometry& geom) {
		mtl = NULL;
		// Parse the <geometry> element, extract vertex data, etc
		domMesh *thisMesh = geom.getMesh();


		iVertsCount =  thisMesh->getSource_array()[0]->getFloat_array()->getCount()/3;

		pVertices = new CVector3[iVertsCount];

		for(int i=0;i<iVertsCount;i++)
  		{

			pVertices[i] = CVector3(
 				thisMesh->getSource_array()[0]->getFloat_array()->getValue().get(i*3),
				thisMesh->getSource_array()[0]->getFloat_array()->getValue().get(i*3+1),
 				thisMesh->getSource_array()[0]->getFloat_array()->getValue().get(i*3+2)
 				);
		}

		iVerticesCount = 0;

		//Triangles
 		int triangleElementCount = (int)(thisMesh->getTriangles_array().getCount());
  		for(int currentTriangle = 0;currentTriangle < triangleElementCount; currentTriangle++)
  		{
  			domTriangles* thisTriangles = thisMesh->getTriangles_array().get(currentTriangle);
  
  			ConstructTriangles(thisMesh, thisTriangles);
  		}

		//thisMesh->remove

		//Tristrips
		int tristripElementCount = (int)(thisMesh->getTristrips_array().getCount());
  		for(int currentTristrip = 0;currentTristrip < tristripElementCount; currentTristrip++)
  		{
			domTristrips* thisTristrips = thisMesh->getTristrips_array().get(currentTristrip);
  
  			ConstructTristrips(thisMesh, thisTristrips);
  		}

		//Trifans
		int trifanElementCount = (int)(thisMesh->getTrifans_array().getCount());
  		for(int currentTrifan = 0;currentTrifan < trifanElementCount; currentTrifan++)
  		{
  			domTrifans* thisTrifans = thisMesh->getTrifans_array().get(currentTrifan);
  
  			ConstructTrifans(thisMesh, thisTrifans);
  		}

  		// Polylist
		int polylistElementCount = (int)(thisMesh->getPolylist_array().getCount());
  		for(int currentPolylist = 0; currentPolylist < polylistElementCount; currentPolylist++)
  		{		
  			domPolylist *thisPolylist = thisMesh->getPolylist_array().get(currentPolylist);
  
  			ConstructPolylist( thisMesh, thisPolylist);
  		}
 
  		//Polygons
  		int polygonesElementCount = (int)(thisMesh->getPolygons_array().getCount());
  		for(int currentPolygones = 0; currentPolygones < polygonesElementCount; currentPolygones++)
  		{
  			domPolygons* thisPolygons = thisMesh->getPolygons_array().get(currentPolygones);
  
  			ConstructPolygon( thisMesh, thisPolygons);
  		}

	}

void CColladaLoader::loadTextures(domImage_Array* pImageAry)
	{
	        unsigned int nImage = (unsigned int)pImageAry->getCount();
	        for(unsigned int iImage = 0; iImage < nImage; iImage ++)
	        {
	                domImage* pImage = pImageAry->get(iImage).cast();
					string str;
					domImage_source::domRef * ref =  pImage->getInit_from()->getRef();
					string strFilename = "";
					if (ref != NULL){
						strFilename = ref->getValue().str();
					}
					if (strFilename == ""){
						strFilename = pImage->getInit_from()->getCharData();
					}
					imageLib[pImage->getID()] = strFilename;
				 }
}


vector<Node> CColladaLoader::convertModel(domCOLLADA& root) {
	// We need to convert the model from the DOM's representation to our internal representation.
	// First find a <visual_scene> to load. In a real app we would look for and load all
	// the <visual_scene>s in a document, but for this app we just convert the first
	// <visual_scene> we find.
	domVisual_scene* visualScene = daeSafeCast<domVisual_scene>(root.getDescendant("visual_scene"));

	// Now covert all the <node>s in the <visual_scene>. This is a recursive process,
	// so any child nodes will also be converted.
	domNode_Array& nodes = visualScene->getNode_array();

	vector<Node> nodeVector;
	for (size_t i = 0; i < nodes.getCount(); i++)
		nodeVector.push_back(lookup<Node, domNode>(*nodes[i]));

	// parse <library_images> and load textures
    domLibrary_images_Array* pLibImgAry = &(root.getLibrary_images_array());
    unsigned int nLibImg = (unsigned int) pLibImgAry->getCount();
    for(unsigned int iLibImg = 0; iLibImg < nLibImg; iLibImg ++)
    {
            domLibrary_images* pLibImg = pLibImgAry->get(iLibImg);

            loadTextures(&pLibImg->getImage_array());
    }

	return nodeVector;
}


CVector3 multiplyVectorAndMatrix(CVector3 v, domFloat4x4 m){
	CVector3 n;
	n.x = v.x * m[0] + v.y * m[1] + v.z * m[2] + m[3];
	n.y = v.x * m[4] + v.y * m[5] + v.z * m[6] + m[7];
	n.z = v.x * m[8] + v.y * m[9] + v.z * m[10] + m[11];
	return n;
}

domFloat4x4 addMatrixToMatrix(domFloat4x4 m1, domFloat4x4 m2){
	domFloat4x4 m;
	for (int i=0; i<4; i++)
		for (int j=0; j<4; j++){
			float r = 0.0f;
			for (int k=0; k<4; k++)
				r += m1[4 * i + k] * m2[4 * k + j];
		m.append(r);
		}
	return m;
}

TNT::Array2D<float> VFloat4x4ToTNTMatrix(domFloat4x4 m){
	TNT::Array2D<float> ret(4,4,0.0f);
	for (int i=0; i<4; i++)
		for (int j=0; j<4; j++)
			ret[i][j] = m[j + i*4];
	return ret;
}

TNT::Array2D<float> ScaleFloat3ToTNTMatrix(domFloat3 m){
	TNT::Array2D<float> ret(4,4,0.0f);
	for (int i=0; i<4; i++)
		ret[i][i] = 1.0;
	for (int i=0; i<3; i++)
		ret[i][i] = m[i];
	return ret;
}

TNT::Array2D<float> TranslateFloat3ToTNTMatrix(domFloat3 m){
	TNT::Array2D<float> ret(4,4,0.0f);
	for (int i=0; i<4; i++)
		ret[i][i] = 1.0;
	for (int i=0; i<3; i++)
		ret[i][3] = m[i];
	return ret;
}

TNT::Array2D<float> RotateFloat4ToTNTMatrix(domFloat4 m){
	TNT::Array2D<float> rot3x3 = QuaternionToMatrix3x3(AxisRotToQuaternion(m[3] / 180.0 * PI, CVector3(m[0], m[1], m[2])));
	return Rot3x3ToRot4x4(rot3x3);
}

TNT::Array2D<float> SwapMatrix(bool bZup, bool bIs3dStudio){
	TNT::Array2D<float> ret(4,4,0.0f);
	ret[0][0] = 1.0f;
	ret[3][3] = 1.0f;
	if (bZup){
		ret[1][1] = -1.0f;
		ret[2][2] = -1.0f;
	} else if (bIs3dStudio){
		ret[1][2] = 1.0f;
		ret[2][1] = -1.0f;
	} else {
		ret[1][1] = 1.0f;
		ret[2][2] = 1.0f;
	}
	return ret;
}

/*TNT::Array2D<float> VFloat4x4ToTNTMatrix(domFloat4x4 m, bool bZup, bool bIs3dStudio){
	TNT::Array2D<float> ret(4,4,0.0f);
	for (int i=0; i<4; i++)
		for (int j=0; j<4; j++)
			ret[i][j] = m[j + i*4];
	if (bZup){
		for (int i=0; i<4; i++){
			ret[3][i] = -ret[3][i];
			ret[2][i] = -ret[2][i];
		}
	}
	else if (bIs3dStudio){
		for (int i=0; i<4; i++){
			ret[3][i] = ret[2][i];
			ret[2][i] = -ret[3][i];
		}
		}
	return ret;
}

TNT::Array2D<float> ScaleFloat3ToTNTMatrix(domFloat3 m, bool bZup, bool bIs3dStudio){
	TNT::Array2D<float> ret(4,4,0.0f);
	for (int i=0; i<4; i++)
		ret[i][i] = 1.0;
	if (bZup){
		ret[0][0] = m[0];
		ret[1][1] = -m[1];
		ret[2][2] = -m[2];
	}
	else if (bIs3dStudio){
			ret[0][0] = m[0];
			ret[1][1] = m[2];
			ret[2][2] = -m[1];
		}
		else
			for (int i=0; i<3; i++)
				ret[i][i] = m[i];
	return ret;
}

TNT::Array2D<float> TranslateFloat3ToTNTMatrix(domFloat3 m, bool bZup, bool bIs3dStudio){
	TNT::Array2D<float> ret(4,4,0.0f);
	for (int i=0; i<4; i++)
		ret[i][i] = 1.0;
	if (bZup){
		ret[0][3] = m[0];
		ret[1][3] = -m[1];
		ret[2][3] = -m[2];
	}
	else if (bIs3dStudio){
			ret[0][3] = m[0];
			ret[1][3] = m[2];
			ret[2][3] = -m[1];
		}
		else
			for (int i=0; i<3; i++)
				ret[i][3] = m[i];
	return ret;
}*/
/*
void copyObjectSkeletonShaderData(ObjectSkeletonShaderData * src, ObjectSkeletonShaderData * dest){
	dest->numOfBones = src->numOfBones;
	dest->numOfVertices = src->numOfVertices;
	memcpy(dest->indices, src->indices, src->numOfVertices * config.NUM_OF_CTRL_BONES * sizeof(float));
	memcpy(dest->weights, src->weights, src->numOfVertices * config.NUM_OF_CTRL_BONES * sizeof(float));
}

bool CColladaLoader::LoadColladaModel(int importStyle, structure::t3DModel * pModel, SN::SkeletonNode * nproot, vector<ObjectSkeletonShaderData*> &skeletonData, const string file) {

	// Load a document from disk
	DAE * dae = new DAE(NULL, NULL);

	logg.log(0, "Import Style: ", importStyle);
	
	domCOLLADA* root = dae->open("/"+file);

	if (!root)
		return false;

	bool bIs3dStudio = false;
	bool bZup = false;

	domAsset::domContributor::domAuthoring_tool* pAuthorTool = NULL;
	if(root->getAsset() != NULL){
		if (root->getAsset()->getContributor_array().getCount() > 0){
			root->getAsset()->getContributor_array().get(0)->getAuthoring_tool();
			if (pAuthorTool)
			{
				string strAuthorTool = pAuthorTool->getValue();
				if(strAuthorTool.find("3ds") != -1)
				{
					bIs3dStudio = true;
				}
			}
		}
		if (root->getAsset()->getUp_axis() != NULL && root->getAsset()->getUp_axis()->getCharData() == "Z_UP")
			bZup = true;
	}

	g_modelMax = BoundingBox();


	// Do the conversion. The conversion process throws an exception on error, so
	// we'll include a try/catch handler.
	
	sceneNodes = convertModel(*root);
	vector<Node> queue;
	vector< TNT::Array2D<float> > queueMatrix;
	TNT::Array2D<float> actualMatrix(4,4,0.0f);

	for (int i=0; i < 4; i++)
		actualMatrix[i][i] = 1.0f;

	bool isRoot = true;
	vector<SN::SkeletonNode*> queueSkeleton;
	
	for (int i=0; i < sceneNodes.size(); i++){
		queue.push_back(sceneNodes[i]);	

		if (sceneNodes[i].isBone){
			if (isRoot){
				queueSkeleton.push_back(nproot);
				isRoot = false;
			}
			else{
				queueSkeleton.push_back(new SN::SkeletonNode());
			}
		}

		queueMatrix.push_back(actualMatrix);	
	}

	pModel->pObject = vector<structure::t3DObject>();
	int numOfObjects = 0;

	SN::SkeletonNode * actualNode = nproot;

	vector<ObjectSkeletonShaderData> v_ossdata;
	Controller * meshController = NULL;
	pModel->numOfMaterials = 0;

	while (queue.size() > 0){

		Node node = queue[queue.size() - 1];
		queue.pop_back();
		actualMatrix = queueMatrix[queueMatrix.size() - 1];
		queueMatrix.pop_back();

		for (int i=0; i < node.vTranslate.size(); i++){
			actualMatrix = actualMatrix * TranslateFloat3ToTNTMatrix(node.vTranslate[i]);
		}

		for (int i=0; i < node.vScale.size(); i++){
			actualMatrix = actualMatrix * ScaleFloat3ToTNTMatrix(node.vScale[i]);
		}

		for (int i=0; i < node.vRotate.size(); i++){
			actualMatrix = actualMatrix * RotateFloat4ToTNTMatrix(node.vRotate[i]);
		}

		for (int i=0; i < node.vMatrix.size(); i++){
			actualMatrix = actualMatrix * VFloat4x4ToTNTMatrix(node.vMatrix[i]);
		}

		// set bone parameters
		if (node.isBone && importStyle > 0){
			actualNode = queueSkeleton[queueSkeleton.size() - 1];
			queueSkeleton.pop_back();
			actualNode->matrices = BonesMatrices();
			//actualNode->bindPoseMatrices = BonesMatrices();
			actualNode->matrices.currentAffine = SwapMatrix(bZup, bIs3dStudio) * actualMatrix;
			//copyBonesMatrices(&actualNode->bindPoseMatrices, &actualNode->matrices);
			actualNode->point = CVector3(actualNode->matrices.currentAffine[0][3], actualNode->matrices.currentAffine[1][3], actualNode->matrices.currentAffine[2][3]);
			actualNode->sId = node.sId;
		}

		for (int i=0; i < node.controllers.size(); i++){
			meshController = node.controllers[i];

			domGeometry* instanceGeom = daeSafeCast<domGeometry>(meshController->geoElement);

			// Now convert the geometry, add the result to our list of meshes, and assign
			// the mesh a material.
			node.meshes.push_back(&lookup<CMesh, domGeometry>(*instanceGeom));
			if (meshController->materials.size() > 0)
				node.meshes[node.meshes.size() - 1]->mtl = &meshController->materials[0];
		}


		if (node.meshes.size() > 0){
			for (int i=0; i < node.meshes.size(); i++){
				CMesh * mesh = node.meshes[i];

				ObjectSkeletonShaderData ossdata;

				for (int i=0; i < mesh->iVertsCount; i++){
					mesh->pVertices[i] = TntVecToCVec(CVecToTntVec4(mesh->pVertices[i]) * actualMatrix.transpose(SwapMatrix(bZup, bIs3dStudio) * actualMatrix) );
				}

				structure::t3DObject * pObject = new structure::t3DObject();
				pObject->skinning = node.skinning;
				numOfObjects++;
				// convert to 3dobject structure
				pObject->numOfVerts = mesh->iVertsCount;
				pObject->numOfVertices = mesh->iVerticesCount;
				pObject->numOfFaces = mesh->vPolygons.size();
				pObject->numTexVertex = 0;
				pObject->materialID = -1;
				pObject->bHasTexture = false;
				//pObject->strName[255] = "";

				pObject->pVerts = mesh->pVertices;

				/*if (bIs3dStudio){
					for(int i = 0; i < pObject->numOfVerts; i++)
					{
						// Store off the Y value
						float fTempY = pObject->pVerts[i].y;

						// Set the Y value to the Z value
						pObject->pVerts[i].y = pObject->pVerts[i].z;

						// Set the Z value to the Y value,
						// but negative Z because 3D Studio max does the opposite.
						pObject->pVerts[i].z = -fTempY;

					}
				}

				if (bZup){
					for(int i = 0; i < pObject->numOfVerts; i++)
					{
						// Set the Y value to the -Y value, Z to -Z
						pObject->pVerts[i].y = -pObject->pVerts[i].y;
						pObject->pVerts[i].z = -pObject->pVerts[i].z;
					
					}
				}*/

/*				for (int k=0; k < mesh->iVertsCount;k++){
					if ( mesh->pVertices[k].x < g_modelMax.x_min) {
						g_modelMax.x_min = mesh->pVertices[k].x;
						g_modelMax.x_min_v = mesh->pVertices[k];
					}
					if (  mesh->pVertices[k].x > g_modelMax.x_max) {
						g_modelMax.x_max =  mesh->pVertices[k].x;
						g_modelMax.x_max_v =  mesh->pVertices[k];
					}

					if (  mesh->pVertices[k].y < g_modelMax.y_min) {
						g_modelMax.y_min =  mesh->pVertices[k].y;
						g_modelMax.y_min_v =  mesh->pVertices[k];
					}
					if (  mesh->pVertices[k].y > g_modelMax.y_max) {
						g_modelMax.y_max =  mesh->pVertices[k].y;
						g_modelMax.y_max_v =  mesh->pVertices[k];
					}
					if (  mesh->pVertices[k].z < g_modelMax.z_min) {
						g_modelMax.z_min =  mesh->pVertices[k].z;
						g_modelMax.z_min_v =  mesh->pVertices[k];
					}
					if (  mesh->pVertices[k].z > g_modelMax.z_max) {
						g_modelMax.z_max =  mesh->pVertices[k].z;
						g_modelMax.z_max_v =  mesh->pVertices[k];
					}
				}

				if (mesh->vIndices.size() > 0){
					pObject->pIndices = new unsigned int[mesh->vIndices.size()];
					for (int v = 0; v < mesh->vIndices.size(); v++)
						pObject->pIndices[mesh->vIndices.size() - v - 1] = mesh->vIndices[v];
				}
				if (mesh->vNormals.size() > 0){
					pObject->pNormals = new CVector3[mesh->iVertsCount];
					if (bZup){
						for (int v = 0; v < mesh->vIndices.size(); v++)
							pObject->pNormals[mesh->vIndices[v]] = CVector3(mesh->vNormals[v].x, -mesh->vNormals[v].y, -mesh->vNormals[v].z);
					} else if (bIs3dStudio){
							for (int v = 0; v < mesh->vIndices.size(); v++)
								pObject->pNormals[mesh->vIndices[v]] = CVector3(mesh->vNormals[v].x, mesh->vNormals[v].z, -mesh->vNormals[v].y);;
						} else {
								for (int v = 0; v < mesh->vIndices.size(); v++)
									pObject->pNormals[mesh->vIndices[v]] = mesh->vNormals[v];
							}
				}
				if (mesh->vTexVerts.size() > 0){
					pObject->pTexVerts = new CVector2[mesh->iVertsCount];
					for (int v = 0; v < mesh->vTexVerts.size(); v++)
						pObject->pTexVerts[mesh->vIndices[v]] = mesh->vTexVerts[v];
				} else {
					pObject->pTexVerts = new CVector2[mesh->iVertsCount];
				}
					
				pObject->pFaceNormals = new CVector3[mesh->vPolygons.size()]; // not filled yet
				pObject->pTangents = new CVector3[mesh->iVerticesCount];
				pObject->pFaces = new tFace[mesh->vPolygons.size()];
				for (int j=0; j<mesh->vPolygons.size(); j++){
					tFace face;
					face.vertIndex[0] = mesh->vPolygons[j].pindices[0];
					face.vertIndex[1] = mesh->vPolygons[j].pindices[1];
					face.vertIndex[2] = mesh->vPolygons[j].pindices[2];
					face.coordIndex[0] = mesh->vPolygons[j].pindices[0];
					face.coordIndex[1] = mesh->vPolygons[j].pindices[1];
					face.coordIndex[2] = mesh->vPolygons[j].pindices[2];
					pObject->pFaces[j] = face;
				}
				pObject->pVertID = new unsigned int[mesh->iVerticesCount];

				if (importStyle > 1 && meshController != NULL && pObject->skinning){
					// weights and indices
					ossdata.numOfVertices = pObject->numOfVerts;
					ossdata.numOfBones = meshController->bones.size();
					ossdata.indices = new float[ossdata.numOfVertices * config.NUM_OF_CTRL_BONES];
					ossdata.weights = new float[ossdata.numOfVertices * config.NUM_OF_CTRL_BONES];
					memcpy(ossdata.indices, meshController->indices, ossdata.numOfVertices * config.NUM_OF_CTRL_BONES * sizeof(float));
 					memcpy(ossdata.weights, meshController->weights, ossdata.numOfVertices * config.NUM_OF_CTRL_BONES * sizeof(float));
				} else {
					ossdata.numOfVertices = pObject->numOfVerts;
					ossdata.numOfBones = config.NUM_OF_CTRL_BONES;
					ossdata.indices = new float[ossdata.numOfVertices * config.NUM_OF_CTRL_BONES];
					ossdata.weights = new float[ossdata.numOfVertices * config.NUM_OF_CTRL_BONES];	
					for (int i=0; i<ossdata.numOfVertices * config.NUM_OF_CTRL_BONES; i++)
						ossdata.indices[i] = -1;
				}

				if (mesh->mtl != NULL){
					if (!mesh->mtl->initialized)
					for (int k=0; k < node.controllers.size(); k++){
						Controller * ctrl = node.controllers[k];
						for (int l=0; l < ctrl->symbols.size(); l++){
							if (ctrl->symbols[l] == mesh->mtl->strMaterialName){
								mesh->mtl = &ctrl->materials[l];
								k = node.controllers.size();
								l = ctrl->materials.size();
							}
						}
					}

				// material
					pObject->materialID = pModel->pMaterials.size();
					tMaterialInfo info;
					BYTE *b = new BYTE[3];
					if (mesh->mtl->textureID != -1){
						b = mesh->mtl->rgbAmbient.toByte3();
					} else {
						b = mesh->mtl->rgbDiffuse.toByte3();
					}
					info.color[0] = b[0];
					info.color[1] = b[1];
					info.color[2] = b[2];
					const char * m;
					if (mesh->mtl->refToTexture != ""){
						for (int im = 0; im < imageLib.size(); im++)
							m = imageLib[mesh->mtl->refToTexture].c_str();
					} else {
						m = mesh->mtl->strMaterialName.c_str();
					}
					for (int n=0; n < mesh->mtl->strMaterialName.size(); n++)
						info.strName[n] = m[n];
					for (int n=mesh->mtl->strMaterialName.size(); n < 255; n++)
						info.strName[n] = 0;
					info.textureId = mesh->mtl->textureID;
					bool add = true;
					for (int m=0; m < pModel->pMaterials.size(); m++)
						if (strcmp(pModel->pMaterials[m].strName, info.strName) == 0){
							add = false;
							pObject->materialID = m;
						}
					if (add){
						pModel->pMaterials.push_back(info);
						pModel->numOfMaterials++;
					}
				}								

				if (importStyle > 1 && meshController != NULL){
					v_ossdata.push_back(ossdata);			
					pObject->hasMeshController = true;
				} else {
					pObject->hasMeshController = false;
				}
				pModel->pObject.push_back(*pObject);
			}
		}

		for (int i=0; i < node.childNodes.size(); i++){
			queue.push_back(*(node.childNodes[i]));	
			queueMatrix.push_back(actualMatrix);

			if (node.childNodes[i]->isBone && importStyle > 0){
				if (isRoot){
					queueSkeleton.push_back(nproot);
					isRoot = false;
				}
				else{
					actualNode->nodes.push_back(new SN::SkeletonNode());

					queueSkeleton.push_back(actualNode->nodes.back());
					actualNode->nodes.back()->father = actualNode;
				}
			}
		}
	}
	if (numOfObjects == 0)
		return false;

	vector<structure::t3DObject> inversed;

	for (int i=0; i < pModel->pObject.size(); i++)
		inversed.push_back(pModel->pObject[pModel->pObject.size() - i - 1]);

	pModel->pObject = inversed;

	pModel->numOfObjects = numOfObjects;

	if (importStyle > 0 && meshController != NULL){
		// skeleton
		vector<SN::SkeletonNode*> queueSkeleton;
		queueSkeleton.push_back(nproot);
		//generateIdForTree(nproot);
		//copySN::SkeletonNode(nproot, bproot);

		while (queueSkeleton.size() > 0){

			actualNode = queueSkeleton[queueSkeleton.size() - 1];
			queueSkeleton.pop_back();
			actualNode->id = meshController->boneIndices[actualNode->sId] + 1;
			//actualNode->matrices = BonesMatrices();
			//actualNode->bindPoseMatrices = BonesMatrices();
			if (actualNode->sId != ""){
				TNT::Array2D<float> ibmatrix = meshController->bones[meshController->boneIndices[actualNode->sId]].m_InverseBindMatrix;
				actualNode->bindPoseMatrices.currentAffine = SwapMatrix(bZup, bIs3dStudio) * ibmatrix.invert(ibmatrix);
				//copyBonesMatrices(&actualNode->bindPoseMatrices, &actualNode->matrices);
				//actualNode->point = CVector3(actualNode->bindPoseMatrices.currentAffine[0][3], actualNode->bindPoseMatrices.currentAffine[1][3], actualNode->bindPoseMatrices.currentAffine[2][3]);
			}

			for (int i=0; i < actualNode->nodes.size(); i++)
				queueSkeleton.push_back(actualNode->nodes[i]);
		}
	}

	if (importStyle > 1 && meshController != NULL){
		for (int i=0; i < numOfObjects; i++){
			ObjectSkeletonShaderData * psd = new ObjectSkeletonShaderData();
			psd->numOfVertices = pModel->pObject[i].numOfVertices;
			psd->indices = new float[psd->numOfVertices * config.NUM_OF_CTRL_BONES];
			psd->weights = new float[psd->numOfVertices * config.NUM_OF_CTRL_BONES];

			int ossid = 0;
			if (pModel->pObject[i].hasMeshController){

				/*for (int j = 0; j < v_ossdata[ossid].numOfVertices; j++) {
					for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
						logg.log(0, "indexy pre vertex uz v v_ossdata : " ,j);
						logg.log(0, "index", v_ossdata[ossid].indices[j * 4 + k]);
					}

					for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
						logg.log(0, "vahy pre vertex uz v v_ossdata : " , j);
						logg.log(0, "vaha ", v_ossdata[ossid].weights[j * 4 + k]);
					}
				}*/

	/*			int ind = 0;
				for(int j = 0; j < pModel->pObject[i].numOfFaces; j++){
					for(int whichVertex = 0; whichVertex < 3; whichVertex++){
						int index = pModel->pObject[i].pFaces[j].vertIndex[whichVertex];
						for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
							psd->indices[ind * config.NUM_OF_CTRL_BONES + k] = v_ossdata[v_ossdata.size() - ossid - 1].indices[index * config.NUM_OF_CTRL_BONES + k];
							psd->weights[ind * config.NUM_OF_CTRL_BONES + k] = v_ossdata[v_ossdata.size() - ossid - 1].weights[index * config.NUM_OF_CTRL_BONES + k];
						}
						ind++;
					}
				}

				skeletonData.push_back(psd);


				for (int j = 0; j < skeletonData[i]->numOfVertices; j++) {
					for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
						logg.log(0, "indexy pre vertex uz v datovej strukture : " ,j);
						logg.log(0, "index", skeletonData[i]->indices[j * 4 + k]);
					}

					for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
						logg.log(0, "vahy pre vertex uz v datovej strukture : " , j);
						logg.log(0, "vaha ", skeletonData[i]->weights[j * 4 + k]);
					}
				}


				ossid++;
			} else {

				for (int j=0; j<psd->numOfVertices * config.NUM_OF_CTRL_BONES; j++){
					psd->indices[j] = -1.0f;
					psd->weights[j] = 0.0f;
				}
				skeletonData.push_back(psd);
			}

		}
	}

	pModel->modelbb = g_modelMax;
	// Don't forget to destroy the objects we created during the conversion process
	//freeConversionObjects<Node, domNode>(dae);
	//freeConversionObjects<Mesh, domGeometry>(dae);
	//freeConversionObjects<Material, domMaterial>(dae);
	root->release();
	
}

// EXPORT


// Demonstrates how to use the DOM to create a simple, textured Collada model
// and save it to disk.


bool CColladaLoader::ExportToFile(structure::t3DModel * pModel, ObjectSkeletonShaderData * objectSkeletonDataArray, const string file, SN::SkeletonNode * skeletonroot, bool exportSkeleton) {
	DAE * dae = new DAE();
	domCOLLADA* root = dae->add(file);
	if (root == NULL)
		return false;

	addAsset(root);
	if (pModel->numOfMaterials > 0){
		addEffects(pModel, root);
		addMaterials(pModel, root);
		addImages(pModel, root);
	}

	addGeometry(pModel->pMaterials, pModel, root);

	if (exportSkeleton){
		addControllers(objectSkeletonDataArray, skeletonroot, pModel, root);
	}

	addVisualScene(pModel, exportSkeleton, skeletonroot, root);
	
	dae->writeAll();

	// As a very simple check for possible errors, make sure the document loads
	// back in successfully.
	dae->clear();
	dae->close(file);
	dae->cleanup();
	//delete dae;
return true;	
}

bool CColladaLoader::addAsset(daeElement* root) {
	SafeAdd(root, "asset", asset);

	time_t rawtime;

	time ( &rawtime );
	char t[255];
	struct tm * timeinfo;
	timeinfo = localtime ( &rawtime );

	sprintf ( t, "%02i-%02i-%02iT%02i:%02i:%02iZ", 
		timeinfo->tm_year + 1900,
		timeinfo->tm_mon + 1,
		timeinfo->tm_mday,
		timeinfo->tm_hour,
		timeinfo->tm_min,
		timeinfo->tm_sec
	);

	asset->add("created")->setCharData(t);
	asset->add("modified")->setCharData(t);
	return true;
}

template<typename T>
daeTArray<T> CColladaLoader::rawArrayToDaeArray(T rawArray[], size_t count) {
	daeTArray<T> result;
	for (size_t i = 0; i < count; i++)
		result.append(rawArray[i]);
	return result;
}


string CColladaLoader::makeUriRef(const string& id) {
	return string("#") + id;
}

bool CColladaLoader::addSource(daeElement* mesh,
                     const string& srcID,
					 const string& srcName,
                     const string& paramNames,
                     domFloat values[],
                     int valueCount) {
	SafeAdd(mesh, "source", src);
	src->setAttribute("id", srcID.c_str());
	src->setAttribute("name", srcName.c_str());

	domFloat_array* fa = daeSafeCast<domFloat_array>(src->add("float_array"));
	fa->setId((src->getAttribute("id") + "-array").c_str());
	fa->setCount(valueCount);
	fa->getValue() = rawArrayToDaeArray(values, valueCount);

	domAccessor* acc = daeSafeCast<domAccessor>(src->add("technique_common accessor"));
	acc->setSource(makeUriRef(fa->getId()).c_str());

	list<string> params;
	for (int i=0; i<(paramNames.length()+1) / 2; i++){
		stringstream s; 
		s << paramNames.c_str()[i*2];
		params.push_front(s.str());
	}
	acc->setStride(params.size());
	acc->setCount(valueCount/params.size());
	for (tokenIter iter = params.begin(); iter != params.end(); iter++) {
		SafeAdd(acc, "param", p);
		p->setAttribute("name", iter->c_str());
		p->setAttribute("type", "float");
	}
	return true;	
}

bool CColladaLoader::addNameSource(daeElement* mesh,
                     const string& srcID,
                     daeStringRef values[],
                     int valueCount) {
	SafeAdd(mesh, "source", src);
	src->setAttribute("id", srcID.c_str());

	domName_array* na = daeSafeCast<domName_array>(src->add("Name_array"));
	na->setId((src->getAttribute("id") + "-array").c_str());
	na->setCount(valueCount);
	na->setValue(rawArrayToDaeArray(values, valueCount));

	domAccessor* acc = daeSafeCast<domAccessor>(src->add("technique_common accessor"));
	acc->setSource(makeUriRef(na->getId()).c_str());

	acc->setStride(1);
	acc->setCount(valueCount);

	SafeAdd(acc, "param", p);
	p->setAttribute("name", "JOINT");
	p->setAttribute("type", "Name");
	return true;	
}

bool CColladaLoader::addTransformSource(daeElement* mesh,
                     const string& srcID,
                     domFloat values[],
                     int valueCount) {
	SafeAdd(mesh, "source", src);
	src->setAttribute("id", srcID.c_str());

	domFloat_array* fa = daeSafeCast<domFloat_array>(src->add("float_array"));
	fa->setId((src->getAttribute("id") + "-array").c_str());
	fa->setCount(valueCount);
	fa->getValue() = rawArrayToDaeArray(values, valueCount);

	domAccessor* acc = daeSafeCast<domAccessor>(src->add("technique_common accessor"));
	acc->setSource(makeUriRef(fa->getId()).c_str());

	acc->setStride(16);
	acc->setCount(valueCount / 16);

	SafeAdd(acc, "param", p);
	p->setAttribute("name", "TRANSFORM");
	p->setAttribute("type", "float4x4");
	return true;	
}

bool CColladaLoader::addWeightSource(daeElement* mesh,
                     const string& srcID,
                     domFloat values[],
                     int valueCount) {
	SafeAdd(mesh, "source", src);
	src->setAttribute("id", srcID.c_str());

	domFloat_array* fa = daeSafeCast<domFloat_array>(src->add("float_array"));
	fa->setId((src->getAttribute("id") + "-array").c_str());
	fa->setCount(valueCount);
	fa->getValue() = rawArrayToDaeArray(values, valueCount);

	domAccessor* acc = daeSafeCast<domAccessor>(src->add("technique_common accessor"));
	acc->setSource(makeUriRef(fa->getId()).c_str());

	acc->setStride(1);
	acc->setCount(valueCount);

	SafeAdd(acc, "param", p);
	p->setAttribute("name", "WEIGHT");
	p->setAttribute("type", "float");
	return true;	
}

bool CColladaLoader::addInput(daeElement* triangles,
                    const string& semantic,
                    const string& srcID,
                    int offset) {
    domInput_local_offset* input = daeSafeCast<domInput_local_offset>(triangles->add("input"));
	input->setSemantic(semantic.c_str());
	if (offset > -1)
		input->setOffset(offset);
    domUrifragment source(*triangles->getDAE(), srcID);
    input->setSource(source);
	if (semantic == "TEXCOORD")
		input->setSet(0);

	return true;
}

bool CColladaLoader::addControllers(ObjectSkeletonShaderData * objectSkeletonDataArray, SN::SkeletonNode * skeletonroot,  structure::t3DModel * pModel, daeElement* root){
	SafeAdd(root, "library_controllers", controlLib);

	for (int i=0; i < pModel->numOfObjects; i++){

		structure::t3DObject * pObject = &pModel->pObject[i];
		if (!pObject->skinning)
			continue;
		ObjectSkeletonShaderData * ossdata = &objectSkeletonDataArray[i];

		/*for (int j = 0; j < ossdata->numOfVertices; j++) {
			for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
				logg.log(0, "indexy pre vertex potom : " ,j);
				logg.log(0, "index", ossdata->indices[j * config.NUM_OF_CTRL_BONES + k]);
			}

			for (int k = 0; k < config.NUM_OF_CTRL_BONES; k++) {
				logg.log(0, "vahy pre vertex potom : " , j);
				logg.log(0, "vaha ",ossdata->weights[j * config.NUM_OF_CTRL_BONES + k]);
			}
		}*/

	/*	SafeAdd(controlLib, "controller", control);
		string geomID = "geometry_"+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)));
		control->setAttribute("id", (geomID+"-skin").c_str());

		domSkin* skin = daeSafeCast<domSkin>(control->add("skin"));

		skin->setAttribute("source", makeUriRef(geomID).c_str());

		SafeAdd(skin, "bind_shape_matrix", bsmatrix);
		bsmatrix->setCharData("1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1");
	
		vector<SN::SkeletonNode*> queue;
		queue.push_back(skeletonroot);

		int num = 0;

		while (queue.size() > 0){
			SN::SkeletonNode* pNode = queue[queue.size() - 1];
			queue.pop_back();

			num++;

			for (int i=0; i < pNode->nodes.size(); i++){
				SN::SkeletonNode* pSon = pNode->nodes[i];
				queue.push_back(pSon);
			}
		}

		daeStringRef * joints = new daeStringRef[num];

		for (int j=0; j < num; j++)
			joints[j] = daeStringRef(("joint"+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(j)))).c_str());

		domFloat * matrices = new domFloat[num * 16];

		queue.push_back(skeletonroot);

		int idx = 0;

		while (queue.size() > 0){
			SN::SkeletonNode* pNode = queue[queue.size() - 1];
			queue.pop_back();

			TNT::Array2D<float> invbindpose = pNode->bindPoseMatrices.currentAffine.invert(pNode->bindPoseMatrices.currentAffine);

			for (int m=0; m < 4; m++)
				for (int n=0; n < 4; n++){
					float val = invbindpose[m][n];
					matrices[idx * 16 + m * 4 + n] = val;
				}

			idx++;

			for (int i=0; i < pNode->nodes.size(); i++){
				SN::SkeletonNode* pSon = pNode->nodes[i];
				queue.push_back(pSon);
			}
		}

		domFloat * weights = new domFloat[ossdata->numOfVertices * config.NUM_OF_CTRL_BONES];
		for (int j=0; j < ossdata->numOfVertices * config.NUM_OF_CTRL_BONES; j++)
			weights[j] = ossdata->weights[j];

		// joints
		addNameSource(skin, geomID + "-skin-joints", joints, num);
		// bind poses
		addTransformSource(skin, geomID + "-skin-bind_poses", matrices, num * 16);
		// weights
		addWeightSource(skin, geomID + "-skin-weights", weights, ossdata->numOfVertices * config.NUM_OF_CTRL_BONES);

		domSkin::domJoints * sjoints = daeSafeCast<domSkin::domJoints>(skin->add("joints"));
	
		SafeAdd(sjoints, "input", jinput);
		jinput->setAttribute("semantic", "JOINT");
		jinput->setAttribute("source", makeUriRef(geomID + "-skin-joints").c_str());

		SafeAdd(sjoints, "input", binput);
		binput->setAttribute("semantic", "INV_BIND_MATRIX");
		binput->setAttribute("source", makeUriRef(geomID + "-skin-bind_poses").c_str());
	
			
		domSkin::domVertex_weights * vweights = daeSafeCast<domSkin::domVertex_weights>(skin->add("vertex_weights"));
		vweights->setCount(pModel->pObject[i].numOfVerts * config.NUM_OF_CTRL_BONES);

		addInput(vweights, "JOINT",  makeUriRef(geomID + "-skin-joints"), 0);
		addInput(vweights, "WEIGHT", makeUriRef(geomID + "-skin-weights"),  1);

		SafeAdd(vweights, "v", v);
		SafeAdd(vweights, "vcount", vcount);

		for (int j=0; j<ossdata->numOfVertices; j++){
			vweights->getVcount()->getValue().append(config.NUM_OF_CTRL_BONES);
			for (int k=0; k<config.NUM_OF_CTRL_BONES; k++){
				vweights->getV()->getValue().append(ossdata->indices[j * 4 + k]);
				vweights->getV()->getValue().append(j * 4 + k);
			}
		}
		delete[] weights;
		delete[] matrices;
		delete[] joints;
	}

	return true;	
}

bool CColladaLoader::addGeometry(vector<tMaterialInfo> infos, structure::t3DModel * pModel, daeElement* root) {
	SafeAdd(root, "library_geometries", geomLib);

	for (int i=0; i < pModel->numOfObjects; i++){

		structure::t3DObject * pObject = &pModel->pObject[i];

		SafeAdd(geomLib, "geometry", geom);
		string geomID = "geometry_"+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)));
		geom->setAttribute("id", geomID.c_str());
		SafeAdd(geom, "mesh", mesh);

		domFloat *posArray = new domFloat[pObject->numOfVerts * 3];
		domFloat *normalArray = new domFloat[pObject->numOfVerts * 3];
		domFloat *uvArray = new domFloat[pObject->numOfVerts * 3];
		domUint	*indices = new domUint[pObject->numOfVertices * 3]; // positons + normals + uvcoords

		int vertIndex = 0;
		for(int j = 0; j < pObject->numOfFaces; j++)
		{
			for(int whichVertex = 0; whichVertex < 3; whichVertex++)
			{
				int index = pObject->pFaces[j].vertIndex[whichVertex];
				indices[vertIndex * 3] = index;
				indices[vertIndex * 3 + 1] = index;
				indices[vertIndex * 3 + 2] = index;
				vertIndex ++;
			}
		}

		for (int j = 0; j < pObject->numOfVerts; j++) {
			posArray[j * 3] = pObject->pVerts[ j ].x;
			posArray[j * 3 + 1] = pObject->pVerts[ j ].y;
			posArray[j * 3 + 2] = pObject->pVerts[ j ].z;
			normalArray[j * 3] = pObject->pNormals[ j ].x;
			normalArray[j * 3 + 1] = pObject->pNormals[ j ].y;
			normalArray[j * 3 + 2] = pObject->pNormals[ j ].z;
			uvArray[j * 2] = pObject->pTexVerts[ j ].x;
			uvArray[j * 2 + 1] = pObject->pTexVerts[ j ].y;
		}
				
		addSource(mesh, geomID + "-positions", "position", "X Y Z", posArray, pObject->numOfVerts * 3);
		addSource(mesh, geomID + "-normals", "normal", "X Y Z", normalArray, pObject->numOfVerts * 3);
		addSource(mesh, geomID + "-uv", "map", "S T", uvArray, pObject->numOfVerts * 2);

		// Add the <vertices> element
		SafeAdd(mesh, "vertices", vertices);
		vertices->setAttribute("id", (geomID + "-vertices").c_str());
		SafeAdd(vertices, "input", verticesInput);
		verticesInput->setAttribute("semantic", "POSITION");
		verticesInput->setAttribute("source", makeUriRef(geomID + "-positions").c_str());


		domTriangles* triangles = daeSafeCast<domTriangles>(mesh->add("triangles"));
		triangles->setCount(pObject->numOfFaces);
		if (pObject->materialID > -1){
			triangles->setMaterial(infos[pObject->materialID].strName);
		}

		addInput(triangles, "VERTEX",   makeUriRef(geomID + "-vertices"), 0);
		addInput(triangles, "NORMAL",   makeUriRef(geomID + "-normals"),  1);
		addInput(triangles, "TEXCOORD", makeUriRef(geomID + "-uv"),       2);

		domP* p = daeSafeCast<domP>(triangles->add("p"));
		p->getValue() = rawArrayToDaeArray(indices, pObject->numOfVertices * 3);

		delete[] posArray;
		delete[] normalArray;
		delete[] uvArray;
		delete[] indices;

	}

	return true;
}

bool CColladaLoader::addImages(structure::t3DModel * pModel, daeElement* root) {
	bool addroot = false;
	for (int i=0; i< pModel->numOfMaterials; i++)
		if (pModel->pMaterials[i].textureId != -1)
			addroot = true;
	if (addroot){
		SafeAdd(root, "library_images", imageLib);
		for (int i=0; i< pModel->numOfMaterials; i++){
			if (pModel->pMaterials[i].textureId != -1){
				SafeAdd(imageLib, "image", image);
				image->setAttribute("id", (string("img-")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)))).c_str());
				SafeAdd(image, "init_from", initfrom);
				initfrom->setCharData( string("../")+string(pModel->pMaterials[i].strFile) );
			}
		}
	}
	
	return true;
}

bool CColladaLoader::addEffects(structure::t3DModel * pModel, daeElement* root) {
	SafeAdd(root, "library_effects", effectLib);
	for (int i=0; i< pModel->numOfMaterials; i++){
		tMaterialInfo info = pModel->pMaterials[i];
		SafeAdd(effectLib, "effect", effect);
		effect->setAttribute("id", (string("effect_")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)))).c_str());
		SafeAdd(effect, "profile_COMMON", profile);

		if (info.textureId != -1){

			// Add a <sampler2D>
			SafeAdd(profile, "newparam", newparam);
			newparam->setAttribute("sid", "sampler");
			SafeAdd(newparam, "sampler2D", sampler);
			daeSafeCast<domInstance_image>(sampler->add("instance_image"))->setUrl(makeUriRef(string("img-")+string(info.strName)).c_str());
			sampler->add("minfilter")->setCharData("LINEAR");
			sampler->add("magfilter")->setCharData("LINEAR");
		}

		SafeAdd(profile, "technique", technique);
		technique->setAttribute("sid", "common");
		SafeAdd(technique, "phong", phong);
		domFx_common_color_or_texture* cott = daeSafeCast<domFx_common_color_or_texture>(phong->add("diffuse"));

		if (info.textureId != -1){
			domFx_common_color_or_texture::domTexture *tex = daeSafeCast< domFx_common_color_or_texture::domTexture>( cott->add( "texture" ) );
			tex->setAttribute("texture", "sampler");
			tex->setAttribute("texcoord", "uv0");

			domFx_common_color_or_texture* cott2 = daeSafeCast<domFx_common_color_or_texture>(phong->add("ambient"));
			domFx_common_color_or_texture::domColor *col = daeSafeCast< domFx_common_color_or_texture::domColor>( cott2->add( "color" ) );

			col->getValue().append(info.color[0] / 255.0);
			col->getValue().append(info.color[1] / 255.0);
			col->getValue().append(info.color[2] / 255.0);
			col->getValue().append(0.0f);
		} else {

			domFx_common_color_or_texture::domColor *col = daeSafeCast< domFx_common_color_or_texture::domColor>( cott->add( "color" ) );

			col->getValue().append(info.color[0] / 255.0);
			col->getValue().append(info.color[1] / 255.0);
			col->getValue().append(info.color[2] / 255.0);
			col->getValue().append(0.0f);

		}
	}

	return true;
}

bool CColladaLoader::addMaterials(structure::t3DModel * pModel, daeElement* root) {
	SafeAdd(root, "library_materials", materialLib);
	for (int i=0; i< pModel->numOfMaterials; i++){
		SafeAdd(materialLib, "material", material);
		material->setAttribute("id", (string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)))+string("-material")).c_str());
		material->setAttribute("name", (char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)));
		material->add("instance_effect")->setAttribute("url", makeUriRef(string("effect_")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)))).c_str());
	}

	return true;
}

bool CColladaLoader::addVisualScene(structure::t3DModel * pModel, bool exportskeleton, SN::SkeletonNode * skeletonroot, daeElement* root) {
	SafeAdd(root, "library_visual_scenes", visualSceneLib);
	SafeAdd(visualSceneLib, "visual_scene", visualScene);
	visualScene->setAttribute("id", "scene");

	for (int i=0; i<pModel->numOfObjects; i++){
		// Add a <node> with a simple transformation
		SafeAdd(visualScene, "node", node);
		node->setAttribute("id", ((string("node_")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i))))).c_str());

		daeElement * pDaeObject = NULL;

		if (exportskeleton && pModel->pObject[i].skinning){
			// Instantiate the <controller>
			SafeAdd(node, "instance_controller", instanceCont);
			instanceCont->setAttribute("url", makeUriRef(string("geometry_")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i)))+string("-skin")).c_str());
			SafeAdd(instanceCont, "skeleton", skeleton);
			skeleton->setCharData(makeUriRef("skeleton_root").c_str());
			pDaeObject = instanceCont;
		} else {
			// Instantiate the <geometry>
			SafeAdd(node, "instance_geometry", instanceGeom);
			instanceGeom->setAttribute("url", makeUriRef((string("geometry_")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(i))))).c_str());
			pDaeObject = instanceGeom;
		}

		if (pModel->pObject[i].materialID > -1){
			// Bind material parameters
			SafeAdd(pDaeObject, "bind_material technique_common instance_material", instanceMaterial);
			instanceMaterial->setAttribute("symbol", (char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(pModel->pObject[i].materialID)));
			instanceMaterial->setAttribute("target", makeUriRef(string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(pModel->pObject[i].materialID)))+string("-material")).c_str());

			if (pModel->pObject[i].bHasTexture){
				SafeAdd(instanceMaterial, "bind_vertex_input", bindVertexInput);
				bindVertexInput->setAttribute("semantic", "uv0");
				bindVertexInput->setAttribute("input_semantic", "TEXCOORD");
				bindVertexInput->setAttribute("input_set", "0");
			}
		}
	}

	if (exportskeleton){
		SafeAdd(visualScene, "node", skroot);
		skroot->setAttribute("id", "skeleton_root");
		skroot->setAttribute("name", "joint0");
		skroot->setAttribute("sid", "joint0");
		skroot->setAttribute("type", "JOINT");


		domTranslate* ftranslate = daeSafeCast<domTranslate>(skroot->add("translate"));
		domFloat * ftranArray = new domFloat[3];
		CVector3 fctrans = skeletonroot->bindPoseMatrices.getTranslationFromAffine();
		ftranArray[0] = fctrans.x;
		ftranArray[1] = fctrans.y;
		ftranArray[2] = fctrans.z;
		ftranslate->getValue() = rawArrayToDaeArray(ftranArray, 3);
		delete[] ftranArray;

		vector<SN::SkeletonNode*> queue;
		vector<daeElement*> daequeue;


		for (int j=0; j < skeletonroot->nodes.size(); j++){
			queue.push_back(skeletonroot->nodes[j]);
			daequeue.push_back(skroot);
		}

		int num = 1;

		while (queue.size() > 0){
			SN::SkeletonNode* pNode = queue[queue.size() - 1];
			queue.pop_back();

			daeElement * pFather = daequeue[daequeue.size() - 1];
			daequeue.pop_back();

			num++;

			SafeAdd(pFather, "node", node);
			node->setAttribute("name", ((string("joint")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(num - 1))))).c_str());
			node->setAttribute("sid", ((string("joint")+string((char*)(void*)Marshal::StringToHGlobalAnsi(System::Convert::ToString(num - 1))))).c_str());
			node->setAttribute("type", "JOINT");

			domTranslate* translate = daeSafeCast<domTranslate>(node->add("translate"));
			domFloat * tranArray = new domFloat[3];
			CVector3 ctrans = pNode->bindPoseMatrices.getTranslationFromAffine() - pNode->father->bindPoseMatrices.getTranslationFromAffine();
			tranArray[0] = ctrans.x;
			tranArray[1] = ctrans.y;
			tranArray[2] = ctrans.z;
			translate->getValue() = rawArrayToDaeArray(tranArray, 3);
			delete[] tranArray;


			for (int i=0; i < pNode->nodes.size(); i++){
				SN::SkeletonNode* pSon = pNode->nodes[i];
				queue.push_back(pSon);
				daequeue.push_back(node);
			}
		}

	}

	// Add a <scene>
	root->add("scene instance_visual_scene")->setAttribute("url", makeUriRef("scene").c_str());

	return true;
}*/