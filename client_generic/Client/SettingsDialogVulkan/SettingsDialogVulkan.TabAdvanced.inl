// Advanced tab — mirrors SettingsDialogWin32.TabAdvanced.inl.
// Linux: omits "Keep screensaver enabled" (not applicable).

const float advancedFontScale = 14.0f / 17.0f;
ImGui::SetWindowFontScale(advancedFontScale);

const float availW = ImGui::GetContentRegionAvail().x;
const float leftInset = S(138.f);
const float proxyGroupTopGap = S(12.f);
const float proxyContentInsetX = S(6.f);
const float proxyContentInsetY = S(22.f);
const float labelInputGap = S(8.f);
const float fieldRowGap = S(2.f);

#ifdef DEBUG
const char* serverLabel = "Server:";
const float serverLabelW = ImGui::CalcTextSize(serverLabel).x;
ImGui::SetCursorPosX(leftInset);
ImGui::AlignTextToFramePadding();
ImGui::TextUnformatted(serverLabel);
ImGui::SameLine(0.f, labelInputGap);
ImGui::PushItemWidth(availW - leftInset - serverLabelW - labelInputGap - S(8.f));
ImGui::InputText("##server", g_serverBuf, sizeof g_serverBuf);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + proxyGroupTopGap);
#endif

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + proxyGroupTopGap);
const float proxyGroupW = availW;
const float proxyGroupH = S(160.f);
ImGui::BeginChild("advanced_proxy_group", ImVec2(proxyGroupW, proxyGroupH), true,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

if (g_boldUiFont) ImGui::PushFont(g_boldUiFont);
ImGui::SetCursorPos(ImVec2(proxyContentInsetX, S(2.f)));
ImGui::TextUnformatted("Proxy");
if (g_boldUiFont) ImGui::PopFont();

const char* hostLabel = "Host:";
const char* loginLabel = "Login:";
const char* passwordLabel = "Password:";
const float deltaLabelW = S(40.f);
const float hostLabelW = ImGui::CalcTextSize(hostLabel).x + deltaLabelW;
const float loginLabelW = ImGui::CalcTextSize(loginLabel).x + deltaLabelW;
const float passwordLabelW = ImGui::CalcTextSize(passwordLabel).x + deltaLabelW;
float labelW = hostLabelW;
if (loginLabelW > labelW) labelW = loginLabelW;
if (passwordLabelW > labelW) labelW = passwordLabelW;

const float rowStartX = proxyContentInsetX;
const float labelRightX = rowStartX + labelW;
const float inputX = rowStartX + labelW + labelInputGap;
const float proxyInputRightPadding = S(20.f);
const float inputW = proxyGroupW - inputX - proxyContentInsetX - proxyInputRightPadding;
const float startY = proxyContentInsetY;
ImGui::SetCursorPos(ImVec2(inputX, startY));
StyledCheckbox("Use Proxy", &g_useProxy);
ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX(), ImGui::GetCursorPosY() + S(40.f)));

const auto drawProxyRow = [&](const char* label, const char* id, char* buf, size_t bufSize,
                               ImGuiInputTextFlags flags) {
    const float rowY = ImGui::GetCursorPosY() + fieldRowGap;
    const float textWidth = ImGui::CalcTextSize(label).x;
    const float frameHeight = ImGui::GetFrameHeight();
    const float textHeight = ImGui::GetTextLineHeight();
    const float labelY = rowY + (frameHeight - textHeight) * 0.5f;

    ImGui::SetCursorPos(ImVec2(labelRightX - textWidth, labelY));
    ImGui::TextUnformatted(label);

    ImGui::SetCursorPos(ImVec2(inputX, rowY));
    ImGui::PushItemWidth(inputW);
    ImGui::InputText(id, buf, bufSize, flags);
    DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
    ImGui::PopItemWidth();
};

ImGui::SetCursorPos(ImVec2(rowStartX, startY + ImGui::GetFrameHeight() + fieldRowGap));
drawProxyRow(hostLabel, "##proxy_host", g_proxyHostBuf, sizeof g_proxyHostBuf, 0);
drawProxyRow(loginLabel, "##proxy_login", g_proxyLoginBuf, sizeof g_proxyLoginBuf, 0);
drawProxyRow(passwordLabel, "##proxy_password", g_proxyPasswordBuf, sizeof g_proxyPasswordBuf,
             ImGuiInputTextFlags_Password);

ImGui::EndChild();
ImGui::SetWindowFontScale(1.0f);
