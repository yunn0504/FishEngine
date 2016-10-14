#include "Shader.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cassert>
#include <set>
#include <boost/algorithm/string.hpp>

#include "Texture.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Pipeline.hpp"

using namespace std;

#if FISHENGINE_PLATFORM_WINDOWS
static const std::string include_dir = "../../assets/shaders/include/";
#else
static const std::string include_dir = "/Users/yushroom/program/graphics/FishEngine/assets/shaders/include/";
#endif

std::string readFile(const std::string& path)
{
    std::ifstream stream(path);
    if (!stream.is_open()) {
        FishEngine::Debug::LogError("Can not open shader header file: %s", path.c_str());
        abort();
    }
    std::stringstream sstream;
    sstream << stream.rdbuf();
    return sstream.str();
}

std::string processInclude(const std::string& str)
{
    auto result(str);
    std::set<std::string> loaded_headers;
    auto pos = result.find("#include", 0);
    while (pos != std::string::npos) {
        auto pos2 = result.find("\n", pos);
        if (pos2 == std::string::npos)
            pos2 = result.size();
        std::string filename = result.substr(pos+8+2, pos2-pos-8-2-1);
        if (loaded_headers.find(filename) == loaded_headers.end()) {
            loaded_headers.insert(filename);
            auto include_file = readFile(include_dir + filename);
            result = result.substr(0, pos) + include_file + result.substr(pos2);
        } else {
            result = result.substr(0, pos) + result.substr(pos2);
        }
        pos = result.find("#include", pos);
    }
    return result;
}


namespace FishEngine {

    std::string Shader::m_shaderVariables;
    std::map<std::string, PShader> Shader::m_builtinShaders;

    Shader::Shader(Shader&& s)
    {
        m_program = s.m_program;
    }

    PShader Shader::CreateFromString(const std::string& vs_str, const std::string& fs_str, const std::string& gs_str)
    {
        auto s = std::make_shared<Shader>();
        s->FromString(vs_str, fs_str, gs_str);
        return s;
    }

    PShader Shader::CreateFromString(const std::string& vs_str, const std::string& fs_str)
    {
        auto s = std::make_shared<Shader>();
        s->FromString(vs_str, fs_str);
        return s;
    }

    PShader Shader::CreateFromFile(const std::string& vs_path, const std::string& fs_path)
    {
        auto s = std::make_shared<Shader>();
        s->FromFile(vs_path, fs_path);
        return s;
    }

    PShader Shader::CreateFromFile(const std::string& vs_path, const std::string& fs_path, const std::string& gs_path)
    {
        auto s = std::make_shared<Shader>();
        s->FromFile(vs_path, fs_path, gs_path);
        return s;
    }

    void Shader::FromString(const std::string &vs_string, const std::string &fs_string)
    {
        FromString(vs_string, "", "", "", fs_string);
    }

    void Shader::FromString(const std::string& vs_string, const std::string& fs_string, const std::string& gs_string)
    {
        FromString(vs_string, "", "", gs_string, fs_string);
    }


    const char* GLenumToString(GLenum e)
    {
        switch (e) {
        case GL_FLOAT:
            return "GL_FLOAT";
        case GL_FLOAT_VEC3:
            return "GL_FLOAT_VEC3";
        case GL_FLOAT_MAT4:
            return "GL_FLOAT_MAT4";
        case GL_SAMPLER_2D:
            return "GL_SAMPLER_2D";
        case GL_SAMPLER_3D:
            return "GL_SAMPLER_3D";
        case GL_SAMPLER_CUBE:
            return "GL_SAMPLER_CUBE";
        default:
            return "UNKNOWN";
            break;
        }
    }

    GLuint
        LinkShader(
            GLuint vs,
            GLuint tcs,
            GLuint tes,
            GLuint gs,
            GLuint fs)
    {
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        if (gs != 0)
            glAttachShader(program, gs);
        if (tes != 0) {
            if (tcs != 0) glAttachShader(program, tcs);
            glAttachShader(program, tes);
        }
        glLinkProgram(program);
        GLint success;
        GLchar infoLog[1024];
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, 1024, NULL, infoLog);
            Debug::LogError("%s", infoLog);
            abort();
        }

        glDetachShader(program, vs);
        glDetachShader(program, fs);
        if (gs != 0) {
            glDetachShader(program, gs);
        }
        if (tes != 0) {
            if (tcs != 0) glDetachShader(program, tcs);
            glDetachShader(program, tes);
        }

        glCheckError();
        return program;
    }

    void Shader::FromString(const std::string& vs_string,
        const std::string& tcs_string,
        const std::string& tes_string,
        const std::string& gs_string,
        const std::string& fs_string)
    {
#if FISHENGINE_PLATFORM_WINDOWS
        const std::string root_dir = "../../assets/shaders/";
#else
        const std::string root_dir = "/Users/yushroom/program/graphics/FishEngine/assets/shaders/";
#endif
        assert(m_program == 0);
        assert(!vs_string.empty() && !fs_string.empty());
        //assert(tcs_string.empty() || (!tcs_string.empty() && !tes_string.empty()));
        assert(!(!tcs_string.empty() && tes_string.empty()));

        bool use_gs = !gs_string.empty();
        bool use_ts = !tes_string.empty();
        GLuint vs = 0;
        GLuint vs_skinned = 0;
        GLuint fs = 0;
        GLuint gs = 0;
        GLuint tcs = 0;
        GLuint tes = 0;

        auto add_line_number = [](const string& str) -> std::string {
            stringstream ss;
            int line_number = 2;
            ss << "#1 ";
            string::size_type last_pos = 0;
            auto pos = str.find('\n');
            while (pos != string::npos) {
                ss << str.substr(last_pos, pos - last_pos) << "\n#" << line_number << " ";
                last_pos = pos + 1;
                pos = str.find('\n', last_pos);
                line_number++;
            };
            return ss.str();
        };

        auto compileShader = [&add_line_number](GLuint& shader, GLenum shader_type, const std::string& shader_str) {
            const GLchar* shader_c_str = shader_str.c_str();
            shader = glCreateShader(shader_type);
            glShaderSource(shader, 1, &shader_c_str, NULL);
            glCompileShader(shader);
            GLint success = GL_FALSE;
            //GLchar infoLog[1024];
            GLint infoLogLength = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
            if (!success) {
                std::vector<char> infoLog(infoLogLength + 1);
                std::cout << add_line_number(shader_str) << endl;
                glGetShaderInfoLog(shader, infoLogLength, NULL, infoLog.data());
                //std::cout << string(&infoLog[0]) << endl;
                Debug::LogError("%s", infoLog.data());
                abort();
            }
        };

        map<string, string> settings = { {"Cull", "Back"},{"ZWrite", "On"},{"Blend", "Off"}, {"ZTest", "Less"} };

        auto extractSettings = [&settings](const string& shader_str) -> void {
            auto lines = split(shader_str, "\n");
            for (auto& line : lines) {
                boost::trim(line);
                if (boost::starts_with(line, "///")) {
                    line = line.substr(3);
                    boost::trim(line);
                    auto s = split(line, " ");
                    if (s.size() > 2) {
                        Debug::LogWarning("Incorrect shader setting format: %s", line.c_str());
                    }
                    auto res = settings.find(s[0]);
                    if (res != settings.end()) {
                        Debug::Log("\tOverride shader setting: %s", line.c_str());
                        settings[s[0]] = s[1];
                    }
                    else {
                        Debug::LogWarning("Unknown shader setting: %s", line.c_str());
                    }
                }
            }
        };

        bool hasSkinnedVersion =
            (vs_string.find("AppDataBase.inc") != std::string::npos)
            || (vs_string.find("AppDataTan.inc") != std::string::npos);

        extractSettings(vs_string);
        auto parsed_vs = m_shaderVariables + "\n" + processInclude(vs_string);
        extractSettings(fs_string);
        auto parsed_fs = m_shaderVariables + "\n" + processInclude(fs_string);

        m_cullface = ToEnum<Cullface>(settings["Cull"]);
        m_ZWrite = settings["ZWrite"] == "On";
        m_blend = settings["Blend"] == "On";

        compileShader(vs, GL_VERTEX_SHADER, "#version 410 core\n" + parsed_vs);
        if (hasSkinnedVersion)
            compileShader(vs_skinned, GL_VERTEX_SHADER, "#version 410 core\n#define SKINNED\n" + parsed_vs);
        compileShader(fs, GL_FRAGMENT_SHADER, "#version 410 core\n" + parsed_fs);

        // gs
        if (use_gs) {
            compileShader(gs, GL_GEOMETRY_SHADER, "#version 410 core\n" + m_shaderVariables + gs_string);
        }
        if (use_ts) {
            if (!tcs_string.empty())
                compileShader(tcs, GL_TESS_CONTROL_SHADER, m_shaderVariables + tcs_string);
            compileShader(tes, GL_TESS_EVALUATION_SHADER, m_shaderVariables + tes_string);
        }

        m_program = LinkShader(vs, tcs, tes, gs, fs);
        GetAllUniforms();

        if (hasSkinnedVersion) {
            m_skinnedShader = std::make_shared<Shader>();
            *m_skinnedShader = *this;
            m_skinnedShader->m_program = LinkShader(vs_skinned, tcs, tes, gs, fs);
            m_skinnedShader->GetAllUniforms();
            assert(m_uniforms.size() + 1 == m_skinnedShader->m_uniforms.size());
        }

        glDeleteShader(vs);
        glDeleteShader(vs_skinned);
        glDeleteShader(fs);
        if (gs != 0) {
            glDeleteShader(gs);
        }
        if (tes != 0) {
            if (tcs != 0) glDeleteShader(tcs);
            glDeleteShader(tes);
        }
    }

    void Shader::FromFile(const std::string& vs_path, const std::string& fs_path)
    {
        assert(m_program == 0);
        FromString(readFile(vs_path), readFile(fs_path));
    }

    void Shader::FromFile(const std::string& vs_path, const std::string& fs_path, const std::string& gs_path)
    {
        assert(m_program == 0);
        FromString(readFile(vs_path), readFile(fs_path), readFile(gs_path));
    }

    Shader::~Shader()
    {
        glDeleteProgram(m_program);
    }

    void Shader::Use() const {
        glUseProgram(this->m_program);
    }

    //GLuint Shader::getAttribLocation(const char* name) const {
    //    return glGetAttribLocation(m_program, name);
    //}

    void Shader::BindMatrixArray(const std::string& name, const std::vector<Matrix4x4>& matrixArray)
    {
        //auto loc = GetUniformLocation(name.c_str());
        //glUniformMatrix4fv(loc, matrixArray.size(), GL_TRUE, matrixArray.data()->data());
        //glCheckError();
        for (auto& u : m_uniforms) {
            if (boost::starts_with(u.name, name)) {
                glUniformMatrix4fv(u.location, matrixArray.size(), GL_TRUE, matrixArray.data()->data());
                glCheckError();
                u.binded = true;
                return;
            }
        }
        Debug::LogWarning("Uniform %s not found!", name.c_str());
    }

    //void Shader::BindUniformFloat(const char* name, const float value) const {
    //    GLint loc = GetUniformLocation(name);
    //    glUniform1f(loc, value);
    //}
    //
    //void Shader::BindUniformVec3(const char* name, const glm::vec3& value) const {
    //    GLint loc = GetUniformLocation(name);
    //    glUniform3fv(loc, 1, glm::value_ptr(value));
    //}
    //
    //void Shader::BindUniformMat4(const char* name, const glm::mat4& value) const {
    //    GLint loc = GetUniformLocation(name);
    //    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
    //}
    //
    //void Shader::BindUniformMat3(const char* name, const glm::mat3& value) const {
    //    GLint loc = GetUniformLocation(name);
    //    glUniformMatrix3fv(loc, 1, GL_FALSE, glm::value_ptr(value));
    //}
    //
    //void Shader::BindUniformTexture(const char* name, const GLuint texture, const GLuint id, GLenum textureType /*= GL_TEXTURE_2D*/) const {
    //    glActiveTexture(GLenum(GL_TEXTURE0+id));
    //    glBindTexture(textureType, texture);
    //    GLuint loc = GetUniformLocation(name);
    //    glUniform1i(loc, id);
    //}

    void Shader::BindUniforms(const ShaderUniforms& uniforms)
    {
        for (auto& u : m_uniforms) {
            if (u.type == GL_FLOAT_MAT4) {
                auto it = uniforms.mat4s.find(u.name);
                if (it != uniforms.mat4s.end()) {
                    glUniformMatrix4fv(u.location, 1, GL_TRUE, it->second.data());
                    u.binded = true;
                }
                //            else {
                //                Debug::LogWarning("%s of type %u not found", u.name.c_str(), u.type);
                //            }
            }
            else if (u.type == GL_FLOAT_VEC3) {
                auto it = uniforms.vec3s.find(u.name);
                if (it != uniforms.vec3s.end()) {
                    glUniform3fv(u.location, 1, it->second.data());
                    u.binded = true;
                }
                //            else {
                //                Debug::LogWarning("%s of type %u not found", u.name.c_str(), u.type);
                //            }
            }
            else if (u.type == GL_FLOAT) {
                auto it = uniforms.floats.find(u.name);
                if (it != uniforms.floats.end()) {
                    glUniform1f(u.location, it->second);
                    u.binded = true;
                }
            }
            else if (u.type == GL_FLOAT_VEC4) {
                auto it = uniforms.vec4s.find(u.name);
                if (it != uniforms.vec4s.end()) {
                    glUniform4fv(u.location, 1, it->second.data());
                    u.binded = true;
                }
            }
        }
    }

    void Shader::BindTextures(const std::map<std::string, PTexture>& textures)
    {
        int texture_id = 0;
        for (auto& u : m_uniforms) {
            if (u.type != GL_SAMPLER_2D && u.type != GL_SAMPLER_CUBE) continue;
            auto it = textures.find(u.name);
            if (it != textures.end()) {
                GLenum type = u.type == GL_SAMPLER_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
                //BindUniformTexture(u.name.c_str(), it->second->GLTexuture(), texture_id, type);
                glActiveTexture(GLenum(GL_TEXTURE0 + texture_id));
                glBindTexture(type, it->second->GLTexuture());
                //GLuint loc = _getUniformLocation(name);
                glUniform1i(u.location, texture_id);
                texture_id++;
                u.binded = true;
            }
            else {
                Debug::LogWarning("%s of type %s not found", u.name.c_str(), GLenumToString(u.type));
            }
        }
    }

    void Shader::PreRender() const
    {
        if (m_cullface == Cullface::Off) {
            glDisable(GL_CULL_FACE);
        }
        else {
            glCullFace((GLenum)m_cullface);
        }
        glDepthMask(m_ZWrite);
        if (m_blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    void Shader::PostRender() const
    {
        glDepthMask(GL_TRUE);
        if (m_cullface == Cullface::Off)
            glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        if (m_blend)
            glDisable(GL_BLEND);
    }

    void Shader::CheckStatus() const
    {
        for (auto& u : m_uniforms) {
            if (!u.binded) {
                Debug::LogWarning("Uniform %s not binded!", u.name.c_str());
            }
        }
    }

    PShader Shader::builtinShader(const std::string& name)
    {
        auto it = m_builtinShaders.find(name);
        if (it != m_builtinShaders.end()) {
            return it->second;
        }
        Debug::LogWarning("No built-in shader called %s", name.c_str());
        abort();
        return nullptr;
    }

    //static const char* PerDrawUniformNames[] =
    //{
    //    "MATRIX_MVP",
    //    "MATRIX_MV",
    //    "MATRIX_M",
    //    "MATRIX_V",
    //    "MATRIX_P",
    //    "MATRIX_VP",
    //    "MATRIX_IT_MV",
    //    "MATRIX_IT_M",
    //};

    void FishEngine::Shader::GetAllUniforms()
    {
        //{
        //    GLuint perDrawUBO = glGetUniformBlockIndex(m_program, "PerDraw");
        //    GLint ubo_size = 0;
        //    glGetActiveUniformBlockiv(m_program, perDrawUBO, GL_UNIFORM_BLOCK_DATA_SIZE, &ubo_size);
        //    const int perDrawUBOCount = 8;
        //    GLuint indices[perDrawUBOCount];
        //    GLint size[perDrawUBOCount];
        //    GLint offset[perDrawUBOCount];
        //    GLint type[perDrawUBOCount];
        //    glGetUniformIndices(m_program, perDrawUBOCount, PerDrawUniformNames, indices);
        //    glGetActiveUniformsiv(m_program, perDrawUBOCount, indices, GL_UNIFORM_OFFSET, offset);
        //    glGetActiveUniformsiv(m_program, perDrawUBOCount, indices, GL_UNIFORM_SIZE, size);
        //    glGetActiveUniformsiv(m_program, perDrawUBOCount, indices, GL_UNIFORM_TYPE, type);
        //    Debug::Log("here");
        //}

        GLuint blockID = glGetUniformBlockIndex(m_program, "PerDraw");
        GLint blockSize = 0;
        //assert(blockID != GL_INVALID_INDEX);
        if (blockID != GL_INVALID_INDEX)
        {
            glUniformBlockBinding(m_program, blockID, Pipeline::PerDrawUBOBindingPoint);
            glGetActiveUniformBlockiv(m_program, blockID, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
            assert(blockSize == sizeof(Pipeline::perDrawUniformData));
        }

        blockID = glGetUniformBlockIndex(m_program, "PerFrame");
        //assert(blockID != GL_INVALID_INDEX);
        if (blockID != GL_INVALID_INDEX)
        {
            glUniformBlockBinding(m_program, blockID, Pipeline::PerFrameUBOBindingPoint);
            glGetActiveUniformBlockiv(m_program, blockID, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
            assert(blockSize == sizeof(Pipeline::perFrameUniformData));
        }

        //    blockID = glGetUniformBlockIndex(m_program, "Bones");
        //    //assert(blockID != GL_INVALID_INDEX);
        //    if (blockID != GL_INVALID_INDEX)
        //    {
        //        glUniformBlockBinding(m_program, blockID, Pipeline::BonesUBOBindingPoint);
        //        glGetActiveUniformBlockiv(m_program, blockID, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
        //        assert(blockSize == sizeof(Pipeline::bonesUniformData));
        //    }

        GLint count;
        GLint size; // size of the variable
        GLenum type; // type of the variable (float, vec3 or mat4, etc)
        const GLsizei bufSize = 32; // maximum name length
        GLchar name[bufSize]; // variable name in GLSL
        GLsizei length; // name length

        glGetProgramiv(m_program, GL_ACTIVE_UNIFORMS, &count);
        //Debug::Log("Program %u, Active Uniforms: %d", m_program, count);
        m_uniforms.clear();

        for (int i = 0; i < count; i++) {
            glGetActiveUniform(m_program, (GLuint)i, bufSize, &length, &size, &type, name);
            //GLint location = glGetUniformLocation(m_program, name);
            GLint loc = glGetUniformLocation(m_program, name);
            //Debug::Log("Uniform #%d Type: %s Name: %s Loc: %d", i, GLenumToString(type).c_str(), name, loc);
            if (loc >= 0)
                m_uniforms.push_back(UniformInfo{ type, string(name), (GLuint)loc, false });
        }
    }

    GLint Shader::GetUniformLocation(const char* name) const {
        GLint loc = glGetUniformLocation(m_program, name);
        if (loc == -1) {
            Debug::LogWarning("uniform[%s] not found\n", name);
        }
        return loc;
    }


    //========== Static Region ==========

    void Shader::Init() {
#if FISHENGINE_PLATFORM_WINDOWS
        const std::string root_dir = "../../assets/shaders/";
#else
        const std::string root_dir = "/Users/yushroom/program/graphics/FishEngine/assets/shaders/";
#endif
        m_shaderVariables = readFile(root_dir + "include/ShaderVariables.inc") + "\n";
        Debug::Log("Compile shader: VisualizeNormal");
        m_builtinShaders["VisualizeNormal"] = Shader::CreateFromFile(root_dir + "VisualizeNormal.vert", root_dir + "VisualizeNormal.frag", root_dir + "VisualizeNormal.geom");
        for (auto& n : { "PBR", "VertexLit", "SkyBox", "NormalMap", "ShadowMap", "Diffuse", "ScreenTexture", "SolidColor", "Outline" }) {
            Debug::Log("Compile shader: %s", n);
            m_builtinShaders[n] = Shader::CreateFromFile(root_dir + n + ".vert", root_dir + n + ".frag");
        }
        Debug::Log("Compile shader: Texture");
        m_builtinShaders["Texture"] = Shader::CreateFromFile(root_dir + "PBR.vert", root_dir + "Texture.frag");
        Debug::Log("Compile shader: TextureDoubleSided");
        m_builtinShaders["TextureDoubleSided"] = Shader::CreateFromFile(root_dir + "PBR.vert", root_dir + "TextureDoubleSided.frag");
        Debug::Log("Compile shader: Transparent");
        m_builtinShaders["Transparent"] = Shader::CreateFromFile(root_dir + "PBR.vert", root_dir + "Transparent.frag");
        //m_builtinShaders["Diffuse"] = Shader::CreateFromFile(root_dir+"PBR.vert", root_dir + "Diffuse.frag");
    }
}
