// Controls tab — mirrors SettingsDialogWin32.TabControls.inl.

const float sectionHeight = S(240.f);
const float panelGap = S(4.f);
const float panelInnerPadding = S(8.f);
const float buttonWidth = S(170.f);
const float buttonHeight = S(30.f);
const float footerRowHeight = S(40.f);
const float dividerWidth = S(1.f);
const float iconSize = S(44.f);
const ImVec4 sectionDividerColor = Palette().sectionDivider;

const float rowStartY = ImGui::GetCursorPosY();
const float availWidth = ImGui::GetContentRegionAvail().x;
const float panelWidth = (availWidth - (panelGap * 2.f) - dividerWidth) * 0.5f;

auto drawControlPanel = [&](const char* panelId, const char* bodyText, const char* buttonLabel, const char* url,
                            ImTextureID icon) {
    ImGui::BeginChild(panelId, ImVec2(panelWidth, sectionHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(panelInnerPadding, panelInnerPadding));
    const float iconGap = S(8.f);
    const bool showIcon = icon != ImTextureID_Invalid;
    ImGui::PushTextWrapPos(panelWidth - panelInnerPadding - (showIcon ? iconSize + iconGap : 0.f));
    ImGui::TextWrapped("%s", bodyText);
    ImGui::PopTextWrapPos();

    if (showIcon)
    {
        ImGui::SetCursorPos(ImVec2(panelWidth - panelInnerPadding - iconSize, panelInnerPadding));
        ImGui::Image(icon, ImVec2(iconSize, iconSize));
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
                 "Open web remote", kUrlWebRemote, ImTextureID_Invalid);

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
                 "Open playlist browser", kUrlPlaylists, DialogImage("play-playlist.png", iconSize));

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

const ImVec2 footerLabelSize = ImGui::CalcTextSize(footerLabel);

const float footerGroupWidth = footerLabelSize.x + footerGap + buttonWidth;
const float footerGroupStartX = footerStartX + (footerAvailWidth - footerGroupWidth) * 0.5f;
const float footerTextY = footerStartY + (footerRowHeight - footerLabelSize.y) * 0.5f;
const float footerButtonY = footerStartY + (footerRowHeight - buttonHeight) * 0.5f;

ImGui::SetCursorPos(ImVec2(footerGroupStartX, footerTextY));
ImGui::TextUnformatted(footerLabel);

ImGui::SetCursorPos(ImVec2(footerGroupStartX + footerLabelSize.x + footerGap, footerButtonY));
if (ImGui::Button("View help page", ImVec2(buttonWidth, buttonHeight)))
    PlatformUtils::OpenURLExternally(kUrlHelp);
