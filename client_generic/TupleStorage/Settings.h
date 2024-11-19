#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <string_view>
#include <boost/thread.hpp>

#include "Log.h"
#include "Singleton.h"
#include "SmartPtr.h"
#include "base.h"
#include "storage.h"
#include "JSONStorage.h"

/**
        CSettings.
        Singleton class to handle application settings.
*/
MakeSmartPointers(CSettings);
class CSettings : public Base::CSingleton<CSettings>
{
    friend class Base::CSingleton<CSettings>;

    //	Private constructor accessible only to CSingleton.
    CSettings() { m_pStorage = NULL; }

    //	Private destructor accessible only to CSingleton.
    virtual ~CSettings()
    {
        //	Mark singleton as properly shutdown, to track unwanted access
        // after this
        // point.
        SingletonActive(false);
    }

    //	No copy constructor or assignment operator.
    NO_CLASS_STANDARDS(CSettings);

    //	Threadsafe tuple storage object.
    TupleStorage::IStorageInterface* m_pStorage;

  public:
    const char* Description() { return ("Settings"); };

    bool Shutdown(void)
    {
        g_Log->Info("Shutdown()...");

        if (m_pStorage)
        {
            m_pStorage->Commit();
        }

        return true;
    }

    //	Root url.
    std::string Root()
    {
        if (!m_pStorage)
            return "?";

        return m_pStorage->Root();
    };

    //	Init.
    bool Init(std::string_view _sRoot, std::string_view _workingDir,
              bool _bReadOnly = false)
    {
        m_pStorage = new TupleStorage::JSONStorage();
        return m_pStorage->Initialise(_sRoot, _workingDir, _bReadOnly);
    }

    //	Set 32bit integer, double precision floationg point, and string.
    void Set(std::string_view _url, const bool _value)
    {
        if (!m_pStorage)
            return;
        m_pStorage->Set(_url, _value);
    }
    void Set(std::string_view _url, const int32_t _value)
    {
        if (!m_pStorage)
            return;
        m_pStorage->Set(_url, _value);
    }
    void Set(std::string_view _url, const double _value)
    {
        if (!m_pStorage)
            return;
        m_pStorage->Set(_url, _value);
    }
    void Set(std::string_view _url, const uint64_t _value)
    {
        if (!m_pStorage)
            return;
        m_pStorage->Set(_url, _value);
    }
    void Set(std::string_view _url, std::string_view _value)
    {
        if (!m_pStorage)
            return;
        m_pStorage->Set(_url, _value);
    }

    bool Get(std::string_view _url, const bool _default = false)
    {
        bool ret = _default;
        if (m_pStorage && !m_pStorage->Get(_url, ret))
        {
            m_pStorage->Set(_url, _default);
            m_pStorage->Commit();
            return _default;
        }
        return ret;
    }

    int32_t Get(std::string_view _url, const int32_t _default = 0)
    {
        int32_t ret = _default;
        if (m_pStorage && !m_pStorage->Get(_url, ret))
        {
            m_pStorage->Set(_url, _default);
            m_pStorage->Commit();
            return _default;
        }
        return ret;
    }

    double Get(std::string_view _url, const double _default = 0.0)
    {
        double ret = _default;
        if (m_pStorage && !m_pStorage->Get(_url, ret))
        {
            m_pStorage->Set(_url, _default);
            m_pStorage->Commit();
            return _default;
        }
        return ret;
    }

    uint64_t Get(std::string_view _url, const uint64_t _default = 0)
    {
        uint64_t ret = _default;
        if (m_pStorage && !m_pStorage->Get(_url, ret))
        {
            m_pStorage->Set(_url, _default);
            m_pStorage->Commit();
            return _default;
        }
        return ret;
    }

    std::string Get(std::string_view _url,
                    const std::string_view _default = nullptr)
    {
        std::string ret;
        if (m_pStorage && !m_pStorage->Get(_url, ret))
        {
            m_pStorage->Set(_url, _default);
            m_pStorage->Commit();
            return std::string(_default);
        }
        
        if (ret.empty()) {
            return std::string(_default);
        } else {
            return ret;
        }
            
    }

    std::vector<std::string> Get(std::string_view _url,
                                 const std::vector<std::string> _default = {})
    {
        std::vector<std::string> ret = _default;
        if (m_pStorage && !m_pStorage->Get(_url, ret))
        {
            m_pStorage->Set(_url, _default);
            m_pStorage->Commit();
        }
        return ret;
    }

    ///	Direct access to storage.
    TupleStorage::IStorageInterface* Storage() { return m_pStorage; }

    ///	Singleton instance method.
    __attribute__((no_instrument_function)) static CSettings* Instance()
    {
        static CSettings storage;

        if (storage.SingletonActive() == false)
        {
            printf("Trying to access shutdown singleton %s",
                   storage.Description());
        }

        return (&storage);
    }
};

/*
        Helper for less typing...

*/
__attribute__((no_instrument_function))
_LIBCPP_INLINE_VISIBILITY inline tpCSettings
g_Settings(void)
{
    return (CSettings::Instance());
}

#endif
