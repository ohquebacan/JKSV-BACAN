#include "ui/TitleTile.hpp"

#include "graphics/colors.hpp"
#include "logging/logger.hpp"

namespace
{
    constexpr int UNSELECTED_WIDTH_HEIGHT = 128;
    constexpr int SELECTED_WIDTH_HEIGHT   = 176;
}

//                      ---- Construction ----

ui::TitleTile::TitleTile(bool isFavorite, int index, sdl::SharedTexture icon, bool hasCloudBackup, bool hasLocalBackup)
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
    if (m_hasLocalBackup)
    {
        // Local-backup badge: a green square with a white check in the bottom-right corner. Distinct corner
        // and colour from the cloud badge so a game with both backups shows two separate markers.
        static constexpr sdl::Color LOCAL_GREEN = {0x33CC55FF};
        const int badge  = width / 5;
        const int badgeX = renderX + width - badge - 5;
        const int badgeY = renderY + height - badge - 5;

        sdl::render_rect_fill(target, badgeX - 2, badgeY - 2, badge + 4, badge + 4, colors::WHITE); // border
        sdl::render_rect_fill(target, badgeX, badgeY, badge, badge, LOCAL_GREEN);                   // fill

        // White check: two strokes (down to the bottom vertex, then up to the top-right). Drawn a few times
        // with a 1px offset so it stays legible at tile size.
        const int ax = badgeX + badge * 24 / 100, ay = badgeY + badge * 52 / 100;
        const int bx = badgeX + badge * 42 / 100, by = badgeY + badge * 70 / 100;
        const int cx = badgeX + badge * 78 / 100, cy = badgeY + badge * 28 / 100;
        for (int t = 0; t < 3; t++)
        {
            sdl::render_line(target, ax, ay + t, bx, by + t, colors::WHITE);
            sdl::render_line(target, bx, by + t, cx, cy + t, colors::WHITE);
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
