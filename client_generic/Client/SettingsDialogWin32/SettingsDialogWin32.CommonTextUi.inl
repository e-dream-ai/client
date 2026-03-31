static void TrimWhitespaceInPlace(char* s)
{
    if (!s)
        return;

    size_t len = std::strlen(s);
    size_t first = 0;
    while (first < len && std::isspace(static_cast<unsigned char>(s[first])) != 0)
        ++first;
    size_t last = len;
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1])) != 0)
        --last;

    if (first > 0)
        std::memmove(s, s + first, last - first);
    s[last - first] = '\0';
}

static void StripNonDigits(char* s)
{
    if (!s)
        return;

    size_t w = 0;
    for (size_t r = 0; s[r] != '\0'; ++r)
    {
        if (std::isdigit(static_cast<unsigned char>(s[r])) != 0)
        {
            s[w++] = s[r];
            if (w >= 6)
                break;
        }
    }
    s[w] = '\0';
}

static bool InputTextWithPlaceholder(const char* id, const char* placeholder, char* buf, size_t bufSize,
                                     ImGuiInputTextFlags flags = 0)
{
    const bool changed = ImGui::InputText(id, buf, bufSize, flags);
    if (placeholder && placeholder[0] != '\0' && buf && buf[0] == '\0' && !ImGui::IsItemActive())
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        const float textY = min.y + ((max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(min.x + pad.x, textY),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            placeholder);
    }
    return changed;
}

static void DrawFocusedInputDecoration(bool focused)
{
    if (!focused)
        return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float rounding = ImGui::GetStyle().FrameRounding;

    // Subtle outer glow/shadow like macOS focus ring.
    drawList->AddRect(ImVec2(min.x - 2.f, min.y - 2.f), ImVec2(max.x + 2.f, max.y + 2.f),
                      IM_COL32(64, 132, 255, 70), rounding + 1.f, 0, 3.f);
    // Crisp blue focus border.
    drawList->AddRect(min, max, IM_COL32(0, 122, 255, 255), rounding, 0, 1.6f);
}
