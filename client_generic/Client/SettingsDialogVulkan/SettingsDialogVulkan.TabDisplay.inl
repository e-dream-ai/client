// Display tab — mirrors SettingsDialogWin32.TabDisplay.inl.

const float availW = ImGui::GetContentRegionAvail().x;
const float availH = ImGui::GetContentRegionAvail().y;
const float checkSz = ImGui::GetFrameHeight();
const float labelW  = ImGui::CalcTextSize("Preserve Aspect Ratio").x;
const float totalW  = checkSz + ImGui::GetStyle().ItemInnerSpacing.x + labelW;
const float cx = (availW - totalW) * 0.5f;
const float cy = (availH - checkSz) * 0.5f;
ImGui::SetCursorPos(ImVec2(cx > 0.f ? cx : 0.f, cy > 0.f ? cy : 0.f));
StyledCheckbox("Preserve Aspect Ratio", &g_preserveAR);
