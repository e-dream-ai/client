// sectionHeight/panelGap trimmed from the macOS XIB (190/8) so the View-Help footer row
// stays inside the dialog's content region at 150% Windows display scaling
// (where the dialog window itself is clamped to the host viewport height).
const float sectionHeight = S(160.f);
const float panelGap = S(4.f);
const float panelInnerPadding = S(8.f);
const float buttonWidth = S(170.f);
const float buttonHeight = S(30.f);
const float footerRowHeight = S(34.f);
const float dividerWidth = S(1.f);
const ImVec4 sectionDividerColor(0.90f, 0.90f, 0.90f, 1.00f);

const float controlsFontScale = 14.0f / 17.0f;
ImGui::SetWindowFontScale(controlsFontScale); // Match macOS controls-tab text and button sizing.

const float rowStartY = ImGui::GetCursorPosY();
const float availWidth = ImGui::GetContentRegionAvail().x;
const float panelWidth = (availWidth - (panelGap * 2.f) - dividerWidth) * 0.5f;

auto drawControlPanel = [&](const char* panelId, const char* bodyText, const char* buttonLabel, const char* url,
                            bool showPlaylistIcon) {
    ImGui::BeginChild(panelId, ImVec2(panelWidth, sectionHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(panelInnerPadding, panelInnerPadding));
    float textWrapPos = panelWidth - panelInnerPadding;
    if (showPlaylistIcon && g_srvPlaylistIcon && g_texPlaylistIconW > 0 && g_texPlaylistIconH > 0)
        textWrapPos -= (S(41.f) + S(8.f));
    ImGui::PushTextWrapPos(textWrapPos);
    ImGui::TextWrapped("%s", bodyText);
    ImGui::PopTextWrapPos();

    if (showPlaylistIcon && g_srvPlaylistIcon && g_texPlaylistIconW > 0 && g_texPlaylistIconH > 0)
    {
        const float iconW = S(41.f);
        const float iconH = S(44.f);
        const float iconX = panelWidth - iconW - S(6.f);
        const float iconY = S(32.f);
        ImGui::SetCursorPos(ImVec2(iconX, iconY));
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_srvPlaylistIcon)), ImVec2(iconW, iconH));
    }

    const float buttonX = (panelWidth - buttonWidth) * 0.5f;
    const float buttonY = sectionHeight - buttonHeight - S(10.f);
    ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
    if (ImGui::Button(buttonLabel, ImVec2(buttonWidth, buttonHeight)))
        PlatformUtils::OpenURLExternally(url);
    ImGui::EndChild();
};

drawControlPanel("controls_remote_panel",
                 "Use the A and D keys to adjust the speed of playback. Press F1 to see more keyboard controls. You can also interact with the remote control installed on your phone, or from a web browser:",
                 "Open web remote", kUrlWebRemote, false);

ImGui::SameLine(0.f, panelGap);
ImGui::BeginGroup();
{
    const ImVec2 dividerTop = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(dividerWidth, sectionHeight));
    const ImVec2 dividerBottom(dividerTop.x, dividerTop.y + sectionHeight);
    ImGui::GetWindowDrawList()->AddLine(dividerTop, dividerBottom,
                                        ImGui::GetColorU32(sectionDividerColor), dividerWidth);
}
ImGui::EndGroup();

ImGui::SameLine(0.f, panelGap);
drawControlPanel("controls_playlist_panel",
                 "Change your dreams by selecting a playlist from the browser. Click the button on a thumbnail to start that playlist.",
                 "Open playlist browser", kUrlPlaylists, true);

ImGui::SetCursorPosY(rowStartY + sectionHeight + panelGap);
ImGui::PushStyleColor(ImGuiCol_Separator, sectionDividerColor);
ImGui::Separator();
ImGui::PopStyleColor();
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + panelGap);

const float footerStartX = ImGui::GetCursorPosX();
const float footerStartY = ImGui::GetCursorPosY();
const float footerAvailWidth = ImGui::GetContentRegionAvail().x;
const float footerGap = S(12.f);
const char* footerLabel = "For more controls and explanation:";

ImGui::SetWindowFontScale(16.0f / 17.0f);
const ImVec2 footerLabelSize = ImGui::CalcTextSize(footerLabel);
ImGui::SetWindowFontScale(controlsFontScale);

const float footerGroupWidth = footerLabelSize.x + footerGap + buttonWidth;
const float footerGroupStartX = footerStartX + (footerAvailWidth - footerGroupWidth) * 0.5f;
const float footerTextY = footerStartY + (footerRowHeight - footerLabelSize.y) * 0.5f;
const float footerButtonY = footerStartY + (footerRowHeight - buttonHeight) * 0.5f;

ImGui::SetWindowFontScale(16.0f / 17.0f);
ImGui::SetCursorPos(ImVec2(footerGroupStartX, footerTextY));
ImGui::TextUnformatted(footerLabel);
ImGui::SetWindowFontScale(controlsFontScale);

ImGui::SetCursorPos(ImVec2(footerGroupStartX + footerLabelSize.x + footerGap, footerButtonY));
if (ImGui::Button("View Help Page", ImVec2(buttonWidth, buttonHeight)))
    PlatformUtils::OpenURLExternally(kUrlHelp);

ImGui::SetWindowFontScale(1.0f);
