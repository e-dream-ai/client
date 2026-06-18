// Account tab — mirrors SettingsDialogWin32.TabAccount.inl.
// Linux: uses ImGui modal popup instead of MessageBox for auth errors.

const bool loggedIn = EDreamClient::IsLoggedIn();
const float actionButtonWidth = S(110.f);
const float actionButtonHeight = S(36.f);
const float sidePadding = S(60.f);

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
const float safePanelWidth = (availW < S(300.f)) ? S(300.f) : availW;

const float formHeight = loggedIn ? S(100.f) : S(240.f);
const float topPad = (availH - formHeight) * 0.5f;
if (topPad > 0.f)
    ImGui::Dummy(ImVec2(0.f, topPad));

ImGui::BeginChild("account_centered_body", ImVec2(safePanelWidth, formHeight), false,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

if (loggedIn)
{
    // Single row: [● Signed in as …]  ···  [Sign out]
    // The text baseline is vertically centred within the button height.
    const float dotRadius  = S(10.f);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float rowY       = ImGui::GetCursorPosY() + S(4.f); // small top guard
    const float textY      = rowY + (actionButtonHeight - lineHeight) * 0.5f;

    // Green circle + "Signed in as" text
    ImGui::SetCursorPos(ImVec2(sidePadding, textY));
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 dotCenter(ImGui::GetCursorScreenPos().x + dotRadius,
                               ImGui::GetCursorScreenPos().y + lineHeight * 0.5f);
        drawList->AddCircleFilled(dotCenter, dotRadius,
                                  ImGui::ColorConvertFloat4ToU32(authColor), 24);
        ImGui::Dummy(ImVec2(dotRadius * 2.f, lineHeight));
        ImGui::SameLine(0.f, S(8.f));
        const std::string signedInText = std::string("Signed in as ") + g_nicknameBuf;
        ImGui::TextUnformatted(signedInText.c_str());
    }

    // Sign out button: right-aligned on the same row
    const float signOutX = safePanelWidth - sidePadding - actionButtonWidth;
    ImGui::SetCursorPos(ImVec2(signOutX, rowY));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.f, 1.f, 1.f, 1.f));
    if (g_boldUiFont) ImGui::PushFont(g_boldUiFont);
    if (ImGui::Button("Sign out", ImVec2(actionButtonWidth, actionButtonHeight)))
    {
        std::strncpy(g_previousLoginEmailBuf, g_nicknameBuf, sizeof g_previousLoginEmailBuf - 1);
        g_previousLoginEmailBuf[sizeof g_previousLoginEmailBuf - 1] = '\0';
        g_hasPreviousLoginEmail = true;
        EDreamClient::SignOut();
        g_sentCode = false;
        g_codeBuf[0] = '\0';
        std::strncpy(g_statusBuf, "Signed out.", sizeof g_statusBuf - 1);
    }
    if (g_boldUiFont) ImGui::PopFont();
    ImGui::PopStyleColor(4);
}

bool emailEnter = false;
bool codeEnter = false;
if (!loggedIn)
{
    ImGui::Spacing();
    const float contentW = safePanelWidth - sidePadding * 2.f;
    const float fieldGap = S(10.f);
    const float emailLabelW = ImGui::CalcTextSize("Email:").x;
    const float codeLabelW = ImGui::CalcTextSize("Code:").x;
    const float labelW = (emailLabelW > codeLabelW) ? emailLabelW : codeLabelW;
    const float emailInputW = contentW - labelW - fieldGap;
    const float codeInputW = S(96.f);
    const float leftX = sidePadding;

    ImGui::SetCursorPosX(leftX);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Email:");
    ImGui::BeginDisabled(g_sentCode);
    ImGui::SameLine(0.f, fieldGap);
    ImGui::PushItemWidth(emailInputW);
    emailEnter = InputTextWithPlaceholder("##email", "eg: john@smith.com", g_nicknameBuf,
                                          sizeof g_nicknameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    ImGui::SetCursorPosX(leftX);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Code:");
    ImGui::BeginDisabled(!g_sentCode);
    ImGui::SameLine(0.f, fieldGap);
    ImGui::PushItemWidth(codeInputW);
    codeEnter = InputTextWithPlaceholder("##code", "6 digit code", g_codeBuf, sizeof g_codeBuf,
                                         ImGuiInputTextFlags_CharsDecimal |
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    StripNonDigits(g_codeBuf);
    DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    ImGui::SetCursorPosX(leftX);
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float dotRadius = S(10.f);
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
    if (buttonX > 0.f) ImGui::SetCursorPosX(buttonX);

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
        if (g_boldUiFont) ImGui::PushFont(g_boldUiFont);
        const bool sendClicked = ImGui::Button("Send Code", ImVec2(actionButtonWidth, actionButtonHeight));
        if (sendClicked || emailEnter)
        {
            TrimWhitespaceInPlace(g_nicknameBuf);
            g_Settings()->Set("settings.generator.nickname", std::string(g_nicknameBuf));
            g_Settings()->Storage()->Commit();
            const EDreamClient::SendCodeResult result = EDreamClient::SendCode();
            g_sentCode = result.success;
            if (result.success)
            {
                std::strncpy(g_statusBuf,
                             result.message.empty() ? "Check your e-mail for confirmation code"
                                                    : result.message.c_str(),
                             sizeof g_statusBuf - 1);
                g_statusBuf[sizeof g_statusBuf - 1] = '\0';
            }
            else
            {
                std::strncpy(g_errorPopupMessage,
                             result.message.empty() ? "Failed to send verification code."
                                                    : result.message.c_str(),
                             sizeof g_errorPopupMessage - 1);
                g_errorPopupMessage[sizeof g_errorPopupMessage - 1] = '\0';
                ImGui::OpenPopup("##auth_error");
            }
        }
        if (g_boldUiFont) ImGui::PopFont();
        ImGui::PopStyleColor(4);
    }
    else
    {
        if (ImGui::Button("Start again", ImVec2(actionButtonWidth, actionButtonHeight)))
        {
            g_sentCode = false;
            g_codeBuf[0] = '\0';
            std::strncpy(g_statusBuf, "Code flow restarted.", sizeof g_statusBuf - 1);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        if (g_boldUiFont) ImGui::PushFont(g_boldUiFont);
        const bool validateClicked = ImGui::Button("Validate", ImVec2(actionButtonWidth, actionButtonHeight));
        if (validateClicked || codeEnter)
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
                std::strncpy(g_statusBuf, "Login successful.", sizeof g_statusBuf - 1);
                if (accountChanged)
                    std::exit(0);
            }
            else
            {
                std::strncpy(g_errorPopupMessage,
                             validateResult.message.empty()
                                 ? "Validation failed. Please request a new code and sign in again."
                                 : validateResult.message.c_str(),
                             sizeof g_errorPopupMessage - 1);
                g_errorPopupMessage[sizeof g_errorPopupMessage - 1] = '\0';
                ImGui::OpenPopup("##auth_error");
            }
        }
        if (g_boldUiFont) ImGui::PopFont();
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

ImGui::EndChild();

// Auth error popup (replaces Win32 MessageBox).
if (ImGui::BeginPopupModal("##auth_error", nullptr,
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
{
    ImGui::TextWrapped("%s", g_errorPopupMessage);
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(S(80.f), 0.f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
