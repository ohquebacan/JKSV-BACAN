#include "ui/TitleView.hpp"

#include "config/config.hpp"
#include "error.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "partidas.hpp"
#include "remote/remote.hpp"

#include <cmath>
#include <string>
#include <unordered_set>

namespace
{
    // Rediseno: 6 por fila, con el nombre debajo. Area: 1140 x 552.
    constexpr double UPPER_THRESHOLD = 22.0f;
    constexpr double LOWER_THRESHOLD = 236.0f; // dos filas a la vista, como el mockup
    constexpr int ICON_ROW_SIZE      = 6;
    constexpr int TILE_SPACE_HOR     = 176;
    constexpr int TILE_SPACE_VERT    = 214;
    constexpr int GRID_X             = 55;
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
    // El catalogo de partidas llega en segundo plano: se vuelve a marcar cuando carga.
    if (m_partidasVistas != partidas::generacion())
    {
        m_partidasVistas = partidas::generacion();
        TitleView::refresh();
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
    if (m_titleTiles.empty()) { return; }

    static sdl::SharedTexture ring = sdl::TextureManager::load("tileRing", "romfs:/Textures/TileRing.png");

    const int tileCount = m_titleTiles.size();
    const int y         = m_transition.get_y();
    for (int i = 0, tempY = y; i < tileCount; i += ICON_ROW_SIZE, tempY += TILE_SPACE_VERT)
    {
        if (tempY > 440 || tempY < -8)
        {
            // Fuera de la vista: solo se anota la posicion del elegido.
            for (int j = i; j < i + ICON_ROW_SIZE && j < tileCount; j++)
            {
                if (j == m_selected)
                {
                    m_selectedX = GRID_X + (j - i) * TILE_SPACE_HOR;
                    m_selectedY = tempY;
                }
            }
            continue;
        }
        const int endRow = i + ICON_ROW_SIZE;
        for (int j = i, tempX = GRID_X; j < endRow && j < tileCount; j++, tempX += TILE_SPACE_HOR)
        {
            if (j == m_selected)
            {
                m_selectedX = tempX;
                m_selectedY = tempY;
            }
            m_titleTiles[j].render(target, tempX, tempY);
        }
    }

    // Aro de seleccion del color del tema, en lugar del recuadro que parpadeaba.
    if (hasFocus && ring)
    {
        ring->set_color_mod(colors::BLUE_GREEN);
        ring->render(target, m_selectedX - 6, m_selectedY - 6);
    }
}

int ui::TitleView::get_selected() const noexcept { return m_selected; }

int ui::TitleView::get_selected_entry() const noexcept
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_titleTiles.size())) { return -1; }
    return m_titleTiles[m_selected].get_index();
}

void ui::TitleView::set_filtro(int filtro)
{
    if (filtro == m_filtro) { return; }
    m_filtro   = filtro;
    m_selected = 0;
    m_transition.set_y(UPPER_THRESHOLD);
    m_transition.set_target_y(UPPER_THRESHOLD);
    TitleView::refresh();
}

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

    // Las carpetas de respaldos que existen, leidas una vez: antes se preguntaba por la carpeta de cada juego,
    // y con muchos juegos eran cientos de consultas a la SD en cada refresco.
    std::unordered_set<std::string> carpetas{};
    {
        fslib::Directory trabajo{config::get_working_directory(), false};
        for (int c = 0; trabajo.is_open() && c < static_cast<int>(trabajo.get_count()); c++)
        {
            carpetas.insert(trabajo[c].get_filename());
        }
    }

    m_cuentas = Cuentas{};
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
        if (carpetas.count(titleInfo->get_path_safe_title()))
        {
            const fslib::Path localDir{config::get_working_directory() / titleInfo->get_path_safe_title()};
            {
                fslib::Directory localListing{localDir, false};
                hasLocalBackup = localListing.is_open() && localListing.get_count() > 0;
            }
        }

        const bool hasPartida = partidas::hay_para(applicationID);

        // Cuentas sobre todos; el filtro decide solo que casillas se muestran.
        ++m_cuentas.total;
        if (isFavorite) { ++m_cuentas.favoritos; }
        if (hasLocalBackup) { ++m_cuentas.conRespaldo; }
        if (hasCloudBackup) { ++m_cuentas.enNube; }
        if (hasPartida) { ++m_cuentas.conPartida; }
        if ((m_filtro == 1 && !isFavorite) || (m_filtro == 2 && !hasPartida)) { continue; }

        m_titleTiles.emplace_back(isFavorite, i, icon, hasCloudBackup, hasLocalBackup, hasPartida, std::string{titleInfo->get_title()});
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
