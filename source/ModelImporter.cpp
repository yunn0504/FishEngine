#include "ModelImporter.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "Debug.hpp"
#include "Common.hpp"
#include "MeshRenderer.hpp"
#include "SkinnedMeshRenderer.hpp"

#define REMOVE_FBX_PIVOT
#define DEBUG_ANIMATION

constexpr int MAX_BONE_SIZE = 100;

static const std::set<std::string> DummyNodeNames = { "Translation", "PreRotation", "Rotation", "PostRotation", "Scaling" };

namespace FishEngine {

    std::map<BuiltinModelType, PModel> Model::s_builtinModels;

    void Model::
    AddMesh(
        PMesh& mesh)
    {
        m_meshes.push_back(mesh);
    }
    
    void Model::
    Init() {
#if FISHENGINE_PLATFORM_WINDOWS
        const std::string root_dir = "../../assets/models/";
#else
        const std::string root_dir = "/Users/yushroom/program/graphics/FishEngine/assets/models/";
#endif
        ModelImporter importer;
        s_builtinModels[BuiltinModelType::Cube]     = importer.LoadFromFile(root_dir+"cube.obj");
        s_builtinModels[BuiltinModelType::Sphere]   = importer.LoadFromFile(root_dir+"sphere.obj");
        s_builtinModels[BuiltinModelType::Plane]    = importer.LoadFromFile(root_dir+"plane.obj");
        s_builtinModels[BuiltinModelType::Quad]     = importer.LoadFromFile(root_dir+"quad.obj");
        s_builtinModels[BuiltinModelType::Cone]     = importer.LoadFromFile(root_dir+"cone.obj");
    }
    

    PModel Model::
    builtinModel(
        const BuiltinModelType type) {
        return s_builtinModels[type];
    }


    Matrix4x4
    ConvertMatrix(
        const aiMatrix4x4& m) {
        Matrix4x4 result;
        memcpy(result.m, &m.a1, 16*sizeof(float));
        //result *= Matrix4x4(1, 0, 0, 0,   0, 1, 0, 0,   0, 0, -1, 0,   0, 0, 0, 1);
        return result;
    }


    PModelNode ModelImporter::
    buildModelTree(
        const aiNode*   assimp_node)
    {
        std::string name = assimp_node->mName.C_Str();
        Matrix4x4 transform = ConvertMatrix(assimp_node->mTransformation);
        transform.m[0][3] *= m_fileScale;
        transform.m[1][3] *= m_fileScale;
        transform.m[2][3] *= m_fileScale;

#ifdef REMOVE_FBX_PIVOT
        // remove dummyNode
        auto pos = name.find("_$AssimpFbx$");
        if (pos != std::string::npos)
        {
            auto trueName = name.substr(0, pos);
            auto typeName = name.substr(pos + 13);
            assert(DummyNodeNames.find(typeName) != DummyNodeNames.end());
            auto it = m_nodeTransformations.find(trueName);
            if (it != m_nodeTransformations.end()) {
                it->second[typeName] = transform;
            } else {
                m_nodeTransformations[trueName] = { { typeName, transform } };
            }

            assert(assimp_node->mNumChildren == 1);
            auto child = assimp_node->mChildren[0];
            assert(boost::starts_with(child->mName.C_Str(), trueName));
            auto c = buildModelTree(child);
            c->transform = transform * c->transform;
            return c;
        }
#endif

        auto node = std::make_shared<ModelNode>();
        node->name = name;
        //Debug::LogWarning("Node: %s", node->name.c_str());
        node->transform = transform;
        node->isBone = false;
        int index = m_model->m_avatar->m_boneToIndex.size();
        m_model->m_avatar->m_boneToIndex[node->name] = index;
        m_model->m_avatar->m_indexToBone[index] = node->name;
        node->index = index;
        
        for (uint32_t i = 0; i < assimp_node->mNumMeshes; ++i) {
            node->meshesIndices.push_back(assimp_node->mMeshes[i]);
        }

        for (uint32_t i = 0; i < assimp_node->mNumChildren; ++i) {
            auto child = buildModelTree(assimp_node->mChildren[i]);
            node->children.push_back(child);
            child->parent = node.get();
        }
        return node;
    }


    PMesh ModelImporter::
    ParseMesh(
        const aiMesh*   assimp_mesh,
        bool            load_uv,
        bool            load_tangent)
    {
        auto mesh = std::make_shared<Mesh>();
        mesh->m_name = assimp_mesh->mName.C_Str();
        bool has_uv = assimp_mesh->HasTextureCoords(0);

        auto n_vertices = assimp_mesh->mNumVertices;
        auto n_triangles = assimp_mesh->mNumVertices;
        auto n_bones = assimp_mesh->mNumBones;
        mesh->m_positionBuffer.reserve(n_vertices * 3);
        mesh->m_normalBuffer.reserve(n_vertices * 3);
        mesh->m_uvBuffer.reserve(n_vertices * 2);
        mesh->m_indexBuffer.reserve(n_triangles * 3);
        mesh->m_tangentBuffer.reserve(n_vertices * 3);

        Vector3 vmin(Mathf::Infinity, Mathf::Infinity, Mathf::Infinity);
        Vector3 vmax(Mathf::NegativeInfinity, Mathf::NegativeInfinity, Mathf::NegativeInfinity);

        // Vertex
        for (unsigned int j = 0; j < assimp_mesh->mNumVertices; ++j) {
            auto& v = assimp_mesh->mVertices[j];
            float vx = v.x * m_fileScale;
            float vy = v.y * m_fileScale;
            float vz = v.z * m_fileScale;
            if (vmin.x > vx) vmin.x = vx;
            if (vmin.y > vy) vmin.y = vy;
            if (vmin.z > vz) vmin.z = vz;
            if (vmax.x < vx) vmax.x = vx;
            if (vmax.y < vy) vmax.y = vy;
            if (vmax.z < vz) vmax.z = vz;
            mesh->m_positionBuffer.push_back(vx);
            mesh->m_positionBuffer.push_back(vy);
            mesh->m_positionBuffer.push_back(vz);

            auto& n = assimp_mesh->mNormals[j];
            mesh->m_normalBuffer.push_back(n.x);
            mesh->m_normalBuffer.push_back(n.y);
            mesh->m_normalBuffer.push_back(n.z);

            if (has_uv) {
                auto& uv = assimp_mesh->mTextureCoords[0][j];
                mesh->m_uvBuffer.push_back(uv.x);
                mesh->m_uvBuffer.push_back(uv.y);
            }

            if (load_tangent) {
                auto& t = assimp_mesh->mTangents[j];
                mesh->m_tangentBuffer.push_back(t.x);
                mesh->m_tangentBuffer.push_back(t.y);
                mesh->m_tangentBuffer.push_back(t.z);
            }
        }

        mesh->m_bounds.SetMinMax(vmin, vmax);

        // face index
        for (unsigned int j = 0; j < assimp_mesh->mNumFaces; j++)
        {
            auto& face = assimp_mesh->mFaces[j];
            assert(face.mNumIndices == 3);
            for (int fi = 0; fi < 3; ++fi)
            {
                mesh->m_indexBuffer.push_back(face.mIndices[fi]);
            }
        }

        mesh->m_skinned = assimp_mesh->mNumBones > 0;

        if (mesh->m_skinned)
        {
            assert(assimp_mesh->mNumBones <= MAX_BONE_SIZE);
            //std::vector<BoneWeight> boneWeights;
            //mesh->m_bones.resize(n_bones);
            mesh->m_boneWeights.resize(n_vertices);
            mesh->m_boneIndexBuffer.resize(n_vertices);
            mesh->m_boneWeightBuffer.resize(n_vertices);
            mesh->bindposes().resize(assimp_mesh->mNumBones);
            Debug::Log("Bone count: %d", assimp_mesh->mNumBones);
            for (uint32_t boneIndex = 0; boneIndex < assimp_mesh->mNumBones; ++boneIndex)
            {
                auto& bone = assimp_mesh->mBones[boneIndex];
                std::string boneName(bone->mName.C_Str());
                //Debug::Log("    bone name: %s", boneName.c_str());
                mesh->m_boneNameToIndex[boneName] = boneIndex;

                auto offsetMat = ConvertMatrix(bone->mOffsetMatrix);
                offsetMat.m[0][3] *= m_fileScale;
                offsetMat.m[1][3] *= m_fileScale;
                offsetMat.m[2][3] *= m_fileScale;
                mesh->bindposes()[boneIndex] = offsetMat;
                for (uint32_t k = 0; k < bone->mNumWeights; ++k)
                {
                    uint32_t vextexID = bone->mWeights[k].mVertexId;
                    float weight = bone->mWeights[k].mWeight;
                    mesh->m_boneWeights[vextexID].AddBoneData(boneIndex, weight);
                }
            }
            
            for (uint32_t i = 0; i < n_vertices; ++i)
            {
                auto& b = mesh->m_boneWeights[i];
                mesh->m_boneIndexBuffer[i] = Int4{b.boneIndex[0], b.boneIndex[1], b.boneIndex[2], b.boneIndex[3]};
                mesh->m_boneWeightBuffer[i] = Vector4{b.weight[0], b.weight[1], b.weight[2], b.weight[3]};
            }
        }

        mesh->GenerateBuffer(m_vertexUsages);
        mesh->BindBuffer(m_vertexUsages);
        return mesh;
    }


    Vector3
    ConvertVector3(
        const aiVector3D& avec3) {
        return Vector3(avec3.x, avec3.y, avec3.z);
    };


    Quaternion
    ConvertQuaternion(
        const aiQuaternion& aquat) {
        return Quaternion(aquat.x, aquat.y, aquat.z, aquat.w);
    };


    PAnimation
    ParseAnimation(
        const aiAnimation* assimp_animation,
        const float fileScale)
    {
        auto animation = std::make_shared<Animation>();
        auto& a = assimp_animation;
        animation->name = a->mName.C_Str();
#ifdef DEBUG_ANIMATION
        // remove dummyNode
        auto pos = animation->name.find("_$AssimpFbx$");
        if (pos != std::string::npos)
        {
            //auto trueName = animation->name.substr(0, pos);
            auto typeName = animation->name.substr(pos + 13);
            assert(typeName == "Translation" || typeName == "Rotation" || typeName == "Scaling");
        }
#endif
        animation->duration = (float)a->mDuration;
        animation->ticksPerSecond = (float)a->mTicksPerSecond;
        if (animation->ticksPerSecond <= 0.f) {
            animation->ticksPerSecond = 25.f;
        }

        for (uint32_t i = 0; i < a->mNumChannels; ++i) {
            auto an = a->mChannels[i];
            std::string name = an->mNodeName.C_Str();

            auto it = animation->channels.find(name);
            if (it == animation->channels.end()) {
                animation->channels[name] = AnimationNode();
            }
            auto& n = animation->channels[name];

            n.name = name;
            for (uint32_t j = 0; j < an->mNumPositionKeys; ++j) {
                n.positionKeys.emplace_back(Vector3Key{ (float)an->mPositionKeys[j].mTime, ConvertVector3(an->mPositionKeys[j].mValue)*fileScale });
            }
            for (uint32_t j = 0; j < an->mNumRotationKeys; ++j) {
                n.rotationKeys.emplace_back(QuaternionKey{ (float)an->mRotationKeys[j].mTime, ConvertQuaternion(an->mRotationKeys[j].mValue) });
            }
            for (uint32_t j = 0; j < an->mNumScalingKeys; ++j) {
                n.scalingKeys.emplace_back(Vector3Key{ (float)an->mScalingKeys[j].mTime, ConvertVector3(an->mScalingKeys[j].mValue) });
            }
        }
        Debug::Log("animation name: %s", a->mName.C_Str());
        return animation;
    }


    PModel ModelImporter::
    LoadFromFile(
        const std::string&  path)
    {
        Assimp::Importer importer;
        unsigned int load_option = aiProcess_Triangulate;
        load_option |= aiProcess_LimitBoneWeights;
        load_option |= aiProcess_JoinIdenticalVertices;
        load_option |= aiProcess_ValidateDataStructure;

        load_option |= aiProcess_ImproveCacheLocality;
        load_option |= aiProcess_RemoveRedundantMaterials;
        load_option |= aiProcess_FindDegenerates;
        load_option |= aiProcess_FindInvalidData;
        load_option |= aiProcess_FindInstances;
        load_option |= aiProcess_OptimizeMeshes;
        load_option |= aiProcess_ConvertToLeftHanded;
        //load_option |= aiProcess_GenUVCoords;
        //load_option |= aiProcess_TransformUVCoords;
        //load_option |= aiProcess_SplitByBoneCount;
        //load_option |= aiProcess_SortByPType;
        //load_option |= aiProcess_SplitLargeMeshes;
        //load_option |= aiProcess_FixInfacingNormals;
        //load_option |= aiProcess_OptimizeGraph;
        //load_option |= aiProcess_FlipUVs;

        //importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, false);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, false);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
        if (m_importNormals == ModelImporterNormals::Calculate) {
            importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_NORMALS);
            load_option |= aiProcess_GenSmoothNormals |aiProcess_RemoveComponent;
        } else {
            load_option |= aiProcess_GenNormals;
        }
        bool load_tangent = (m_importTangents != ModelImporterTangents::None);
        if (load_tangent)
            load_option |= aiProcess_CalcTangentSpace;
        const aiScene* scene = importer.ReadFile(path.c_str(), load_option);
        if (!scene) {
            Debug::LogError(importer.GetErrorString());
            Debug::LogError("Can not open file %s", path.c_str());
            abort();
        }

        bool load_uv = (m_vertexUsages & (int)VertexUsage::UV) != 0;
        
        m_model = std::make_shared<Model>();
        m_model->m_name = split(path, "/").back();
        bool isFBX = boost::to_lower_copy(m_model->m_name.substr(m_model->m_name.size() - 3)) == "fbx";
        
        bool loadAnimation = scene->HasAnimations();
        if (loadAnimation)
            Debug::Log("%s has animation", path.c_str());

        m_model->m_rootNode = buildModelTree(scene->mRootNode);
        
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; meshIndex++)
        {
            auto mesh = ParseMesh(scene->mMeshes[meshIndex], load_uv, load_tangent);
            if (mesh->m_name.empty()) {
                mesh->m_name = "mesh" + boost::lexical_cast<std::string>(m_model->m_meshes.size());
            }

            m_model->AddMesh(mesh);
        }

        for (uint32_t animIndex = 0; animIndex < scene->mNumAnimations; ++animIndex) {
            auto animation = ParseAnimation(scene->mAnimations[animIndex], m_fileScale);
#ifdef REMOVE_FBX_PIVOT
            if (isFBX)
                RemoveDummyNodeFBX(animation);
#endif
            m_model->m_animations.push_back(animation);
        }

        return m_model;
    }


    void ModelImporter::
    RemoveDummyNodeFBX(
        PAnimation animation)
    {
        for (auto& it : m_nodeTransformations) {
            auto& name = it.first;
            AnimationNode resultNode;
            resultNode.name = name;

            // position
            {
                auto it2 = it.second.find("Translation");
                if (it2 == it.second.end()) {   // No translation subnode;
                    resultNode.positionKeys.emplace_back(Vector3Key{ 0.f, Vector3::zero });
                }
                else {
                    Matrix4x4 positionMat = it2->second;
                    Vector3 initPosition(positionMat.m[0][3], positionMat.m[1][3], positionMat.m[2][3]);
                    auto fullName = name + "_$AssimpFbx$_Translation";
                    auto it3 = animation->channels.find(fullName);
                    if (it3 == animation->channels.end()) { // no animation on Translation
                        resultNode.positionKeys.emplace_back(Vector3Key{ 0.f, initPosition });
                    }
                    else {
#ifdef DEBUG_ANIMATION
                        assert(it3->second.rotationKeys.size() == 1);
                        assert(it3->second.rotationKeys[0].value == Quaternion::identity);
                        assert(it3->second.scalingKeys.size() == 1);
                        assert(it3->second.scalingKeys[0].value == Vector3::one);
#endif
                        resultNode.positionKeys = it3->second.positionKeys;
                        animation->channels.erase(it3);
                    }
                }
            }

            // rotation
            {
                Matrix4x4 preRotation, postRotation;
                {
                    auto it2 = it.second.find("PreRotation");
                    if (it2 != it.second.end()) {
                        preRotation = it2->second;
                    }
                    auto it3 = it.second.find("PostRotation");
                    if (it3 != it.second.end()) {
                        postRotation = it3->second;
                    }
                }

                auto it2 = it.second.find("Rotation");
                if (it2 == it.second.end()) {   // No rotation subnode;
                    auto rotMat = preRotation * postRotation;
#ifdef DEBUG_ANIMATION
                    Vector3 pos;
                    Quaternion rot;
                    Vector3 scale;
                    Matrix4x4::Decompose(rotMat, &pos, &rot, &scale);
                    assert(pos == Vector3::zero);
                    assert(scale == Vector3::one);
                    assert(rot == rotMat.ToRotation());
#else
                    Quaternion rot = rotMat.ToRotation();
#endif
                    resultNode.rotationKeys.emplace_back(QuaternionKey{ 0.f, rot });
                }
                else {
                    auto fullName = name + "_$AssimpFbx$_Rotation";
                    auto it3 = animation->channels.find(fullName);
                    if (it3 == animation->channels.end()) { // no animation on rotation
                        Matrix4x4 rotMat = preRotation * it2->second * postRotation;
#ifdef DEBUG_ANIMATION
                        Vector3 pos;
                        Quaternion initRotation;
                        Vector3 scale;
                        Matrix4x4::Decompose(rotMat, &pos, &initRotation, &scale);
                        assert(pos == Vector3::zero);
                        assert(scale == Vector3::one);
                        assert(initRotation == rotMat.ToRotation());
#else
                        Quaternion initRotation = rotMat.ToRotation();
#endif
                        resultNode.rotationKeys.emplace_back(QuaternionKey{ 0.f, initRotation });
                    }
                    else {
#ifdef DEBUG_ANIMATION
                        assert(it3->second.positionKeys.size() == 1);
                        assert(it3->second.positionKeys[0].value == Vector3::zero);
                        assert(it3->second.scalingKeys.size() == 1);
                        assert(it3->second.scalingKeys[0].value == Vector3::one);
#endif
                        for (auto& rk : it3->second.rotationKeys) {
                            auto rotMat = preRotation * Matrix4x4::FromRotation(rk.value) * postRotation;
                            Vector3 pos;
                            Quaternion rot;
                            Vector3 scale;
                            Matrix4x4::Decompose(rotMat, &pos, &rot, &scale);
                            resultNode.rotationKeys.emplace_back(QuaternionKey{ rk.time, rot });
                        }
                        //resultNode.rotationKeys = it3->second.rotationKeys;
                        animation->channels.erase(it3);
                    }
                }
            }

            // scale
            {
                auto it2 = it.second.find("Scaling");
                if (it2 == it.second.end()) {   // No scaling subnode;
                    resultNode.scalingKeys.emplace_back(Vector3Key{ 0.f, Vector3::one });
                }
                else {
                    auto fullName = name + "_$AssimpFbx$_Scaling";
                    auto it3 = animation->channels.find(fullName);
                    if (it3 == animation->channels.end()) { // no animation on Scaling
                        Matrix4x4 scaleMat = it2->second;
                        Vector3 pos;
                        Quaternion rot;
                        Vector3 initScale;
                        Matrix4x4::Decompose(scaleMat, &pos, &rot, &initScale);
                        assert(pos == Vector3::zero);
                        assert(rot == Quaternion::identity);
                        resultNode.scalingKeys.emplace_back(Vector3Key{ 0.f, initScale });
                    }
                    else {
#ifdef DEBUG_ANIMATION
                        assert(it3->second.positionKeys.size() == 1);
                        assert(it3->second.positionKeys[0].value == Vector3::zero);
                        assert(it3->second.rotationKeys.size() == 1);
                        assert(it3->second.rotationKeys[0].value == Quaternion::identity);
#endif
                        resultNode.scalingKeys = it3->second.scalingKeys;
                        animation->channels.erase(it3);
                    }
                }
            }

            animation->channels[name] = resultNode;
        }
    }

    PGameObject Model::
    CreateGameObject() const
    {
        std::map<std::string, std::weak_ptr<GameObject>> nameToGameObject;
        auto root = ResursivelyCreateGameObject(m_rootNode, nameToGameObject);
        if (m_animations.size() > 0)
        {
            auto animator = std::make_shared<Animator>();
            animator->m_animation = m_animations.front();
            root->AddComponent(animator);
        }
        return root;
    }


    PGameObject Model::
    ResursivelyCreateGameObject(
        const PModelNode & node,
        std::map<std::string, std::weak_ptr<GameObject>>& nameToGameObject) const
    {
        auto go = Scene::CreateGameObject(node->name);
        go->transform()->setLocalToWorldMatrix(node->transform);
        nameToGameObject[node->name] = go;

        if (m_rootGameObject.expired())
        {
            m_rootGameObject = go;
        }

        if (node->meshesIndices.size() == 1)
        {
            const auto& mesh = m_meshes[node->meshesIndices.front()];
            mesh->setName(node->name);
            auto material = Material::defaultMaterial();
            if (mesh->m_skinned)
            {
                auto meshRenderer = std::make_shared<SkinnedMeshRenderer>(material);
                meshRenderer->setSharedMesh(mesh);
                meshRenderer->setAvatar(m_avatar);
                meshRenderer->setRootBone(m_rootGameObject.lock()->transform());
                go->AddComponent(meshRenderer);
                //m_skinnedMeshRenderersToFindLCA.push_back(meshRenderer);
            }
            else
            {
                auto meshRenderer = std::make_shared<MeshRenderer>(material);
                go->AddComponent(meshRenderer);
                auto meshFilter = std::make_shared<MeshFilter>(mesh);
                go->AddComponent(meshFilter);
            }
        }
        else if (node->meshesIndices.size() > 1)
        {
            for (auto& idx : node->meshesIndices)
            {
                auto& m = m_meshes[idx];
                auto child = Scene::CreateGameObject(m->name());
                child->transform()->SetParent(go->transform());
                nameToGameObject[m->name()] = child;
                const auto& mesh = m_meshes[idx];
                auto material = Material::defaultMaterial();
                if (mesh->m_skinned)
                {
                    auto meshRenderer = std::make_shared<SkinnedMeshRenderer>(material);
                    meshRenderer->setSharedMesh(mesh);
                    meshRenderer->setAvatar(m_avatar);
                    meshRenderer->setRootBone(m_rootGameObject.lock()->transform());
                    child->AddComponent(meshRenderer);
                    //m_skinnedMeshRenderersToFindLCA.push_back(meshRenderer);
                }
                else
                {
                    auto meshRenderer = std::make_shared<MeshRenderer>(material);
                    child->AddComponent(meshRenderer);
                    auto meshFilter = std::make_shared<MeshFilter>(mesh);
                    child->AddComponent(meshFilter);
                }
            }
        }
        
        for (auto& c : node->children)
        {
            auto child = ResursivelyCreateGameObject(c, nameToGameObject);
            child->transform()->SetParent(go->transform());
        }
        
        return go;
    }
}
