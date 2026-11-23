#include <stdafx.hpp>
#include <ui/menu/internal.hpp>
#include <ui/menu/combat_widgets.hpp>

namespace ui
{
using namespace ui::menu::detail;
using namespace ui::menu::combat;

void menu_t::begin_combat_page(bool triggerbot)
{
    static constexpr const char* groups[]{"Global", "Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "Heavy"};
    const auto child_pos = ImGui::GetWindowPos();
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({child_pos.x + 14.0f, child_pos.y + 14.0f}, {child_pos.x + ImGui::GetWindowWidth() - 14.0f, child_pos.y + 58.0f},
                      IM_COL32(255, 255, 255, 10), 4.0f);

    ImGui::SetCursorPos({22.0f, 22.0f});
    for (int i = 0; i < 7; ++i)
    {
        if (i)
            ImGui::SameLine(0.0f, 4.0f);
        if (tab_button(groups[i], m_weapon_group == i - 1) && m_weapon_group != i - 1)
        {
            m_weapon_group = i - 1;
            reset_content_animation();
        }
    }

    ImGui::SetCursorPos({14.0f, 70.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild(triggerbot ? "##trigger_cards" : "##aim_cards", {0.0f, 0.0f}, false);
    begin_cards(triggerbot ? "##trigger_grid" : "##aim_grid");
}

void menu_t::end_combat_page()
{
    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui
