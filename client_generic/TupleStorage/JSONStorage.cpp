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
#include "StringFormat.h"
#include "Log.h"
#include "JSONUtil.h"
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
            auto str = string_format("Exception during parsing config:%s "
                                     "contents:\"%s\"",
                                     e.what(), fileStr.data());
            //ContentDownloader::Shepherd::addMessageText(str.data(), 180);
            g_Log->Error(str.data());
        }
    }
    m_bReadOnly = _bReadOnly;
    return true;
}

bool JSONStorage::Finalise() { return false; }

template <typename T>
bool JSONStorage::GetOrSetValue(
    std::string_view _entry, T& _targetValue,
    std::function<T(json::value*)> callback, bool set,
    std::function<void(json::object*, std::string_view, T)> _emplace)
{
    std::istringstream f(_entry.data());
    std::string token;
    json::object* dict = &m_JSON;
    while (std::getline(f, token, '.'))
    {
        json::value* currentValue = dict->if_contains(token);
        if (f.eof())
        {
            if (currentValue)
            {
                if constexpr (std::is_same<T, double>::value ||
                              std::is_same<T, uint64_t>::value)
                {
                    //A whole number will always get parsed as an int, fix that
                    if (currentValue->is_int64())
                    {
                        if (set)
                        {
                            json::value newVal(_targetValue);
                            dict->at(token) = newVal;
                            currentValue = &dict->at(token);
                        }
                        else
                        {
                            int64_t intVal = currentValue->as_int64();
                            _targetValue = static_cast<T>(intVal);
                            return true;
                        }
                    }
                }
                _targetValue = callback(currentValue);
            }
            else
            {
                _emplace(dict, token, _targetValue);
                return false;
            }
        }
        else
        {
            if (currentValue)
            {
                dict = &currentValue->as_object();
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

static void EmplaceStringVectorInDictionary(json::object* _dict,
                                            std::string_view _token,
                                            std::vector<std::string> _val)
{
    json::array arr;
    for (std::string_view it : _val)
    {
        arr.push_back(it.data());
    }
    _dict->emplace(_token.data(), arr);
}

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
bool JSONStorage::Set(std::string_view _entry, std::vector<std::string> _val)
{
    GetOrSetValue<std::vector<std::string>>(
        _entry, _val,
        [=](json::value* jsonVal) -> std::vector<std::string>
        {
            json::array arr;
            for (auto it : _val)
            {
                arr.push_back(it.data());
            }
            jsonVal->as_array() = arr;
            return _val;
        },
        true, EmplaceStringVectorInDictionary);
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
bool JSONStorage::Get(std::string_view _entry, std::vector<std::string>& _ret)
{
    return GetOrSetValue<std::vector<std::string>>(
        _entry, _ret,
        [=](json::value* jsonVal) -> std::vector<std::string>
        {
            json::array arr = jsonVal->as_array();
            std::vector<std::string> vec;
            for (auto it : arr)
            {
                vec.push_back(it.as_string().data());
            }
            return vec;
        },
        false, EmplaceStringVectorInDictionary);
}

//    Remove node from storage.
bool JSONStorage::Remove(std::string_view /*_entry*/) { return true; }

//    Persist changes.
bool JSONStorage::Commit()
{
    if (Dirty() && !m_bReadOnly)
    {
        std::ofstream outFile(m_ConfigPath);
        JSONUtil::PrettyPrintJSON(outFile, m_JSON);
        outFile.close();
        Dirty(false);
    }
    return true;
}

//    Config.
bool JSONStorage::Config(std::string_view /*_url*/) { return true; }

}; // namespace TupleStorage
