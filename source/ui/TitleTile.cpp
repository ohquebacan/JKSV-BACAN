#include "ui/TitleTile.hpp"

#include "graphics/colors.hpp"
#include "logging/logger.hpp"

namespace
{
    constexpr int UNSELECTED_WIDTH_HEIGHT = 128;
    constexpr int SELECTED_WIDTH_HEIGHT   = 176;
}

//                      ---- Construction ----

ui::TitleTile::TitleTile(bool isFavorite, int index, sdl::SharedTexture icon, bool hasCloudBackup)
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
    , m_index(index)
    , m_icon(icon) {};

//                      ---- Public functions ----

void ui::TitleTile::update(int selected)
{
    const bool isSelected = selected == m_index;

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
    static constexpr std::string_view HEART_CHAR = "\uE017";

    const int width   = m_transition.get_width();
    const int height  = m_transition.get_height();
    const int renderX = x - ((width - 128) / 2);
    const int renderY = y - ((width - 128) / 2);

    m_icon->render_stretched(target, renderX, renderY, width, height);
    if (m_isFavorite) { sdl::text::render(target, renderX + 2, renderY + 2, 28, sdl::text::NO_WRAP, colors::PINK, HEART_CHAR); }
    if (m_hasCloudBackup)
    {
        // Cloud icon in the top-right corner = this game has a backup on the remote storage.
        if (!sm_cloudIcon) { sm_cloudIcon = sdl::TextureManager::load("cloudBadge", "romfs:/Textures/CloudBadge.png"); }
        if (sm_cloudIcon)
        {
            const int badge  = width / 4;
            const int badgeX = renderX + width - badge - 4;
            const int badgeY = renderY + 4;
            sm_cloudIcon->render_stretched(target, badgeX, badgeY, badge, badge);
        }
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
