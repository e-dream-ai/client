ImGui::Checkbox("Use Proxy", &g_useProxy);
ImGui::PushItemWidth(-1.f);
ImGui::InputText("Proxy Host", g_proxyHostBuf, sizeof g_proxyHostBuf);
ImGui::InputText("Proxy Login", g_proxyLoginBuf, sizeof g_proxyLoginBuf);
ImGui::InputText("Proxy Password", g_proxyPasswordBuf, sizeof g_proxyPasswordBuf,
                 ImGuiInputTextFlags_Password);
ImGui::PopItemWidth();

#ifdef DEBUG
ImGui::Separator();
ImGui::PushItemWidth(-1.f);
ImGui::InputText("Server", g_serverBuf, sizeof g_serverBuf);
ImGui::PopItemWidth();
#endif
ImGui::Checkbox("Install and update screensaver", &g_autoInstallScreensaver);
ImGui::Checkbox("Keep screensaver enabled", &g_keepScreensaverEnabled);
