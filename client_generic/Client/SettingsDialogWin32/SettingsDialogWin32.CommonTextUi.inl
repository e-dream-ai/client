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
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float rounding = ImGui::GetStyle().FrameRounding;
    const ImU32 borderColor = focused ? IM_COL32(0, 122, 255, 255) : IM_COL32(224, 224, 224, 255);

    if (focused)
    {
        // Subtle outer glow/shadow like macOS focus ring.
        drawList->AddRect(ImVec2(min.x - 2.f, min.y - 2.f), ImVec2(max.x + 2.f, max.y + 2.f),
                          IM_COL32(64, 132, 255, 70), rounding + 1.f, 0, 3.f);
    }

    // Inputs use a light gray border by default and blue when focused.
    drawList->AddRect(min, max, borderColor, rounding, 0, focused ? 1.6f : 1.2f);
}

static bool StyledCheckbox(const char* label, bool* value)
{
    const bool changed = ImGui::Checkbox(label, value);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const float frameSize = ImGui::GetFrameHeight();
    const ImVec2 max(min.x + frameSize, min.y + frameSize);
    const float rounding = ImGui::GetStyle().FrameRounding;
    const bool focused = ImGui::IsItemActive() || ImGui::IsItemFocused();
    const ImU32 borderColor = focused ? IM_COL32(0, 122, 255, 255) : IM_COL32(224, 224, 224, 255);

    drawList->AddRect(min, max, borderColor, rounding, 0, focused ? 1.6f : 1.2f);
    return changed;
}

static bool StyledRadioButton(const char* label, bool active)
{
    const bool changed = ImGui::RadioButton(label, active);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const float frameSize = ImGui::GetFrameHeight();
    ImVec2 center(min.x + frameSize * 0.5f, min.y + frameSize * 0.5f);
    center.x = static_cast<float>(static_cast<int>(center.x + 0.5f));
    center.y = static_cast<float>(static_cast<int>(center.y + 0.5f));
    const bool focused = ImGui::IsItemActive() || ImGui::IsItemFocused();
    const ImU32 borderColor = focused ? IM_COL32(0, 122, 255, 255) : IM_COL32(224, 224, 224, 255);
    const float radius = ((frameSize - 1.0f) * 0.5f) - 0.5f;

    drawList->AddCircle(center, radius, borderColor, 24, focused ? 1.6f : 1.2f);
    return changed;
}
