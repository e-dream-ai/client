const float diskFontScale = (ImGui::GetFontSize() >= 22.0f) ? 1.0f : (14.0f / 17.0f); // Don't shrink large accessibility fonts.
ImGui::SetWindowFontScale(diskFontScale);

const float contentWidth = ImGui::GetContentRegionAvail().x;
const float groupHeight = 162.f;
const float chooseButtonWidth = 98.f;
const float chooseButtonHeight = std::round(ImGui::GetFrameHeight() * 1.15f);
const float horizontalInset = 14.f;
const float groupGap = 2.f;
const float bottomBorderReserve = 26.f; // Move the content-folder group up a bit more.
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
// StyledRadioButton uses a temporary, smaller FramePadding than the dialog's global style.
// Center this row using the *radio* frame height so the label + radio + text align.
const ImVec2 basePad = ImGui::GetStyle().FramePadding;
const ImVec2 radioPad(std::max(3.0f, std::floor(basePad.x * 0.55f + 0.5f)),
                      std::max(2.0f, std::floor(basePad.y * 0.55f + 0.5f)));
ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, radioPad);
const float radioFrameHeight = ImGui::GetFrameHeight();
ImGui::PopStyleVar();
const float rowHeight = (textHeight > radioFrameHeight) ? textHeight : radioFrameHeight;
const float centeredTextY = rowTopY + (rowHeight - textHeight) * 0.5f;
const float centeredFrameY = rowTopY + (rowHeight - radioFrameHeight) * 0.5f;

ImGui::SetCursorPos(ImVec2(bodyInset, centeredTextY));
ImGui::TextUnformatted(usageText);

const float optionsX = contentWidth - 185.f;
const char* maxDiskLabel = "Max. disk space:";
const float maxDiskLabelWidth = ImGui::CalcTextSize(maxDiskLabel).x;
float maxDiskLabelX = optionsX - maxDiskLabelWidth - 6.f;
const float usageRightX = bodyInset + ImGui::CalcTextSize(usageText).x + 14.f;
if (maxDiskLabelX < usageRightX)
    maxDiskLabelX = usageRightX;

// Align the label baseline to match the text inside the framed radio control.
ImGui::SetCursorPos(ImVec2(maxDiskLabelX, centeredFrameY + radioPad.y));
ImGui::TextUnformatted(maxDiskLabel);

ImGui::SetCursorPos(ImVec2(optionsX, centeredFrameY));
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.5f);
if (StyledRadioButton("##unlimited_cache", g_unlimitedCache))
    g_unlimitedCache = true;
ImGui::SameLine(0.f, 8.f);
ImGui::AlignTextToFramePadding();
ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.f);
ImGui::TextUnformatted("Unlimited");

const float radioToInputGapY = std::max(6.0f, std::round(ImGui::GetStyle().ItemSpacing.y * 1.25f));
const float secondRowY = centeredFrameY + radioFrameHeight + radioToInputGapY;
ImGui::SetCursorPos(ImVec2(optionsX, secondRowY));
if (StyledRadioButton("##limited_cache", !g_unlimitedCache))
    g_unlimitedCache = false;
ImGui::SameLine(0.f, 8.f);
ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.f);
ImGui::AlignTextToFramePadding();
ImGui::BeginDisabled(g_unlimitedCache);
ImGui::PushItemWidth(84.f);
ImGui::InputInt("##max_cache_gb", &g_cacheSizeGb, 0, 0, ImGuiInputTextFlags_CharsDecimal);
if (g_cacheSizeGb < 0)
    g_cacheSizeGb = 0;
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();
ImGui::EndDisabled();
ImGui::SameLine(0.f, 8.f);
ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.f);
ImGui::AlignTextToFramePadding();
ImGui::TextUnformatted("GB");

ImGui::EndChild();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + groupGap);
ImGui::BeginChild("disk_content_folder_group", ImVec2(contentWidth, groupHeight), true,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
ImGui::SetCursorPos(ImVec2(horizontalInset, 10.f));
if (g_boldUiFont)
    ImGui::PushFont(g_boldUiFont);
ImGui::TextUnformatted("Content Folder");
if (g_boldUiFont)
    ImGui::PopFont();

ImGui::SetCursorPos(ImVec2(horizontalInset, 45.f));
ImGui::PushItemWidth(contentWidth - (horizontalInset * 2.f));
ImGui::InputText("##content_folder", g_contentDirBuf, sizeof g_contentDirBuf);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();

ImGui::SetCursorPos(ImVec2(contentWidth - horizontalInset - chooseButtonWidth, 100.f));
if (ImGui::Button("Choose...", ImVec2(chooseButtonWidth, chooseButtonHeight)))
{
    if (ChooseContentFolder(g_contentDirBuf, sizeof g_contentDirBuf))
        SetStatus("Content folder updated.");
}
ImGui::EndChild();
ImGui::SetWindowFontScale(1.0f);
