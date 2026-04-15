static std::wstring Utf8ToWidePath(const std::string& utf8)
{
    if (utf8.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0')
        w.pop_back();
    return w;
}

static std::string WideToUtf8Path(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return std::string();
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), n, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0')
        utf8.pop_back();
    return utf8;
}

static bool ChooseContentFolder(char* outBuf, size_t outSize)
{
    HRESULT coInitHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninit = SUCCEEDED(coInitHr);
    const bool canProceed = SUCCEEDED(coInitHr) || coInitHr == RPC_E_CHANGED_MODE;
    if (!canProceed)
        return false;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog)
    {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    if (outBuf && outBuf[0] != '\0')
    {
        const std::wstring folder = Utf8ToWidePath(std::string(outBuf));
        if (!folder.empty())
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))))
            {
                dialog->SetFolder(item);
                item->Release();
            }
        }
    }

    HWND owner = nullptr;
    if (auto* dx = TryGetDx11Display())
        owner = dx->GetWindowHandle();

    bool selected = false;
    if (SUCCEEDED(dialog->Show(owner)))
    {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result)
        {
            PWSTR folderW = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &folderW)) && folderW)
            {
                const std::string folder = WideToUtf8Path(folderW);
                if (!folder.empty())
                {
                    std::strncpy(outBuf, folder.c_str(), outSize - 1);
                    outBuf[outSize - 1] = '\0';
                    selected = true;
                }
                CoTaskMemFree(folderW);
            }
            result->Release();
        }
    }

    dialog->Release();
    if (shouldUninit)
        CoUninitialize();
    return selected;
}
