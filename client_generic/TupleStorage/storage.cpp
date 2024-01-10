
#include <string>
#include <vector>
#include <filesystem>

#include "Log.h"
#include "storage.h"

using namespace std;

namespace fs = std::filesystem;

namespace TupleStorage
{

bool IStorageInterface::CreateDir(std::string_view _sPath)
{
    try
    {
        if (!fs::exists(_sPath))
        {
            fs::create_directory(_sPath);
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        g_Log->Error("Error creating directory %s: %s\n", _sPath.data(),
                     e.what());
    }

    return (true);
}

bool IStorageInterface::CreateFullDirectory(std::string_view _sPath)
{
    //	Will be false once we're out of the "../../"'s, then we will actually
    // create.
    bool dotdot = true;

    for (unsigned int C = 0; C < _sPath.size(); C++)
    {
        if (_sPath[C] == '/' || _sPath[C] == '\\')
        {
            if (dotdot == false)
            {
                auto subPath = _sPath.substr(0, C);
                if (!CreateDir(subPath))
                {
                    return (false);
                }
                //	Path created.
            }
        }
        else
        {
            if (_sPath[C] != '.')
                dotdot = false;
        }
    }

    return true;
}

}; // namespace TupleStorage
