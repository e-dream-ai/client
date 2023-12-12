#ifndef _STORAGE_H
#define _STORAGE_H

#include "SmartPtr.h"
#include "base.h"
#include <string>

namespace TupleStorage
{

/*
        IStorageInterface.

*/
class IStorageInterface
{
  private:
    bool m_bDirty;

  protected:
    std::string m_sRoot;

    void Dirty(bool _state) { m_bDirty = _state; };
    bool Dirty() { return m_bDirty; };

  public:
    IStorageInterface() : m_bDirty(false), m_sRoot("./.Reg"){};
    virtual ~IStorageInterface(){};

    std::string Root() { return (m_sRoot); };

    //
    virtual bool Initialise(std::string_view _sRoot,
                            std::string_view _sWorkingDir,
                            bool _bReadOnly = false) = PureVirtual;
    virtual bool Finalise() = PureVirtual;

    //	Set values.
    virtual bool Set(std::string_view _entry, bool _val) = PureVirtual;
    virtual bool Set(std::string_view _entry, int32 _val) = PureVirtual;
    virtual bool Set(std::string_view _entry, fp8 _val) = PureVirtual;
    virtual bool Set(std::string_view _entry,
                     std::string_view _str) = PureVirtual;

    //	Get values.
    virtual bool Get(std::string_view _entry, bool& _ret) = PureVirtual;
    virtual bool Get(std::string_view _entry, int32& _ret) = PureVirtual;
    virtual bool Get(std::string_view _entry, fp8& _ret) = PureVirtual;
    virtual bool Get(std::string_view _entry,
                     std::string& _ret) = PureVirtual;

    //	Remove node from storage.
    virtual bool Remove(std::string_view _entry) = PureVirtual;

    //	Persist changes.
    virtual bool Commit() = PureVirtual;

    //	Helpers.
    static bool IoHierarchyHelper(std::string_view _uniformPath,
                                  std::string& _retPath, std::string& _retName);
    static bool CreateDir(std::string_view _sPath);
    static bool RemoveDir(std::string_view _sPath);
    static bool CreateFullDirectory(std::string_view _sPath);
    static bool RemoveFullDirectory(std::string_view _sPath,
                                    const bool _bSubdirectories = false);
    static bool DirectoryEmpty(std::string_view _sPath);

    //	Config.
    virtual bool Config(std::string_view _url) = PureVirtual;
};

MakeSmartPointers(IStorageInterface);

}; // namespace TupleStorage

#endif
