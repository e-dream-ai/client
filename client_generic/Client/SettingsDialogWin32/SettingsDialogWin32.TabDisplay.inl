const float leftPadding = S(140.f);
const float topPadding = S(80.f);

ImGui::SetCursorPos(ImVec2(leftPadding, ImGui::GetCursorPosY() + topPadding));
StyledCheckbox("Preserve Aspect Ratio", &g_preserveAR);

ImGui::SetCursorPosX(leftPadding);
StyledCheckbox("Rotate On Portrait Displays", &g_rotatePortrait);

ImGui::SetCursorPosX(leftPadding);
StyledCheckbox("Share video with", &g_spoutOutput);
if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
{
    ImGui::SetTooltip(
        "Publish the primary display as Spout sender \"infinidream\" for OBS, Resolume, and other receivers.");
}
// "Spout" links to its home page; the checkbox's own text still toggles it.
ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
if (ImGui::TextLink("Spout"))
    PlatformUtils::OpenURLExternally(kUrlSpout);
