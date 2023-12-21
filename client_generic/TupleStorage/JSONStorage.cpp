//
//  JSONStorage.cpp
//  e-dream
//
//  Created by Tibor Hencz on 12.12.2023.
//

#include <fstream>
#include <sstream>
#include <iostream>
#include <boost/format.hpp>

#include "clientversion.h"
#include "Shepherd.h"
#include "Log.h"
#include "JSONStorage.h"

namespace json = boost::json;

namespace TupleStorage
{

bool JSONStorage::Initialise(std::string_view _sRoot,
                             std::string_view /*_sWorkingDir*/, bool _bReadOnly)
{
    m_sRoot = _sRoot;
    m_ConfigPath = m_sRoot + CLIENT_SETTINGS + ".json";
    std::error_code error;
    std::ifstream file(m_ConfigPath);
    if (file.is_open())
    {
        try
        {
            m_JSON = json::parse(file, error).as_object();
        }
        catch (const std::exception& e)
        {
            file.seekg(0, std::ios::beg);
            std::stringstream buffer;
            buffer << file.rdbuf();
            auto fileStr = buffer.str();
            auto str = boost::format("Exception during parsing config:%s "
                                     "contents:\"%s\"") %
                       e.what() % fileStr;
            ContentDownloader::Shepherd::addMessageText(str.str().c_str(), 180);
            g_Log->Error(str.str().data());
        }
    }
    m_bReadOnly = _bReadOnly;
    return true;
}

bool JSONStorage::Finalise() { return false; }

template <typename T>
bool JSONStorage::GetOrSetValue(std::string_view _entry, T& _val,
                                std::function<T(json::value*)> callback,
                                bool set)
{
    std::istringstream f(_entry.data());
    std::string token;
    json::object* dict = &m_JSON;
    while (std::getline(f, token, '.'))
    {
        json::value* val = dict->if_contains(token);
        if (f.eof())
        {
            if (val)
            {
                if constexpr (std::is_same<T, double>::value ||
                              std::is_same<T, uint64_t>::value)
                {
                    //A whole number will always get parsed as an int, fix that
                    if (val->is_int64())
                    {
                        if (set)
                        {
                            json::value newVal(_val);
                            dict->at(token) = newVal;
                            val = &dict->at(token);
                        }
                        else
                        {
                            int64_t intVal = val->as_int64();
                            _val = static_cast<T>(intVal);
                            return true;
                        }
                    }
                }
                _val = callback(val);
            }
            else
            {
                dict->emplace(token, _val);
                return false;
            }
        }
        else
        {
            if (val)
            {
                dict = &val->as_object();
            }
            else
            {
                json::object newVal;
                dict = &dict->emplace(token, newVal).first->value().as_object();
            }
        }
    }
    return true;
}

template bool JSONStorage::GetOrSetValue(std::string_view _entry, bool&,
                                         std::function<bool(json::value*)>,
                                         bool);
template bool JSONStorage::GetOrSetValue(std::string_view _entry, int32_t&,
                                         std::function<int32_t(json::value*)>,
                                         bool);
template bool JSONStorage::GetOrSetValue(std::string_view _entry, double&,
                                         std::function<double(json::value*)>,
                                         bool);
template bool JSONStorage::GetOrSetValue(std::string_view _entry, uint64_t&,
                                         std::function<uint64_t(json::value*)>,
                                         bool);
template bool
JSONStorage::GetOrSetValue(std::string_view _entry, std::string&,
                           std::function<std::string(json::value*)>, bool);

bool JSONStorage::Set(std::string_view _entry, bool _val)
{
    GetOrSetValue<bool>(
        _entry, _val,
        [=](json::value* jsonVal) -> bool { return jsonVal->as_bool() = _val; },
        true);
    Dirty(true);
    return true;
}

bool JSONStorage::Set(std::string_view _entry, int32_t _val)
{
    GetOrSetValue<int32_t>(
        _entry, _val,
        [=](json::value* jsonVal) -> int32_t
        { return jsonVal->as_int64() = _val; },
        true);
    Dirty(true);
    return true;
}
bool JSONStorage::Set(std::string_view _entry, double _val)
{
    GetOrSetValue<double>(
        _entry, _val,
        [=](json::value* jsonVal) -> double
        { return jsonVal->as_double() = _val; },
        true);
    Dirty(true);
    return true;
}
bool JSONStorage::Set(std::string_view _entry, uint64_t _val)
{
    GetOrSetValue<uint64_t>(
        _entry, _val,
        [=](json::value* jsonVal) -> uint64_t
        { return jsonVal->as_uint64() = _val; },
        true);
    Dirty(true);
    return true;
}
bool JSONStorage::Set(std::string_view _entry, std::string_view _str)
{
    std::string str;
    GetOrSetValue<std::string>(
        _entry, str,
        [=](json::value* jsonVal) -> std::string
        {
            jsonVal->as_string() = _str;
            return "";
        },
        true);
    Dirty(true);
    return true;
}

//    Get values.
bool JSONStorage::Get(std::string_view _entry, bool& _ret)
{
    return GetOrSetValue<bool>(
        _entry, _ret,
        [=](json::value* jsonVal) -> bool { return jsonVal->as_bool(); },
        false);
}
bool JSONStorage::Get(std::string_view _entry, int32_t& _ret)
{
    return GetOrSetValue<int32_t>(
        _entry, _ret,
        [=](json::value* jsonVal) -> int32_t
        { return (int32_t)jsonVal->as_int64(); },
        false);
}
bool JSONStorage::Get(std::string_view _entry, double& _ret)
{
    return GetOrSetValue<double>(
        _entry, _ret,
        [=](json::value* jsonVal) -> double { return jsonVal->as_double(); },
        false);
}
bool JSONStorage::Get(std::string_view _entry, uint64_t& _ret)
{
    return GetOrSetValue<uint64_t>(
        _entry, _ret,
        [=](json::value* jsonVal) -> uint64_t { return jsonVal->as_uint64(); },
        false);
}
bool JSONStorage::Get(std::string_view _entry, std::string& _ret)
{
    return GetOrSetValue<std::string>(
        _entry, _ret,
        [=](json::value* jsonVal) -> std::string
        { return jsonVal->as_string().data(); },
        false);
}

//    Remove node from storage.
bool JSONStorage::Remove(std::string_view /*_entry*/) { return true; }
static void PrettyPrint(std::ostream& os, json::value const& jv,
                        std::string* indent = nullptr)
{
    std::string indent_;
    if (!indent)
        indent = &indent_;
    switch (jv.kind())
    {
    case json::kind::object:
    {
        os << "{\n";
        indent->append(4, ' ');
        auto const& obj = jv.get_object();
        if (!obj.empty())
        {
            auto it = obj.begin();
            for (;;)
            {
                os << *indent << json::serialize(it->key()) << " : ";
                PrettyPrint(os, it->value(), indent);
                if (++it == obj.end())
                    break;
                os << ",\n";
            }
        }
        os << "\n";
        indent->resize(indent->size() - 4);
        os << *indent << "}";
        break;
    }

    case json::kind::array:
    {
        os << "[\n";
        indent->append(4, ' ');
        auto const& arr = jv.get_array();
        if (!arr.empty())
        {
            auto it = arr.begin();
            for (;;)
            {
                os << *indent;
                PrettyPrint(os, *it, indent);
                if (++it == arr.end())
                    break;
                os << ",\n";
            }
        }
        os << "\n";
        indent->resize(indent->size() - 4);
        os << *indent << "]";
        break;
    }

    case json::kind::string:
    {
        os << json::serialize(jv.get_string());
        break;
    }

    case json::kind::uint64:
        os << jv.get_uint64();
        break;

    case json::kind::int64:
        os << jv.get_int64();
        break;

    case json::kind::double_:
        os << jv.get_double();
        break;

    case json::kind::bool_:
        if (jv.get_bool())
            os << "true";
        else
            os << "false";
        break;

    case json::kind::null:
        os << "null";
        break;
    }

    if (indent->empty())
        os << "\n";
}

//    Persist changes.
bool JSONStorage::Commit()
{
    if (Dirty() && !m_bReadOnly)
    {
        std::ofstream outFile(m_ConfigPath);
        PrettyPrint(outFile, m_JSON);
        outFile.close();
        Dirty(false);
    }
    return true;
}

//    Config.
bool JSONStorage::Config(std::string_view /*_url*/) { return true; }

}; // namespace TupleStorage
