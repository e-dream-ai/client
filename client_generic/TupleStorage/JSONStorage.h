//
//  JSONStorage.h
//  e-dream
//
//  Created by Tibor Hencz on 12.12.2023.
//

#ifndef JSONStorage_h
#define JSONStorage_h

#include <string_view>
#include <boost/json.hpp>

#include "storage.h"

namespace TupleStorage
{
class JSONStorage : public IStorageInterface
{

  private:
    boost::json::object m_JSON;
    std::string m_ConfigPath;
    bool m_bReadOnly;
    template <typename T>
    bool GetOrSetValue(std::string_view _entry, T& _value,
                       std::function<T(boost::json::value*)> callback,
                       bool set);

  public:
    virtual bool Initialise(std::string_view _sRoot,
                            std::string_view _sWorkingDir,
                            bool _bReadOnly = false) override;
    virtual bool Finalise() override;

    //    Set values.
    virtual bool Set(std::string_view _entry, bool _val) override;
    virtual bool Set(std::string_view _entry, int32_t _val) override;
    virtual bool Set(std::string_view _entry, double _val) override;
    virtual bool Set(std::string_view _entry, uint64_t _val) override;
    virtual bool Set(std::string_view _entry, std::string_view _str) override;

    //    Get values.
    virtual bool Get(std::string_view _entry, bool& _ret) override;
    virtual bool Get(std::string_view _entry, int32_t& _ret) override;
    virtual bool Get(std::string_view _entry, double& _ret) override;
    virtual bool Get(std::string_view _entry, uint64_t& _ret) override;
    virtual bool Get(std::string_view _entry, std::string& _ret) override;

    //    Remove node from storage.
    virtual bool Remove(std::string_view _entry) override;

    //    Persist changes.
    virtual bool Commit() override;

    //    Config.
    virtual bool Config(std::string_view _url) override;
};
}; // namespace TupleStorage

#endif /* JSONStorage_h */
