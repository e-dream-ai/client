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

namespace json = boost::json;

class JSONUtil
{
  public:
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
};

#endif /* JSONUtil_h */
