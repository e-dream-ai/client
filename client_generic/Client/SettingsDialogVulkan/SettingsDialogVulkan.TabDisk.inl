// Disk tab — mirrors SettingsDialogWin32.TabDisk.inl.
// Linux: content folder is a plain text input (no native folder picker).

const float contentWidth = ImGui::GetContentRegionAvail().x;
const float groupHeight = S(72.f);
const float horizontalInset = S(14.f);
const float bottomBorderReserve = S(10.f);
const float limitBoxH = S(118.f);
const float playlistGroupH = S(62.f);
const float playlistGroupGap = ImGui::GetStyle().ItemSpacing.y;
float cacheGroupHeight = ImGui::GetContentRegionAvail().y - groupHeight - playlistGroupH - playlistGroupGap - bottomBorderReserve;
if (cacheGroupHeight < limitBoxH + S(12.f))
    cacheGroupHeight = limitBoxH + S(12.f);

ImGui::BeginChild("disk_cache_group", ImVec2(contentWidth, cacheGroupHeight), false,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

const float bodyInset = S(18.f);
const float colGap = S(20.f);
const float rightColW = S(200.f);
const float leftColW = contentWidth - rightColW - bodyInset * 2.f - colGap;

const float topY = ImGui::GetCursorPosY();

ImGui::SetCursorPos(ImVec2(bodyInset, topY));
ImGui::PushTextWrapPos(bodyInset + leftColW);
ImGui::TextWrapped("Dreams are stored on your local disk so they can be played many times without redownloading.");
ImGui::PopTextWrapPos();

const double usedCacheGb = Cache::CacheManager::getInstance().getCacheSize();
char usageText[96];
std::snprintf(usageText, sizeof usageText, "It is currently using %.2f GB.", usedCacheGb);

ImGui::SetCursorPosX(bodyInset);
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(8.f));
ImGui::TextUnformatted(usageText);

const float rightX = bodyInset + leftColW + colGap;
const float rowGap = S(6.f);
const float boxInset = S(10.f);

ImGui::SetCursorPos(ImVec2(rightX, topY));
ImGui::BeginChild("disk_usage_limit_box", ImVec2(rightColW, limitBoxH), true,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

ImGui::SetCursorPos(ImVec2(boxInset, S(6.f)));
if (g_boldUiFont) ImGui::PushFont(g_boldUiFont);
ImGui::TextUnformatted("Disk usage limit");
if (g_boldUiFont) ImGui::PopFont();

ImGui::SetCursorPos(ImVec2(boxInset, ImGui::GetCursorPosY() + rowGap));
if (StyledRadioButton("Unlimited", g_unlimitedCache, 0.8f))
    g_unlimitedCache = true;

ImGui::SetCursorPos(ImVec2(boxInset, ImGui::GetCursorPosY() + rowGap));
if (StyledRadioButton("##limited_cache", !g_unlimitedCache, 0.8f))
    g_unlimitedCache = false;
ImGui::SameLine(0.f, S(8.f));
ImGui::AlignTextToFramePadding();
ImGui::BeginDisabled(g_unlimitedCache);
ImGui::PushItemWidth(S(72.f));
ImGui::InputInt("##max_cache_gb", &g_cacheSizeGb, 0, 0, ImGuiInputTextFlags_CharsDecimal);
if (g_cacheSizeGb < 0) g_cacheSizeGb = 0;
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();
ImGui::EndDisabled();
ImGui::SameLine(0.f, S(8.f));
ImGui::AlignTextToFramePadding();
ImGui::TextUnformatted("GB");

ImGui::EndChild();
ImGui::EndChild();

// Playlist status — shows current playlist name, cached/total dream count, and
// downloader status so a misconfigured account (e.g. stuck on a tiny test
// playlist) is immediately visible without needing to read log files.
{
    auto& pm = g_Player().GetPlaylistManager();
    auto& cm = Cache::CacheManager::getInstance();

    const auto uuids = pm.getCurrentPlaylistUUIDs();
    const size_t total = pm.getPlaylistSize();
    size_t cached = 0;
    for (const auto& uuid : uuids)
        if (cm.hasDiskCachedItem(uuid)) ++cached;

    const std::string& playlistName = pm.getPlaylistName();
    const std::string dlStatus = g_ContentDownloader().m_gDownloader.GetDownloadStatus();

    char nameText[256];
    std::snprintf(nameText, sizeof nameText, "%s",
                  playlistName.empty() ? "(none)" : playlistName.c_str());

    char countText[64];
    std::snprintf(countText, sizeof countText, "%zu / %zu downloaded", cached, total);

    ImGui::BeginChild("disk_playlist_group", ImVec2(contentWidth, playlistGroupH), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetCursorPos(ImVec2(horizontalInset, S(6.f)));
    if (g_boldUiFont) ImGui::PushFont(g_boldUiFont);
    ImGui::TextUnformatted("Playlist");
    if (g_boldUiFont) ImGui::PopFont();

    const float labelEndX = ImGui::GetItemRectMax().x + S(10.f);
    ImGui::SameLine();
    ImGui::SetCursorPosX(labelEndX);
    ImGui::SetCursorPosY(S(6.f));
    ImGui::TextUnformatted(nameText);

    const float countX = contentWidth - horizontalInset - ImGui::CalcTextSize(countText).x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(countX);
    ImGui::SetCursorPosY(S(6.f));
    ImGui::TextDisabled("%s", countText);

    if (!dlStatus.empty())
    {
        ImGui::SetCursorPos(ImVec2(horizontalInset, S(30.f)));
        ImGui::TextDisabled("%s", dlStatus.c_str());
    }

    ImGui::EndChild();
}

ImGui::BeginChild("disk_content_folder_group", ImVec2(contentWidth, groupHeight), true,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
ImGui::SetCursorPos(ImVec2(horizontalInset, S(6.f)));
if (g_boldUiFont) ImGui::PushFont(g_boldUiFont);
ImGui::TextUnformatted("Content Folder");
if (g_boldUiFont) ImGui::PopFont();

ImGui::SetCursorPos(ImVec2(horizontalInset, S(30.f)));
ImGui::PushItemWidth(contentWidth - (horizontalInset * 2.f));
ImGui::InputText("##content_folder", g_contentDirBuf, sizeof g_contentDirBuf);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();
ImGui::EndChild();
