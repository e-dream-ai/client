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
            std::string str = "Exception during parsing config: ";
            str += e.what();
            str += " contents: ";
            str += fileStr;
            //ContentDownloader::Shepherd::addMessageText(str.data(), 180);
            g_Log->Error(str.c_str());
        }
    }
    m_bReadOnly = _bReadOnly;
    return true;
}

bool JSONStorage::Finalise() { return false; }

// An empty json::value of the kind the callbacks for T use, so a stored value of
// the wrong kind can be replaced before retrying a Set. int32_t is stored as an
// int64 because that's what its callbacks go through.
template <typename T> static json::value DefaultValueForType()
{
    if constexpr (std::is_same_v<T, bool>)
        return json::value(false);
    else if constexpr (std::is_same_v<T, std::string>)
        return json::value(json::string());
    else if constexpr (std::is_same_v<T, std::vector<std::string>>)
        return json::value(json::array());
    else if constexpr (std::is_same_v<T, double>)
        return json::value(0.0);
    else if constexpr (std::is_same_v<T, uint64_t>)
        return json::value(std::uint64_t{0});
    else
        return json::value(std::int64_t{0});
}

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
                // The callback extracts (or, when setting, assigns through) the
                // value via boost::json as_*() accessors, which throw if the stored
                // value isn't the expected kind — e.g. a hand-edited or out-of-range
                // number (an integer too large for int64/uint64 is parsed as a
                // double). Don't let that escape and terminate the process.
                try
                {
                    _targetValue = callback(currentValue);
                }
                catch (const std::exception& e)
                {
                    if (set)
                    {
                        // A write has to land even on a mismatched value, or the bad
                        // entry can never be corrected from inside the app: Settings::Get
                        // repairs a failed read by calling Set, and that repair would
                        // silently fail too, leaving every read to warn and rewrite the
                        // whole settings file. Replace the node with one of the right
                        // kind and let the callback write the real value into it.
                        g_Log->Warning("JSONStorage: value for '%s' has unexpected type (%s); overwriting it",
                                       std::string(_entry).c_str(), e.what());
                        dict->at(token) = DefaultValueForType<T>();
                        currentValue = &dict->at(token);
                        _targetValue = callback(currentValue);
                        return true;
                    }

                    // Reading: treat the mismatch as "not found" so the caller falls
                    // back to its default.
                    g_Log->Warning("JSONStorage: value for '%s' has unexpected type (%s); using default",
                                   std::string(_entry).c_str(), e.what());
                    return false;
                }
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
