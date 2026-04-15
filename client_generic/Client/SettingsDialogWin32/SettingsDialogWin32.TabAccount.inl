const bool loggedIn = EDreamClient::IsLoggedIn();
const float actionButtonWidth = 100.f;
const float actionButtonHeight = 30.f;
const float sidePadding = 60.f;

ImVec4 authColor = ImVec4(0.89f, 0.20f, 0.24f, 1.0f);
const char* authText = "Please sign in.";
if (loggedIn)
{
    authColor = ImVec4(0.18f, 0.72f, 0.29f, 1.0f);
    authText = "Signed in";
}
else if (g_sentCode)
{
    authColor = ImVec4(0.95f, 0.66f, 0.18f, 1.0f);
    authText = "Check your e-mail for confirmation code";
}

const float availW = ImGui::GetContentRegionAvail().x;
const float availH = ImGui::GetContentRegionAvail().y;
const float safePanelWidth = (availW < 300.f) ? 300.f : availW;

const float formHeight = loggedIn ? 92.f : 230.f;
const float topPad = (availH - formHeight) * 0.5f;
if (topPad > 0.f)
    ImGui::Dummy(ImVec2(0.f, topPad));

ImGui::BeginChild("account_centered_body", ImVec2(safePanelWidth, formHeight), false,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

if (loggedIn)
{
    const std::string signedInText = std::string("Signed in as ") + g_nicknameBuf;
    const float dotRadius = 5.f;
    const float rowX = sidePadding;
    if (rowX > 0.f)
        ImGui::SetCursorPosX(rowX);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 dotCenter(ImGui::GetCursorScreenPos().x + dotRadius, ImGui::GetCursorScreenPos().y + lineHeight * 0.5f);
    drawList->AddCircleFilled(dotCenter, dotRadius, ImGui::ColorConvertFloat4ToU32(authColor), 24);
    ImGui::Dummy(ImVec2(dotRadius * 2.f, lineHeight));
    ImGui::SameLine(0.f, 8.f);
    ImGui::TextUnformatted(signedInText.c_str());
}

if (!loggedIn)
{
    ImGui::Spacing();
    const float contentW = safePanelWidth - sidePadding * 2.f;
    const float fieldGap = 10.f;
    const float emailLabelW = ImGui::CalcTextSize("Email:").x;
    const float codeLabelW = ImGui::CalcTextSize("Code:").x;
    const float labelW = (emailLabelW > codeLabelW) ? emailLabelW : codeLabelW;
    const float emailInputW = contentW - labelW - fieldGap;
    const float codeInputW = 96.f; // Match the compact macOS code input feel.
    const float leftX = sidePadding;

    ImGui::SetCursorPosX(leftX);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Email:");
    ImGui::BeginDisabled(g_sentCode);
    ImGui::SameLine(0.f, fieldGap);
    ImGui::PushItemWidth(emailInputW);
    InputTextWithPlaceholder("##email", "eg: john@smith.com", g_nicknameBuf, sizeof g_nicknameBuf);
    DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    ImGui::SetCursorPosX(leftX);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Code:");
    ImGui::BeginDisabled(!g_sentCode);
    ImGui::SameLine(0.f, fieldGap);
    ImGui::PushItemWidth(codeInputW);
    InputTextWithPlaceholder("##code", "6 digit code", g_codeBuf, sizeof g_codeBuf,
                             ImGuiInputTextFlags_CharsDecimal);
    StripNonDigits(g_codeBuf);
    DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    ImGui::SetCursorPosX(leftX);
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float dotRadius = 5.f;
        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 dotCenter(ImGui::GetCursorScreenPos().x + dotRadius,
                               ImGui::GetCursorScreenPos().y + lineHeight * 0.5f);
        drawList->AddCircleFilled(dotCenter, dotRadius, ImGui::ColorConvertFloat4ToU32(authColor), 24);
        ImGui::Dummy(ImVec2(dotRadius * 2.f, lineHeight));
        ImGui::SameLine(0.f, 8.f);
    }
    ImGui::TextUnformatted(authText);

    const float buttonRowW = actionButtonWidth * 2.f + ImGui::GetStyle().ItemSpacing.x;
    const float buttonX = safePanelWidth - sidePadding - buttonRowW;
    if (buttonX > 0.f)
        ImGui::SetCursorPosX(buttonX);

    if (!g_sentCode)
    {
        ImGui::BeginDisabled();
        ImGui::Button("Start Again", ImVec2(actionButtonWidth, actionButtonHeight));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        if (g_boldUiFont)
            ImGui::PushFont(g_boldUiFont);
        if (ImGui::Button("Send Code", ImVec2(actionButtonWidth, actionButtonHeight)))
        {
            TrimWhitespaceInPlace(g_nicknameBuf);
            g_Settings()->Set("settings.generator.nickname", std::string(g_nicknameBuf));
            g_Settings()->Storage()->Commit();
            const EDreamClient::SendCodeResult result = EDreamClient::SendCode();
            g_sentCode = result.success;
            if (result.success)
            {
                SetStatus(result.message.empty() ? "Check your e-mail for confirmation code" : result.message);
            }
            else
            {
                const AuthDialogContent dialog = BuildSendCodeFailureDialog(result);
                SetStatus(dialog.message);
                ShowAuthWarningDialog(dialog);
            }
        }
        if (g_boldUiFont)
            ImGui::PopFont();
        ImGui::PopStyleColor(4);
    }
    else
    {
        if (ImGui::Button("Start again", ImVec2(actionButtonWidth, actionButtonHeight)))
        {
            g_sentCode = false;
            g_codeBuf[0] = '\0';
            SetStatus("Code flow restarted.");
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        if (g_boldUiFont)
            ImGui::PushFont(g_boldUiFont);
        if (ImGui::Button("Validate", ImVec2(actionButtonWidth, actionButtonHeight)))
        {
            StripNonDigits(g_codeBuf);
            const EDreamClient::ValidateCodeResult validateResult =
                EDreamClient::ValidateCodeDetailed(std::string(g_codeBuf));
            if (validateResult.success)
            {
                const bool accountChanged =
                    g_hasPreviousLoginEmail &&
                    std::strcmp(g_previousLoginEmailBuf, g_nicknameBuf) != 0;
                EDreamClient::DidSignIn();
                g_sentCode = false;
                SetStatus("Login successful.");
                if (accountChanged)
                {
                    MessageBoxA(nullptr,
                                "infinidream will now exit. Please restart the application "
                                "to take your new settings into account.",
                                "Account Change Detected", MB_OK | MB_ICONINFORMATION);
                    std::exit(0);
                }
            }
            else
            {
                const AuthDialogContent dialog = BuildValidateFailureDialog(validateResult);
                SetStatus(dialog.message);
                ShowAuthWarningDialog(dialog);
            }
        }
        if (g_boldUiFont)
            ImGui::PopFont();
        ImGui::PopStyleColor(4);
    }

    ImGui::SetCursorPosX(sidePadding);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.86f, 0.86f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.82f, 0.82f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
    if (ImGui::Button("Need an account? Create one", ImVec2(contentW, actionButtonHeight)))
        PlatformUtils::OpenURLExternally(kUrlCreateAccount);
    ImGui::PopStyleColor(4);
}
else
{
    const float signOutX = safePanelWidth - sidePadding - actionButtonWidth;
    if (signOutX > 0.f)
        ImGui::SetCursorPosX(signOutX);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    if (g_boldUiFont)
        ImGui::PushFont(g_boldUiFont);
    if (ImGui::Button("Sign out", ImVec2(actionButtonWidth, actionButtonHeight)))
    {
        std::strncpy(g_previousLoginEmailBuf, g_nicknameBuf, sizeof g_previousLoginEmailBuf - 1);
        g_previousLoginEmailBuf[sizeof g_previousLoginEmailBuf - 1] = '\0';
        g_hasPreviousLoginEmail = true;
        EDreamClient::SignOut();
        g_sentCode = false;
        g_codeBuf[0] = '\0';
        SetStatus("Signed out.");
    }
    if (g_boldUiFont)
        ImGui::PopFont();
    ImGui::PopStyleColor(4);
}

ImGui::EndChild();
