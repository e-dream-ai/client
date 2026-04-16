const float leftPadding = 240.f;
const float topPadding = 120.f;

ImGui::SetCursorPos(ImVec2(leftPadding, ImGui::GetCursorPosY() + topPadding));
StyledCheckbox("Preserve Aspect Ratio", &g_preserveAR);
