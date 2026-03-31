const float chooseButtonWidth = 100.f;
const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
float contentWidth = ImGui::GetContentRegionAvail().x - chooseButtonWidth - itemSpacing;
if (contentWidth < 160.f)
    contentWidth = 160.f;

ImGui::PushItemWidth(contentWidth);
ImGui::InputText("Content Folder", g_contentDirBuf, sizeof g_contentDirBuf);
ImGui::PopItemWidth();
ImGui::SameLine();
if (ImGui::Button("Choose...", ImVec2(chooseButtonWidth, 0.f)))
{
    if (ChooseContentFolder(g_contentDirBuf, sizeof g_contentDirBuf))
        SetStatus("Content folder updated.");
}
ImGui::Checkbox("Unlimited cache", &g_unlimitedCache);
if (!g_unlimitedCache)
    ImGui::InputInt("Max disk space (GB)", &g_cacheSizeGb);
