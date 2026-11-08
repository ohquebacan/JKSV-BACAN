#include "ui/TitleTile.hpp"

#include "graphics/colors.hpp"
#include "logging/logger.hpp"
#include "ui/ListaSecciones.hpp"

namespace
{
    // Rediseno: todos del mismo tamano; la seleccion la marca un aro, no un zoom.
    constexpr int UNSELECTED_WIDTH_HEIGHT = ui::TitleTile::SIZE;
    constexpr int SELECTED_WIDTH_HEIGHT   = ui::TitleTile::SIZE;
    constexpr int NAME_SIZE               = 16;

}

//                      ---- Construction ----

ui::TitleTile::TitleTile(bool isFavorite, int index, sdl::SharedTexture icon, bool hasCloudBackup, bool hasLocalBackup, bool hasPartida, std::string name)
    : m_transition(0,
                   0,
                   UNSELECTED_WIDTH_HEIGHT,
                   UNSELECTED_WIDTH_HEIGHT,
                   0,
                   0,
                   UNSELECTED_WIDTH_HEIGHT,
                   UNSELECTED_WIDTH_HEIGHT,
                   m_transition.DEFAULT_THRESHOLD)
    , m_isFavorite(isFavorite)
    , m_hasCloudBackup(hasCloudBackup)
    , m_hasLocalBackup(hasLocalBackup)
    , m_hasPartida(hasPartida)
    , m_name(std::move(name))
    , m_index(index)
    , m_icon(icon) {};

//                      ---- Public functions ----

void ui::TitleTile::update(int selected)
{
    const bool isSelected = selected == m_index;
    m_isSelected          = isSelected;

    if (isSelected)
    {
        m_transition.set_target_width(SELECTED_WIDTH_HEIGHT);
        m_transition.set_target_height(SELECTED_WIDTH_HEIGHT);
    }
    else
    {
        m_transition.set_target_width(UNSELECTED_WIDTH_HEIGHT);
        m_transition.set_target_height(UNSELECTED_WIDTH_HEIGHT);
    }

    m_transition.update_width_height();
}

void ui::TitleTile::render(sdl::SharedTexture &target, int x, int y)
{

    const int width   = m_transition.get_width();
    const int height  = m_transition.get_height();
    const int renderX = x - ((width - SIZE) / 2);
    const int renderY = y - ((width - SIZE) / 2);

    m_icon->render_stretched(target, renderX, renderY, width, height);
    // Esquinas redondeadas: una mascara del color del fondo encima del icono.
    if (!sm_mask) { sm_mask = sdl::TextureManager::load("tileMask", "romfs:/Textures/TileMask.png"); }
    if (sm_mask)
    {
        sm_mask->set_color_mod(colors::CLEAR_COLOR);
        sm_mask->render_stretched(target, renderX, renderY, width, height);
    }
    // Nombre debajo, en el color de seleccion si es el elegido.
    // El nombre en dos lineas como mucho, repartido por palabras y cortado con "…" (al dibujar: medir texto
    // carga glifos, que son texturas de SDL, y la cuadricula a veces se rearma desde otro hilo).
    if (!m_nameReady)
    {
        m_lineas    = ui::lista::partir(m_name, NAME_SIZE, SIZE, 2);
        m_nameReady = true;
    }
    for (size_t l = 0; l < m_lineas.size(); l++)
    {
        sdl::text::render(target, renderX, renderY + height + 8 + static_cast<int>(l) * 21, NAME_SIZE, sdl::text::NO_WRAP,
                          m_isSelected ? colors::BLUE_GREEN : colors::DIALOG_LIGHT, m_lineas[l]);
    }
    // Rediseno (como el mockup): abajo a la izquierda, respaldo en la consola y luego nube; arriba a la
    // derecha, la partida completa; abajo a la derecha, el favorito. Todas suavizadas.
    if (!sm_badgeBase) { sm_badgeBase = sdl::TextureManager::load("badgeBase", "romfs:/Textures/BadgeBase.png"); }
    if (!sm_badgeCheck) { sm_badgeCheck = sdl::TextureManager::load("badgeCheck", "romfs:/Textures/BadgeCheck.png"); }
    if (!sm_cloudIcon) { sm_cloudIcon = sdl::TextureManager::load("cloudGlyph", "romfs:/Textures/CloudGlyph.png"); }

    const int badge = width / 5;
    int bx          = renderX + 8;
    const int by    = renderY + height - badge - 8;
    if (m_hasLocalBackup && sm_badgeBase && sm_badgeCheck)
    {
        sm_badgeBase->set_color_mod(colors::BLUE_GREEN);
        sm_badgeBase->render_stretched(target, bx, by, badge, badge);
        sm_badgeCheck->set_color_mod(colors::CLEAR_COLOR);
        sm_badgeCheck->render_stretched(target, bx, by, badge, badge);
        bx += badge + 5;
    }
    if (m_hasCloudBackup && sm_badgeBase && sm_cloudIcon)
    {
        static constexpr sdl::Color CLARO = {0xECEFEAFF};
        sm_badgeBase->set_color_mod(CLARO);
        sm_badgeBase->render_stretched(target, bx, by, badge, badge);
        sm_cloudIcon->set_color_mod(colors::CLEAR_COLOR);
        sm_cloudIcon->render_stretched(target, bx, by, badge, badge);
    }
    if (m_hasPartida)
    {
        if (!sm_partida) { sm_partida = sdl::TextureManager::load("partidaBadge", "romfs:/Textures/Partida.png"); }
        if (sm_partida)
        {
            const int pw = width * 48 / 100;
            const int ph = pw * 40 / 104;
            sm_partida->render_stretched(target, renderX + width - pw - 8, renderY + 8, pw, ph);
        }
    }
    if (m_isFavorite)
    {
        static constexpr std::string_view HEART = "\uE017";
        const int hw = sdl::text::get_width(24, HEART);
        sdl::text::render(target, renderX + width - hw - 10, by + (badge - 24) / 2, 24, sdl::text::NO_WRAP, colors::PINK, HEART);
    }
}

void ui::TitleTile::reset() noexcept
{
    m_transition.set_width(UNSELECTED_WIDTH_HEIGHT);
    m_transition.set_height(UNSELECTED_WIDTH_HEIGHT);
    m_transition.set_target_width(UNSELECTED_WIDTH_HEIGHT);
    m_transition.set_target_height(UNSELECTED_WIDTH_HEIGHT);
}

int ui::TitleTile::get_width() const noexcept { return m_transition.get_width(); }

int ui::TitleTile::get_height() const noexcept { return m_transition.get_height(); }
