#ifndef _SHADERMETAL_H_
#define _SHADERMETAL_H_

#import <Metal/Metal.h>

#include <map>
#include <vector>

#include "Log.h"
#include "Shader.h"

namespace DisplayOutput
{

class CShaderUniformMetal : public CShaderUniform
{
  std::vector<uint8_t> m_Data;
  int32 m_Index;
  size_t m_Size;

public:
  CShaderUniformMetal(const std::string _name, const eUniformType _eType,
                      const int32 _index = 0, const size_t _size = 0)
      : CShaderUniform(_name, _eType), m_Index(_index), m_Size(_size)
  {
    m_Data.resize(_size);
  };

  virtual bool SetData(void *_pData, const uint32 _size)
  {
    std::memcpy(m_Data.data(), _pData, _size);
    return true;
  }
  const uint8_t *GetData() const { return m_Data.data(); }
  int32 GetIndex() const { return m_Index; }
  size_t GetSize() const { return m_Size; }
  virtual void Apply(){};
};

MakeSmartPointers(CShaderUniformMetal);
/*
    CShaderMetal().

*/
class CShaderMetal : public CShader
{
  id<MTLRenderPipelineState> m_PipelineState;
  std::vector<uint8_t> m_UniformBuffer;
  std::vector<spCShaderUniformMetal> m_SortedUniformRefs;

public:
  CShaderMetal(id<MTLDevice> device, id<MTLFunction> vertexFunction,
               id<MTLFunction> fragmentFunction,
               MTLVertexDescriptor *vertexDescriptor,
               std::vector<std::pair<std::string, eUniformType>> _uniforms);
  virtual ~CShaderMetal();

  virtual bool Bind(void);
  virtual bool Unbind(void);
  virtual bool Apply(void);

  virtual bool Build(const char *, const char *) { return true; };
  id<MTLRenderPipelineState> GetPipelineState() const
  {
    return m_PipelineState;
  }
  void UploadUniforms(id<MTLRenderCommandEncoder> renderEncoder);
};

MakeSmartPointers(CShaderMetal);

} // namespace DisplayOutput

#endif
