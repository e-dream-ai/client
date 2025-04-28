//
//  JSONUtil.h
//  e-dream
//
//  Created by Tibi Hencz on 22.12.2023.
//

#ifndef JSONUtil_h
#define JSONUtil_h

#include <iostream>
#include <string>
#include <boost/json.hpp>

#include "Log.h"
#include "StringFormat.h"

namespace json = boost::json;

#define CHK(x, y)                                                              \
    x;                                                                         \
    if (JSONUtil::DumpError(ec, y))                                            \
        return false;

static constexpr const char* s_JSONKinds[] = {
    "null", "bool", "int64", "uint64", "double", "string", "array", "object"};

class JSONUtil
{
  public:
    static void LogException(const std::exception& e, std::string_view fileStr)
    {
        auto str = string_format(
            "Exception during parsing dreams list:%s contents:\"%s\"", e.what(),
            fileStr.data());
        //ContentDownloader::Shepherd::addMessageText(str.str().c_str(), 180);
        g_Log->Error(str);
    }

    static bool DumpError(const boost::system::error_code& e,
                          std::string_view fileStr)
    {
        if (!e)
            return false;
        auto str = string_format(
            "Exception during parsing dreams list:%s contents:\"%s\"",
            e.what().data(), fileStr.data());
        //ContentDownloader::Shepherd::addMessageText(str.str().c_str(), 180);
        g_Log->Error(str);
        return true;
    }

    static std::string PrintJSON(json::value const& jv)
    {
        return json::serialize(jv);
    }

    static std::string PrettyPrintJSON(json::value const& jv)
    {
        std::stringstream ss;
        PrettyPrintJSON(ss, jv);
        return ss.str();
    }

    static void PrettyPrintJSON(std::ostream& os, json::value const& jv,
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
                    PrettyPrintJSON(os, it->value(), indent);
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
                    PrettyPrintJSON(os, *it, indent);
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

    static int64_t ParseInt64(const json::value& v,
                              std::string_view _originalString)
    {
        if (!v.is_int64())
        {
            g_Log->Error("Expected an Integer but got %s in %s",
                         s_JSONKinds[(int)v.kind()], _originalString.data());
        }
        return v.as_int64();
    }
};

#endif /* JSONUtil_h */
