#include "ui/TitleView.hpp"

#include "config/config.hpp"
#include "error.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "remote/remote.hpp"

#include <cmath>

namespace
{
    constexpr double UPPER_THRESHOLD = 32.0f;
    constexpr double LOWER_THRESHOLD = 388.0f;
    constexpr int ICON_ROW_SIZE      = 7;
}

//                      ---- Construction ----

ui::TitleView::TitleView(data::User *user)
    : m_user(user)
    , m_transition(0, UPPER_THRESHOLD, 0, 0, 0, UPPER_THRESHOLD, 0, 0, m_transition.DEFAULT_THRESHOLD)
    , m_bounding(ui::BoundingBox::create(0, 0, 188, 188))
{
    TitleView::initialize_static_members();
    TitleView::refresh();
}

void ui::TitleView::update(bool hasFocus)
{
    // The remote listing loads on a background thread after startup. Once it's ready, rebuild the tiles once
    // so the cloud badges show up (the grid was built before the listing existed).
    if (!m_cloudMarked && remote::get_remote_storage() != nullptr)
    {
        TitleView::refresh();
        m_cloudMarked = true;
    }

    if (m_titleTiles.empty()) { return; }

    m_bounding->update(hasFocus);
    TitleView::handle_input();
    TitleView::handle_scrolling();
    TitleView::update_tiles();

    m_transition.update();
}

void ui::TitleView::render(sdl::SharedTexture &target, bool hasFocus)
{
    static constexpr int TILE_SPACE_VERT = 144;
    static constexpr int TILE_SPACE_HOR  = 144;

    if (m_titleTiles.empty()) { return; }

    const int tileCount = m_titleTiles.size();
    const int y         = m_transition.get_y();
    for (int i = 0, tempY = y; i < tileCount; i += ICON_ROW_SIZE, tempY += TILE_SPACE_VERT)
    {
        const int endRow = i + ICON_ROW_SIZE;
        for (int j = i, tempX = 32; j < endRow && j < tileCount; j++, tempX += TILE_SPACE_HOR)
        {
            if (j == m_selected)
            {
                m_selectedX = tempX;
                m_selectedY = tempY;
                continue;
            }

            ui::TitleTile &tile = m_titleTiles[j];
            tile.render(target, tempX, tempY);
        }
    }

    if (hasFocus)
    {
        m_bounding->set_x(m_selectedX - 30);
        m_bounding->set_y(m_selectedY - 30);

        sdl::render_rect_fill(target, m_selectedX - 28, m_selectedY - 28, 184, 184, colors::CLEAR_COLOR);
        m_bounding->render(target, hasFocus);
    }

    ui::TitleTile &selectedTile = m_titleTiles[m_selected];
    selectedTile.render(target, m_selectedX, m_selectedY);
}

int ui::TitleView::get_selected() const noexcept { return m_selected; }

void ui::TitleView::set_selected(int selected) noexcept
{
    const int tilesCount = m_titleTiles.size();
    if (selected < 0 || selected >= tilesCount) { return; }
    m_selected = selected;
}

void ui::TitleView::refresh()
{
    m_titleTiles.clear();

    const int entryCount    = m_user->get_total_data_entries();
    remote::Storage *remote = remote::get_remote_storage();

    for (int i = 0; i < entryCount; i++)
    {
        const uint64_t applicationID = m_user->get_application_id_at(i);
        data::TitleInfo *titleInfo   = data::get_title_info_by_id(applicationID);
        if (error::is_null(titleInfo)) { continue; }

        const bool isFavorite   = config::is_favorite(applicationID);
        sdl::SharedTexture icon = titleInfo->get_icon(); // I don't like this but w/e.

        // Mark the tile if this game already has a backup on the remote storage.
        bool hasCloudBackup = false;
        if (remote)
        {
            const std::string_view folder =
                remote->supports_utf8() ? titleInfo->get_title() : titleInfo->get_path_safe_title();
            hasCloudBackup = remote->has_backups_for(folder);
        }

        // Mark the tile if this game has a non-empty local backup folder on the SD card. (The folder can exist
        // but be empty just from opening the game, so an entry count > 0 is what actually counts as a backup.)
        bool hasLocalBackup = false;
        {
            const fslib::Path localDir{config::get_working_directory() / titleInfo->get_path_safe_title()};
            if (fslib::directory_exists(localDir))
            {
                fslib::Directory localListing{localDir, false};
                hasLocalBackup = localListing.is_open() && localListing.get_count() > 0;
            }
        }

        m_titleTiles.emplace_back(isFavorite, i, icon, hasCloudBackup, hasLocalBackup);
    }

    const int tileCount = m_titleTiles.size() - 1;
    if (tileCount <= 0) { m_selected = 0; }
    else if (m_selected > tileCount) { m_selected = tileCount; }
}

void ui::TitleView::reset()
{
    for (ui::TitleTile &currentTile : m_titleTiles) { currentTile.reset(); }
}

void ui::TitleView::play_sound() noexcept { sm_cursor->play(); }

//                          ---- Private functions ----

void ui::TitleView::initialize_static_members()
{
    static constexpr std::string_view CURSOR_NAME = "MenuCursor";
    static constexpr const char *CURSOR_PATH      = "romfs:/Sound/MenuCursor.wav";

    if (sm_cursor) { return; }

    sm_cursor = sdl::SoundManager::load(CURSOR_NAME, CURSOR_PATH);
}

void ui::TitleView::handle_input()
{
    const int totalTiles        = m_titleTiles.size() - 1;
    const bool upPressed        = input::button_pressed(HidNpadButton_AnyUp);
    const bool downPressed      = input::button_pressed(HidNpadButton_AnyDown);
    const bool leftPressed      = input::button_pressed(HidNpadButton_AnyLeft);
    const bool rightPressed     = input::button_pressed(HidNpadButton_AnyRight);
    const bool lShoulderPressed = input::button_pressed(HidNpadButton_L);
    const bool rShoulderPressed = input::button_pressed(HidNpadButton_R);

    const int previousSelected = m_selected;

    if (upPressed) { m_selected -= ICON_ROW_SIZE; }
    else if (leftPressed) { --m_selected; }
    else if (lShoulderPressed) { m_selected -= ICON_ROW_SIZE * 3; }
    else if (downPressed) { m_selected += ICON_ROW_SIZE; }
    else if (rightPressed) { ++m_selected; }
    else if (rShoulderPressed) { m_selected += ICON_ROW_SIZE * 3; }

    if (m_selected < 0) { m_selected = 0; }
    if (m_selected > totalTiles) { m_selected = totalTiles; }

    if (m_selected != previousSelected) { sm_cursor->play(); }
}

void ui::TitleView::handle_scrolling()
{
    double shift{};
    bool changed{};
    if (m_selectedY < static_cast<int>(UPPER_THRESHOLD))
    {
        shift   = (UPPER_THRESHOLD - m_selectedY);
        changed = true;
    }
    else if (m_selectedY > static_cast<int>(LOWER_THRESHOLD))
    {
        shift   = (LOWER_THRESHOLD - m_selectedY);
        changed = true;
    }

    if (changed)
    {
        const int shiftedY = m_transition.get_y() + std::round(shift);
        m_transition.set_target_y(shiftedY);
    }
}

void ui::TitleView::update_tiles()
{
    for (ui::TitleTile &tile : m_titleTiles) { tile.update(m_selected); }
}
