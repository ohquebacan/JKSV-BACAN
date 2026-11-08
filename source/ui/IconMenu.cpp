#include "ui/IconMenu.hpp"

#include "graphics/colors.hpp"

namespace
{
    constexpr int ICON_RENDER_WIDTH  = 128;
    constexpr int ICON_RENDER_HEIGHT = 128;

    constexpr int BOUND_WIDTH  = 152;
    constexpr int BOUND_HEIGHT = 146;
}

//                      ---- Construction ----

ui::IconMenu::IconMenu(int x, int y, int renderTargetHeight)
    : Menu(x, y, 140, 70, renderTargetHeight)
{
    // Rediseno: avatar redondo de 76 con el nombre debajo, cada 126 px.
    m_optionHeight = 126;
}

//                      ---- Public functions ----

void ui::IconMenu::update(bool hasFocus) { Menu::update(hasFocus); }

void ui::IconMenu::render(sdl::SharedTexture &target, bool hasFocus)
{
    static constexpr int AVATAR = 76;
    static constexpr int CENTRO = 70;
    static sdl::SharedTexture mascara = sdl::TextureManager::load("avatarMask", "romfs:/Textures/AvatarMask.png");
    static sdl::SharedTexture aro     = sdl::TextureManager::load("avatarRing", "romfs:/Textures/AvatarRing.png");
    static sdl::SharedTexture circulo = sdl::TextureManager::load("circleFill", "romfs:/Textures/CircleFill.png");

    static sdl::SharedTexture cuadro = sdl::TextureManager::load("badgeBase", "romfs:/Textures/BadgeBase.png");
    (void)circulo;

    const int optionCount = m_options.size();
    const int y           = m_transition.get_y();

    // Botones tipo app (Ajustes, Extras): fijos abajo, separados por una linea sutil, como el mockup.
    int botones = 0;
    for (int i = 0; i < optionCount; i++) { botones += (i < static_cast<int>(m_glyphs.size()) && m_glyphs[i]) ? 1 : 0; }
    static constexpr int BOTON   = 52;
    static constexpr int PASO    = 68; // boton + aire (sin nombre debajo)
    const int topeBotones        = 700 - botones * PASO - 4;
    if (botones > 0) { sdl::render_line(target, CENTRO - 30, topeBotones, CENTRO + 30, topeBotones, colors::DIV_COLOR); }

    // Usuarios: su propio desplazamiento, para que el elegido quede siempre por encima de los botones.
    (void)y;
    {
        const int alturaUsuarios = topeBotones - 8;
        const bool eligeUsuario  = m_selected < optionCount && !(m_selected < static_cast<int>(m_glyphs.size()) && m_glyphs[m_selected]);
        if (eligeUsuario)
        {
            const double abajo  = 14 + (m_selected + 1) * m_optionHeight - alturaUsuarios;
            const double arriba = 14 + m_selected * m_optionHeight - 14;
            double objetivo     = m_scrollUsuarios;
            if (abajo > objetivo) { objetivo = abajo; }
            if (arriba < objetivo) { objetivo = arriba < 0 ? 0 : arriba; }
            m_scrollUsuarios += (objetivo - m_scrollUsuarios) / 3.0;
        }
    }

    int boton = 0;
    for (int i = 0, tempY = 14 - static_cast<int>(m_scrollUsuarios); i < optionCount; i++, tempY += m_optionHeight)
    {
        const bool elegido = i == m_selected;
        const bool glifo   = i < static_cast<int>(m_glyphs.size()) && m_glyphs[i];
        if (glifo)
        {
            const int by = topeBotones + 14 + boton * PASO;
            const int bx = CENTRO - BOTON / 2;
            ++boton;
            if (cuadro)
            {
                if (elegido)
                {
                    cuadro->set_color_mod(hasFocus ? colors::BLUE_GREEN : colors::DIV_COLOR);
                    cuadro->render_stretched(target, bx - 4, by - 4, BOTON + 8, BOTON + 8);
                }
                cuadro->set_color_mod(colors::DIALOG_DARK);
                cuadro->render_stretched(target, bx, by, BOTON, BOTON);
            }
            m_options[i]->set_color_mod(elegido && hasFocus ? colors::BLUE_GREEN : colors::WHITE);
            m_options[i]->render_stretched(target, bx + 7, by + 7, BOTON - 14, BOTON - 14);
            // Sin nombre debajo: minimalista, como el mockup.
            continue;
        }

        // Usuarios: avatar redondo con el nombre. No se dibujan sobre los botones de abajo.
        const int ax = CENTRO - AVATAR / 2;
        const int ay = tempY + 8;
        if (ay < -AVATAR + 20 || ay + AVATAR + 30 > topeBotones - 4) { continue; }
        m_options[i]->render_stretched(target, ax, ay, AVATAR, AVATAR);
        if (mascara)
        {
            mascara->set_color_mod(colors::PANEL_SOLID);
            mascara->render(target, ax, ay);
        }
        if (elegido && aro)
        {
            aro->set_color_mod(hasFocus ? colors::BLUE_GREEN : colors::DIV_COLOR);
            aro->render(target, CENTRO - 44, ay - 6);
        }
        if (i < static_cast<int>(m_labels.size()) && !m_labels[i].empty())
        {
            int size = 16;
            while (size > 12 && sdl::text::get_width(size, m_labels[i]) > 128) { --size; }
            const int w = sdl::text::get_width(size, m_labels[i]);
            sdl::text::render(target, CENTRO - w / 2, ay + AVATAR + 10, size, sdl::text::NO_WRAP,
                              elegido ? colors::BLUE_GREEN : colors::DIALOG_LIGHT, m_labels[i]);
        }
    }
}

void ui::IconMenu::add_option(sdl::SharedTexture newOption)
{
    Menu::add_option("ICON"); // Parent class needs text for this to work correctly.
    m_options.push_back(newOption);
}

void ui::IconMenu::add_option(sdl::SharedTexture newOption, std::string label, bool glyph)
{
    ui::IconMenu::add_option(newOption);
    m_labels.resize(m_options.size());
    m_labels.back() = std::move(label);
    m_glyphs.resize(m_options.size());
    m_glyphs.back() = glyph;
}
