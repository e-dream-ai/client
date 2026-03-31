StyledCheckbox("Use Proxy", &g_useProxy);
ImGui::PushItemWidth(-1.f);
ImGui::InputText("Proxy Host", g_proxyHostBuf, sizeof g_proxyHostBuf);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::InputText("Proxy Login", g_proxyLoginBuf, sizeof g_proxyLoginBuf);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::InputText("Proxy Password", g_proxyPasswordBuf, sizeof g_proxyPasswordBuf,
                 ImGuiInputTextFlags_Password);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();

#ifdef DEBUG
ImGui::Separator();
ImGui::PushItemWidth(-1.f);
ImGui::InputText("Server", g_serverBuf, sizeof g_serverBuf);
DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
ImGui::PopItemWidth();
#endif
StyledCheckbox("Install and update screensaver", &g_autoInstallScreensaver);
StyledCheckbox("Keep screensaver enabled", &g_keepScreensaverEnabled);
