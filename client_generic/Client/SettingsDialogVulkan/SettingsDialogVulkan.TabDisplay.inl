// Display tab — mirrors SettingsDialogWin32.TabDisplay.inl.

const float leftPadding = S(140.f);
const float topPadding = S(80.f);

ImGui::SetCursorPos(ImVec2(leftPadding, ImGui::GetCursorPosY() + topPadding));
StyledCheckbox("Preserve Aspect Ratio", &g_preserveAR);
