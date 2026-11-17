// Display tab — mirrors SettingsDialogWin32.TabDisplay.inl.

const float availW = ImGui::GetContentRegionAvail().x;
const float availH = ImGui::GetContentRegionAvail().y;
const float checkSz = ImGui::GetFrameHeight();
// The checkboxes are centered as a group, left-aligned with each other.
const float labelW  = ImGui::CalcTextSize("Rotate On Portrait Displays").x;  // the longest
const float totalW  = checkSz + ImGui::GetStyle().ItemInnerSpacing.x + labelW;
const float totalH  = checkSz * 2.f + ImGui::GetStyle().ItemSpacing.y;
const float cx = (availW - totalW) * 0.5f;
const float cy = (availH - totalH) * 0.5f;
const float left = cx > 0.f ? cx : 0.f;
ImGui::SetCursorPos(ImVec2(left, cy > 0.f ? cy : 0.f));
StyledCheckbox("Preserve Aspect Ratio", &g_preserveAR);
ImGui::SetCursorPosX(left);
StyledCheckbox("Rotate On Portrait Displays", &g_rotatePortrait);
