const float contentWidth = ImGui::GetContentRegionAvail().x;
const float groupHeight = S(72.f); // header + single input/button row
const float chooseButtonWidth = S(98.f);
const float horizontalInset = S(14.f);
const float bottomBorderReserve = S(10.f);
const float limitBoxH = S(118.f); // header + two radio rows + padding
float cacheGroupHeight = ImGui::GetContentRegionAvail().y - groupHeight - bottomBorderReserve;
if (cacheGroupHeight < limitBoxH + S(12.f))
    cacheGroupHeight = limitBoxH + S(12.f);

ImGui::BeginChild("disk_cache_group", ImVec2(contentWidth, cacheGroupHeight), false,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

const float bodyInset = S(18.f);
const float colGap = S(20.f);
// Right column wide enough for "Max. disk space:" header, "Unlimited" radio,
// and the "[NN] GB" radio + input row.
const float rightColW = S(200.f);
const float leftColW = contentWidth - rightColW - bodyInset * 2.f - colGap;

const float topY = ImGui::GetCursorPosY();

// Left column: explanatory copy + current usage line.
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

// Right column: bordered group containing the disk usage limit controls.
const float rightX = bodyInset + leftColW + colGap;
const float rowGap = S(6.f);
const float boxInset = S(10.f);

ImGui::SetCursorPos(ImVec2(rightX, topY));
ImGui::BeginChild("disk_usage_limit_box", ImVec2(rightColW, limitBoxH), true,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

ImGui::SetCursorPos(ImVec2(boxInset, S(6.f)));
if (g_boldUiFont)
    ImGui::PushFont(g_boldUiFont);
ImGui::TextUnformatted("Disk usage limit");
if (g_boldUiFont)
    ImGui::PopFont();

ImGui::SetCursorPos(ImVec2(boxInset, ImGui::GetCursorPosY() + rowGap));
if (StyledRadioButton("Unlimited", g_unlimitedCache))
    g_unlimitedCache = true;

ImGui::SetCursorPos(ImVec2(boxInset, ImGui::GetCursorPosY() + rowGap));
if (StyledRadioButton("##limited_cache", !g_unlimitedCache))
    g_unlimitedCache = false;
ImGui::SameLine(0.f, S(8.f));
ImGui::AlignTextToFramePadding();
ImGui::BeginDisabled(g_unlimitedCache);
ImGui::PushItemWidth(S(72.f));
ImGui::InputInt("##max_cache_gb", &g_cacheSizeGb, 0, 0, ImGuiInputTextFlags_CharsDecimal);
if (g_cacheSizeGb < 0)
    g_cacheSizeGb = 0;
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();
ImGui::EndDisabled();
ImGui::SameLine(0.f, S(8.f));
ImGui::AlignTextToFramePadding();
ImGui::TextUnformatted("GB");

ImGui::EndChild();

ImGui::EndChild();

ImGui::BeginChild("disk_content_folder_group", ImVec2(contentWidth, groupHeight), true,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
ImGui::SetCursorPos(ImVec2(horizontalInset, S(6.f)));
if (g_boldUiFont)
    ImGui::PushFont(g_boldUiFont);
ImGui::TextUnformatted("Content Folder");
if (g_boldUiFont)
    ImGui::PopFont();

// Single row: input field on the left, Choose... button on the right.
const float fieldRowGap = S(8.f);
ImGui::SetCursorPos(ImVec2(horizontalInset, S(30.f)));
ImGui::PushItemWidth(contentWidth - (horizontalInset * 2.f) - chooseButtonWidth - fieldRowGap);
ImGui::InputText("##content_folder", g_contentDirBuf, sizeof g_contentDirBuf);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();
ImGui::SameLine(0.f, fieldRowGap);
if (ImGui::Button("Choose...", ImVec2(chooseButtonWidth, 0.f)))
{
    if (ChooseContentFolder(g_contentDirBuf, sizeof g_contentDirBuf))
        SetStatus("Content folder updated.");
}
ImGui::EndChild();
