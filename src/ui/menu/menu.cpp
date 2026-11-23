#include <stdafx.hpp>
#include <render/presentation.hpp>
#include <platform/windows/input.hpp>
#include <scripting/runtime.hpp>
#include <config/first_run.hpp>
#include <config/settings.hpp>
#include <game/input/bindings.hpp>
#include <config/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <features/chams/preview.hpp>
#include <features/chams/renderer.hpp>
#include <render/text/localization.hpp>
#include <ui/menu/menu.hpp>
#include <ui/overlay/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#include <ui/menu/internal.hpp>
#include <ui/overlay/overlay.hpp>

namespace ui
{
using namespace ui::menu::detail;

namespace
{
#include "logo_no_bg_rgba.inl"
#ifndef ZOOWIE_EXTERNAL_ONLY
#include "skinchanger_icon_rgba.inl"
#endif
#include "exit_icon_rgba.inl"
#include "zoowie_logo_rgba.inl"

struct logo_texture
{
    ID3D11ShaderResourceView* srv{};
    int w{};
    int h{};
};

logo_texture create_rgba_texture(ID3D11Device* device, const unsigned char* rgba, int w, int h)
{
    logo_texture out{};
    if (!device || !rgba || w <= 0 || h <= 0)
        return out;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(w);
    desc.Height = static_cast<UINT>(h);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = rgba;
    sub.SysMemPitch = static_cast<UINT>(w * 4);
    ID3D11Texture2D* tex{};
    if (SUCCEEDED(device->CreateTexture2D(&desc, &sub, &tex)) && tex)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(tex, &srvDesc, &out.srv);
        tex->Release();
        out.w = w;
        out.h = h;
    }
    return out;
}

logo_texture load_logo_texture(ID3D11Device* device, const wchar_t* path)
{
    logo_texture out{};
    if (!device || !path)
        return out;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory)
        return out;
    IWICBitmapDecoder* decoder{};
    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) || !decoder)
    {
        factory->Release();
        return out;
    }
    IWICBitmapFrameDecode* frame{};
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame)
    {
        decoder->Release();
        factory->Release();
        return out;
    }
    IWICFormatConverter* converter{};
    if (FAILED(factory->CreateFormatConverter(&converter)) || !converter)
    {
        frame->Release();
        decoder->Release();
        factory->Release();
        return out;
    }
    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return out;
    }
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (!w || !h)
    {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return out;
    }
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data())))
    {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return out;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = pixels.data();
    sub.SysMemPitch = w * 4;
    ID3D11Texture2D* tex{};
    if (SUCCEEDED(device->CreateTexture2D(&desc, &sub, &tex)) && tex)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(tex, &srvDesc, &out.srv);
        tex->Release();
        out.w = static_cast<int>(w);
        out.h = static_cast<int>(h);
    }
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return out;
}
} // namespace

bool menu_t::initialize(HWND hwnd)
{
    m_hwnd = hwnd;
    return true;
}

void menu_t::map_pointer_to_layout(float& x, float& y, const float display_width,
                                   const float display_height) const noexcept
{
    if (!is_open())
        return;
    const auto scale = current_menu_scale(display_width, display_height);
    if (std::abs(scale - 1.0f) < 0.0001f)
        return;
    const auto origin = menu_transform_origin(display_width, display_height);
    x = origin.x + (x - origin.x) / scale;
    y = origin.y + (y - origin.y) / scale;
}

void menu_t::map_pointer_to_screen(float& x, float& y, const float display_width,
                                   const float display_height) const noexcept
{
    if (!is_open())
        return;
    const auto scale = current_menu_scale(display_width, display_height);
    if (std::abs(scale - 1.0f) < 0.0001f)
        return;
    const auto origin = menu_transform_origin(display_width, display_height);
    x = origin.x + (x - origin.x) * scale;
    y = origin.y + (y - origin.y) * scale;
}

void menu_t::reset_content_animation()
{
    m_content_animation = 0.0f;
}

void menu_t::poll_hotkey()
{
    // A fresh installation shows the guide by itself, without the menu; it waits for the movement keys like a normal open.
    const bool guide_wanted = config::first_run::pending();
    if (!guide_wanted)
        m_guide_open_pending = false;
    else if (!m_guide_visible.load(std::memory_order_relaxed) && !m_open.load(std::memory_order_relaxed))
        m_guide_open_pending = true;

    const auto menu_key = config::lifecycle_keys().menu;
    const bool hotkey_is_down = (::GetAsyncKeyState(menu_key) & 0x8000) != 0;
    // While the guide is up it is modal: the menu key waits until the guide is finished.
    const bool guide_modal = guide_wanted && m_guide_visible.load(std::memory_order_relaxed);
    if (hotkey_is_down && !m_menu_hotkey_was_down && !guide_modal)
    {
        if (m_open.load(std::memory_order_relaxed))
        {
            m_open.store(false, std::memory_order_release);
            m_open_pending = false;
        }
        else
            m_open_pending = !m_open_pending;
    }
    m_menu_hotkey_was_down = hotkey_is_down;

    const bool open_menu = m_open_pending && !m_open.load(std::memory_order_relaxed);
    if (!open_menu && !m_guide_open_pending)
        return;

    std::vector<std::uint16_t> movement_keys{};
    constexpr std::array movement_actions{
        game::input_action::forward,
        game::input_action::back,
        game::input_action::left,
        game::input_action::right,
    };
    for (const auto action : movement_actions)
    {
        for (const auto& binding : game::input_bindings().candidates(action))
        {
            if (binding.device == game::input_device::keyboard && binding.virtual_key &&
                std::ranges::find(movement_keys, binding.virtual_key) == movement_keys.end())
            {
                movement_keys.push_back(binding.virtual_key);
            }
        }
    }
    if (std::ranges::any_of(movement_keys, [](const std::uint16_t key)
                            { return platform::windows::bound_input().physical_key_down(key); }))
    {
        return;
    }

    if (open_menu)
        m_open.store(true, std::memory_order_release);
    if (m_guide_open_pending)
    {
        m_guide_front_pending = true;
        m_guide_open_pending = false;
        m_guide_visible.store(true, std::memory_order_release);
    }
    std::vector<platform::windows::input_gateway::key_transition> releases{};
    releases.reserve(movement_keys.size());
    for (const auto key : movement_keys)
        releases.push_back({key, false});
    for (std::size_t offset = 0; offset < releases.size(); offset += 8)
        platform::windows::bound_input().keys(
            std::span{releases}.subspan(offset, std::min<std::size_t>(8, releases.size() - offset)));
    platform::windows::bound_input().clear_movement_gates();
    m_open_pending = false;
    g_toggle_animations.clear();
    reset_content_animation();
}

void menu_t::draw()
{
    if (config::first_run::pending() && m_open.load(std::memory_order_acquire) &&
        !m_guide_visible.load(std::memory_order_acquire))
    {
        m_guide_front_pending = true;
        m_guide_visible.store(true, std::memory_order_release);
    }
    const bool menu_open = m_open.load(std::memory_order_acquire);
    const bool guide_visible = m_guide_visible.load(std::memory_order_acquire);
    const auto dt = ImGui::GetIO().DeltaTime;
    m_was_menu_open = menu_open;
    const float menu_target = menu_open ? 1.0f : 0.0f;
    const float menu_speed = menu_open ? 10.0f : 12.0f;
    m_menu_animation = std::clamp(m_menu_animation + (menu_target - m_menu_animation) * std::min(1.0f, dt * menu_speed), 0.0f, 1.0f);
    const bool should_draw_menu = menu_open || m_menu_animation > 0.01f;
    const bool draw_screen_snow = config::general_settings.m_menu_style.snowflakes_screen;
    if (!should_draw_menu && !guide_visible && !draw_screen_snow)
        return;
    synchronize_menu_palette();

    const auto display = ImGui::GetIO().DisplaySize;
    if (draw_screen_snow)
    {
        struct snowflake { float x, y, s, v; };
        static std::array<snowflake, 48> flakes{};
        static bool init = false;
        if (!init)
        {
            for (size_t i = 0; i < flakes.size(); ++i)
                flakes[i] = {static_cast<float>(i * 17 % 100) / 100.0f, static_cast<float>(i * 29 % 100) / 100.0f,
                             1.0f + static_cast<float>(i % 3), 18.0f + static_cast<float>(i % 20)};
            init = true;
        }
        auto* bg = ImGui::GetBackgroundDrawList();
        const float w = display.x, h = display.y;
        for (auto& f : flakes)
        {
            f.y += f.v * dt / std::max(h, 1.0f);
            if (f.y > 1.05f) { f.y = -0.05f; f.x = static_cast<float>(rand() % 100) / 100.0f; }
            bg->AddCircleFilled({f.x * w, f.y * h}, f.s, IM_COL32(255, 255, 255, 110), 6);
        }
    }
    if (!should_draw_menu && !guide_visible)
        return;
    const auto menu_scale = current_menu_scale(display.x, display.y);
    const auto transform_origin = menu_transform_origin(display.x, display.y);
    const menu_render_scope render_scope{display, transform_origin, menu_scale};
    if (should_draw_menu)
    {
        const auto dim_alpha = std::clamp(config::general_settings.m_menu_style.dim_alpha, 0, 255);
        if (dim_alpha > 0 && m_menu_animation > 0.01f)
        {
            const auto a = static_cast<int>(static_cast<float>(dim_alpha) * m_menu_animation);
            ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, a));
        }
        if (config::general_settings.m_menu_style.snowflakes && m_menu_animation > 0.01f)
        {
            struct snowflake { float x, y, s, v; };
            static std::array<snowflake, 48> flakes{};
            static bool init = false;
            if (!init)
            {
                for (size_t i = 0; i < flakes.size(); ++i)
                    flakes[i] = {static_cast<float>(i * 17 % 100) / 100.0f, static_cast<float>(i * 29 % 100) / 100.0f,
                                 1.0f + static_cast<float>(i % 3), 18.0f + static_cast<float>(i % 20)};
                init = true;
            }
            auto* bg = ImGui::GetBackgroundDrawList();
            const float w = display.x, h = display.y;
            const auto snow_a = static_cast<int>(120.0f * m_menu_animation);
            for (auto& f : flakes)
            {
                f.y += f.v * dt / std::max(h, 1.0f);
                if (f.y > 1.05f) { f.y = -0.05f; f.x = static_cast<float>(rand() % 100) / 100.0f; }
                bg->AddCircleFilled({f.x * w, f.y * h}, f.s, IM_COL32(255, 255, 255, snow_a), 6);
            }
        }


        const auto initial_position = initial_menu_layout_position(display.x, display.y);
        ImGui::SetNextWindowPos(initial_position, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({k_menu_width, k_menu_height}, ImGuiCond_Always);
        const auto menu_rounding = static_cast<float>(std::clamp(config::general_settings.m_menu_style.rounding, 0, 20));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, menu_rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, k_text_main);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4{0, 0, 0, 0});
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, k_border_light);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, k_text_muted);
        ImGuiWindowFlags menu_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
        if (!menu_open)
            menu_flags |= ImGuiWindowFlags_NoInputs;
        ImGui::Begin("##zoowie_menu", nullptr, menu_flags);

        const auto min = ImGui::GetWindowPos();
        const auto max = ImVec2{min.x + k_menu_width, min.y + k_menu_height};
        g_menu_min = min;
        g_menu_max = max;
        auto* draw = ImGui::GetWindowDrawList();
        const auto menu_bg_alpha = static_cast<unsigned int>(std::clamp(config::general_settings.m_menu_style.bg_alpha, 0, 255));
        const auto menu_header_alpha = static_cast<unsigned int>(std::clamp(config::general_settings.m_menu_style.header_alpha, 0, 255));

        constexpr float header_h = 47.0f;
        draw->PushClipRectFullScreen();
        soft_shadow(draw, min, max, menu_rounding, {0.0f, 8.0f}, 24.0f, {0, 0, 0, 1}, 0.75f);
        draw->PopClipRect();
        const auto bg_col = IM_COL32(static_cast<int>(k_bg_base.x * 255.0f), static_cast<int>(k_bg_base.y * 255.0f),
                                     static_cast<int>(k_bg_base.z * 255.0f), static_cast<int>(menu_bg_alpha));
        const auto header_col = IM_COL32(static_cast<int>(k_bg_panel.x * 255.0f), static_cast<int>(k_bg_panel.y * 255.0f),
                                         static_cast<int>(k_bg_panel.z * 255.0f), static_cast<int>(menu_header_alpha));
        // body
        draw->AddRectFilled({min.x, min.y + header_h}, max, bg_col, menu_rounding, ImDrawFlags_RoundCornersBottom);
        // header
        if (config::general_settings.m_menu_style.header_blur)
        {
            const auto ha = static_cast<unsigned int>(std::clamp(config::general_settings.m_menu_style.header_alpha, 20, 255) / 2);
            draw->AddRectFilled(min, {max.x, min.y + header_h}, IM_COL32(255, 255, 255, 18), menu_rounding, ImDrawFlags_RoundCornersTop);
            draw->AddRectFilledMultiColor(min, {max.x, min.y + header_h}, IM_COL32(255,255,255,28), IM_COL32(255,255,255,28), IM_COL32(255,255,255,8), IM_COL32(255,255,255,8));
            draw->AddRectFilled(min, {max.x, min.y + header_h}, IM_COL32(30, 30, 30, ha), menu_rounding, ImDrawFlags_RoundCornersTop);
        }
        else
        {
            draw->AddRectFilled(min, {max.x, min.y + header_h}, header_col, menu_rounding, ImDrawFlags_RoundCornersTop);
        }
        draw->AddRect(min, max, packed(k_border), menu_rounding);
        draw->AddLine({min.x, min.y + header_h - 1.0f}, {max.x, min.y + header_h - 1.0f}, IM_COL32(255, 255, 255, 13));

        // brand text
        {
            const auto* brand = render::fonts().menu_brand_30;
            if (brand && brand->im_font)
            {
                constexpr auto text = std::string_view{"Zoowie"};
                const auto brand_size = brand->font_size * 0.82f;
                const ImVec2 text_pos{min.x + 18.0f, min.y + (header_h - brand_size) * 0.5f - 1.0f};
                draw->AddText(brand->im_font, brand_size, text_pos, packed(k_text_main), text.data(), text.data() + text.size());
            }
        }

        // exit button (top-right)
        {
#ifndef ZOOWIE_EXTERNAL_ONLY
            static logo_texture sc_icon{};
#endif
            static logo_texture exit_icon{};
            if (!exit_icon.srv
#ifndef ZOOWIE_EXTERNAL_ONLY
                || !sc_icon.srv
#endif
            )
            {
                if (auto* ov = ui::overlay_t::active_instance())
                {
#ifndef ZOOWIE_EXTERNAL_ONLY
                    if (!sc_icon.srv) {
                        sc_icon = create_rgba_texture(ov->device(), k_skinchanger_icon_rgba, k_skinchanger_icon_w, k_skinchanger_icon_h);
                        g_nav_skinchanger_icon = sc_icon.srv;
                    }
#endif
                    if (!exit_icon.srv) exit_icon = create_rgba_texture(ov->device(), k_exit_icon_rgba, k_exit_icon_w, k_exit_icon_h);
                }
            }
            const float btn = 24.0f;
            const ImVec2 pmin{max.x - 18.0f - btn, min.y + (header_h - btn) * 0.5f};
            const ImVec2 pmax{pmin.x + btn, pmin.y + btn};
            ImGui::SetCursorScreenPos(pmin);
            ImGui::PushID("menu_exit");
            const bool clicked = ImGui::InvisibleButton("##menu_exit", {btn, btn});
            ImGui::PopID();
            if (exit_icon.srv) draw->AddImage((ImTextureID)exit_icon.srv, pmin, pmax, {0,0}, {1,1}, IM_COL32(255,255,255,255));
            if (clicked) { if (auto* ov = ui::overlay_t::active_instance()) ov->request_shutdown(); }
        }

        push_menu_font(render::fonts().menu_regular_12);
        draw_top_nav();
        draw_content();
        ImGui::PopFont();

        ImGui::End();

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(5);
    }

    if (guide_visible)
    {
        push_menu_font(render::fonts().menu_regular_12);
        draw_guide();
        ImGui::PopFont();
    }
    scale_menu_draw_lists(transform_origin, menu_scale, m_menu_animation);
}

void menu_t::draw_top_nav()
{
    constexpr float header_h = 47.0f;
    constexpr float nav_h = 42.0f;
    ImGui::SetCursorPos({0.0f, header_h});
    ImGui::BeginChild("##top_nav", {k_menu_width, nav_h}, false, ImGuiWindowFlags_NoScrollbar);
    const auto min = ImGui::GetWindowPos();
    const auto max = ImVec2{min.x + k_menu_width, min.y + nav_h};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, IM_COL32(255, 255, 255, 8));
    draw->AddLine({min.x, max.y - 1.0f}, {max.x, max.y - 1.0f}, IM_COL32(255, 255, 255, 13));

#ifndef ZOOWIE_EXTERNAL_ONLY
    static constexpr const char* labels[]{"Aimbot", "Triggerbot", "Visuals", "Misc", "Skinchanger"};
    constexpr int k_page_count = 5;
#else
    static constexpr const char* labels[]{"Aimbot", "Triggerbot", "Visuals", "Misc"};
    constexpr int k_page_count = 4;
#endif
    const float pad_x = 12.0f;
    const float gap = 6.0f;
    const float btn_w = (k_menu_width - pad_x * 2.0f - gap * (k_page_count - 1)) / (float)k_page_count;
    ImGui::SetCursorPos({pad_x, 4.0f});
    for (int i = 0; i < k_page_count; ++i)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, gap);
        ImGui::PushID(i);
        const auto active = m_page == i;
        if (nav_button(labels[i], active, i, btn_w) && !active)
        {
            m_page = i;
            reset_content_animation();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void menu_t::draw_content()
{
    constexpr float header_h = 47.0f;
    constexpr float nav_h = 42.0f;
    const float top = header_h + nav_h;
    ImGui::SetCursorPos({0.0f, top});
    ImGui::BeginChild("##content", {k_menu_width, k_menu_height - top}, false, ImGuiWindowFlags_NoScrollbar);
    m_content_animation = std::min(1.0f, m_content_animation + ImGui::GetIO().DeltaTime / 0.18f);
    const auto t = 1.0f - std::pow(1.0f - m_content_animation, 3.0f);
    ImGui::SetCursorPosY((1.0f - t) * 6.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (1.0f - t) * 4.0f);

    if (m_page == 0)
        draw_aimbot();
    else if (m_page == 1)
        draw_triggerbot();
    else if (m_page == 2)
        draw_visuals();
    else if (m_page == 3)
        draw_misc();
#ifndef ZOOWIE_EXTERNAL_ONLY
    else
        draw_skinchanger();
#endif

    ImGui::EndChild();
}
} // namespace ui
