const float diskFontScale = 14.0f / 17.0f; // Closer to the macOS system-control text feel.
ImGui::SetWindowFontScale(diskFontScale);

const float contentWidth = ImGui::GetContentRegionAvail().x;
const float groupHeight = 92.f;
const float chooseButtonWidth = 98.f;
const float chooseButtonHeight = 28.f;
const float horizontalInset = 14.f;
const float groupGap = 6.f;
const float bottomBorderReserve = 10.f; // Move the content-folder group up a bit more.
float cacheGroupHeight = ImGui::GetContentRegionAvail().y - groupHeight - groupGap - bottomBorderReserve;
if (cacheGroupHeight < 86.f)
    cacheGroupHeight = 86.f;

ImGui::BeginChild("disk_cache_group", ImVec2(contentWidth, cacheGroupHeight), false,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

const float bodyInset = 18.f;
ImGui::SetCursorPosX(bodyInset);
ImGui::PushTextWrapPos(bodyInset + contentWidth - 32.f);
ImGui::TextWrapped("Dreams are stored on your local disk so they can be played many times without redownloading.");
ImGui::PopTextWrapPos();

const double usedCacheGb = Cache::CacheManager::getInstance().getCacheSize();
char usageText[96];
std::snprintf(usageText, sizeof usageText, "It is currently using %.2f GB.", usedCacheGb);

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.f);
const float rowTopY = ImGui::GetCursorPosY();
const float textHeight = ImGui::GetTextLineHeight();
const float frameHeight = ImGui::GetFrameHeight();
const float rowHeight = (textHeight > frameHeight) ? textHeight : frameHeight;
const float centeredTextY = rowTopY + (rowHeight - textHeight) * 0.5f;
const float centeredFrameY = rowTopY + (rowHeight - frameHeight) * 0.5f;

ImGui::SetCursorPos(ImVec2(bodyInset, centeredTextY));
ImGui::TextUnformatted(usageText);

const float optionsX = contentWidth - 185.f;
const char* maxDiskLabel = "Max. disk space:";
const float maxDiskLabelWidth = ImGui::CalcTextSize(maxDiskLabel).x;
float maxDiskLabelX = optionsX - maxDiskLabelWidth - 6.f;
const float usageRightX = bodyInset + ImGui::CalcTextSize(usageText).x + 14.f;
if (maxDiskLabelX < usageRightX)
    maxDiskLabelX = usageRightX;

ImGui::SetCursorPos(ImVec2(maxDiskLabelX, centeredTextY));
ImGui::TextUnformatted(maxDiskLabel);

ImGui::SetCursorPos(ImVec2(optionsX, centeredFrameY));
if (ImGui::RadioButton("Unlimited", g_unlimitedCache))
    g_unlimitedCache = true;

ImGui::SetCursorPos(ImVec2(optionsX, ImGui::GetCursorPosY() + 4.f));
if (ImGui::RadioButton("##limited_cache", !g_unlimitedCache))
    g_unlimitedCache = false;
ImGui::SameLine(0.f, 8.f);
ImGui::AlignTextToFramePadding();
ImGui::BeginDisabled(g_unlimitedCache);
ImGui::PushItemWidth(84.f);
ImGui::InputInt("##max_cache_gb", &g_cacheSizeGb, 0, 0);
ImGui::PopItemWidth();
ImGui::EndDisabled();
ImGui::SameLine(0.f, 8.f);
ImGui::AlignTextToFramePadding();
ImGui::TextUnformatted("GB");

ImGui::EndChild();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + groupGap);
ImGui::BeginChild("disk_content_folder_group", ImVec2(contentWidth, groupHeight), true,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
ImGui::SetCursorPos(ImVec2(horizontalInset, 6.f));
if (g_boldUiFont)
    ImGui::PushFont(g_boldUiFont);
ImGui::TextUnformatted("Content Folder");
if (g_boldUiFont)
    ImGui::PopFont();

ImGui::SetCursorPos(ImVec2(horizontalInset, 30.f));
ImGui::PushItemWidth(contentWidth - (horizontalInset * 2.f));
ImGui::InputText("##content_folder", g_contentDirBuf, sizeof g_contentDirBuf);
ImGui::PopItemWidth();

ImGui::SetCursorPos(ImVec2(contentWidth - horizontalInset - chooseButtonWidth, 58.f));
if (ImGui::Button("Choose...", ImVec2(chooseButtonWidth, chooseButtonHeight)))
{
    if (ChooseContentFolder(g_contentDirBuf, sizeof g_contentDirBuf))
        SetStatus("Content folder updated.");
}
ImGui::EndChild();
ImGui::SetWindowFontScale(1.0f);
