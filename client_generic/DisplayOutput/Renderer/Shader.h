#ifndef _SHADER_H_
#define _SHADER_H_

#include "Log.h"
#include "SmartPtr.h"
#include <map>

namespace DisplayOutput
{

enum eUniformType
{
    eUniform_Float = 0,
    eUniform_Float2,
    eUniform_Float3,
    eUniform_Float4,
    eUniform_Int,
    eUniform_Int2,
    eUniform_Int3,
    eUniform_Int4,
    eUniform_Boolean,
    eUniform_Boolean2,
    eUniform_Boolean3,
    eUniform_Boolean4,
    eUniform_Matrix2,
    eUniform_Matrix3,
    eUniform_Matrix4,
    eUniform_Sampler,
    eUniform_NumUniformTypes,
};

// Corresponding array with sizes
const std::size_t UniformTypeSizes[eUniform_NumUniformTypes] = {
    sizeof(float),      // eUniform_Float
    sizeof(float) * 2,  // eUniform_Float2
    sizeof(float) * 3,  // eUniform_Float3
    sizeof(float) * 4,  // eUniform_Float4
    sizeof(int),        // eUniform_Int
    sizeof(int) * 2,    // eUniform_Int2
    sizeof(int) * 3,    // eUniform_Int3
    sizeof(int) * 4,    // eUniform_Int4
    sizeof(bool),       // eUniform_Boolean
    sizeof(bool) * 2,   // eUniform_Boolean2
    sizeof(bool) * 3,   // eUniform_Boolean3
    sizeof(bool) * 4,   // eUniform_Boolean4
    sizeof(float) * 4,  // eUniform_Matrix2
    sizeof(float) * 9,  // eUniform_Matrix3
    sizeof(float) * 16, // eUniform_Matrix4
    sizeof(int),        // eUniform_Sampler (assuming int for simplicity)
};

/*
        CShaderUniform.

*/
class CShaderUniform
{
  protected:
    bool m_bDirty;
    std::string m_Name;
    eUniformType m_eType;

  public:
    CShaderUniform(const std::string _name, const eUniformType _eType)
        : m_bDirty(true), m_Name(_name), m_eType(_eType)
    {
    }
    virtual ~CShaderUniform() {}

    virtual bool SetData(void* _pData, const uint32_t _size) = PureVirtual;
    virtual void Apply() = PureVirtual;
};

MakeSmartPointers(CShaderUniform);

/*
        CShader().

*/
class CShader
{
  protected:
    std::map<std::string, spCShaderUniform> m_Uniforms;
    std::map<std::string, spCShaderUniform> m_Samplers;

    virtual spCShaderUniform Uniform(const std::string _name) const
    {
        spCShaderUniform ret = NULL;

        std::map<std::string, spCShaderUniform>::const_iterator iter;

        iter = m_Uniforms.find(_name);
        if (iter != m_Uniforms.end())
            return iter->second;

        iter = m_Samplers.find(_name);
        if (iter != m_Samplers.end())
            return iter->second;

        // ThrowStr( ("Uniform '" + _name + "' not found").c_str() );
        // g_Log->Warning( "Uniform '%s' not found", _name.c_str() );
        return NULL;
    }

  public:
    CShader();
    virtual ~CShader();

    virtual bool Bind(void) = PureVirtual;
    virtual bool Unbind(void) = PureVirtual;
    virtual bool Apply(void) = PureVirtual;

    virtual bool Build(const char* _pVertexShader,
                       const char* _pFragmentShader) = PureVirtual;

    bool Set(const std::string _name, const int32_t _value)
    {
        spCShaderUniform spUniform = Uniform(_name);
        if (spUniform != NULL)
        {
            spUniform->SetData((void*)&_value, sizeof(_value));
            return true;
        }

        return false;
    }

    bool Set(const std::string _name, const float _value)
    {
        spCShaderUniform spUniform = Uniform(_name);
        if (spUniform != NULL)
        {
            spUniform->SetData((void*)&_value, sizeof(_value));
            return true;
        }

        return false;
    }

    bool Set(const std::string _name, const float _x, const float _y,
             const float _z, const float _w)
    {
        spCShaderUniform spUniform = Uniform(_name);
        if (spUniform != NULL)
        {
            float v[4] = {_x, _y, _z, _w};
            spUniform->SetData(&v, sizeof(v));
            return true;
        }

        return false;
    }
};

MakeSmartPointers(CShader);

} // namespace DisplayOutput

#endif
