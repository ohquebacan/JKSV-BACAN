#include "appstates/TitleSelectState.hpp"

#include <vector>
#include "ui/ListaSecciones.hpp"
#include "fslib.hpp"
#include "remote/remote.hpp"
#include "stringutil.hpp"
#include "data/data.hpp"
#include "partidas.hpp"

#include "StateManager.hpp"
#include "appstates/BackupMenuState.hpp"
#include "appstates/MainMenuState.hpp"
#include "appstates/TitleOptionState.hpp"
#include "config/config.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"

#include <string_view>

namespace
{
    // All of these states share the same render target.
} // namespace

//                      ---- Construction ----

TitleSelectState::TitleSelectState(data::User *user)
    : TitleSelectCommon()
    , m_user(user)
    , m_renderTarget(sdl::TextureManager::load("titleGridTarget2", 1140, 440, SDL_TEXTUREACCESS_TARGET))
    , m_titleView(ui::TitleView::create(m_user)) {};


//                      ---- Public functions ----

void TitleSelectState::update()
{
    if (!TitleSelectState::title_count_check()) { return; }

    // We just came back from a menu (backup/options) that may have added or removed cloud backups: re-mark.
    if (m_refreshPending)
    {
        m_titleView->refresh();
        m_refreshPending = false;
    }

    const bool hasFocus = BaseState::has_focus();
    const bool aPressed = input::button_pressed(HidNpadButton_A);
    const bool bPressed = input::button_pressed(HidNpadButton_B);
    const bool xPressed = input::button_pressed(HidNpadButton_X);
    const bool yPressed = input::button_pressed(HidNpadButton_Y);

    const bool zlPressed = input::button_pressed(HidNpadButton_ZL);
    const bool zrPressed = input::button_pressed(HidNpadButton_ZR);
    if (zlPressed || zrPressed)
    {
        // Rediseno: ZL/ZR cambian el filtro (todos, favoritos, con partida completa).
        const int filtro = (m_titleView->get_filtro() + (zrPressed ? 1 : 2)) % 3;
        m_titleView->set_filtro(filtro);
        m_cacheEntrada = -2;
    }
    else if (aPressed) { TitleSelectState::create_backup_menu(); }
    else if (xPressed) { TitleSelectState::create_title_option_menu(); }
    else if (yPressed) { TitleSelectState::add_remove_favorite(); }
    else if (bPressed)
    {
        TitleSelectState::deactivate_state();
        return;
    }

    m_titleView->update(hasFocus);
    sm_controlGuide->update(hasFocus);
}

void TitleSelectState::render()
{
    const bool hasFocus = BaseState::has_focus();

    m_renderTarget->clear(colors::TRANSPARENT);
    m_titleView->render(m_renderTarget, hasFocus);
    sm_controlGuide->render(sdl::Texture::Null, hasFocus);
    m_renderTarget->render(sdl::Texture::Null, 140, 97);

    // ---- Rediseno: pildoras de filtro en el encabezado ----
    const ui::TitleView::Cuentas &c = m_titleView->get_cuentas();
    {
        const int filtro = m_titleView->get_filtro();
        const std::string textos[3] = {
            stringutil::get_formatted_string(strings::tr("All · %d", "Todos · %d"), c.total),
            stringutil::get_formatted_string(strings::tr("Favorites · %d", "Favoritos · %d"), c.favoritos),
            stringutil::get_formatted_string(strings::tr("Full save · %d", "Con partida completa · %d"), c.conPartida),
        };
        static constexpr sdl::Color DORADO = {0xE6B95CFF};
        int x = 1244;
        for (int i = 2; i >= 0; i--)
        {
            const int w = sdl::text::get_width(17, textos[i]) + 32;
            x -= w;
            const bool activo = i == filtro;
            ui::lista::pildora(sdl::Texture::Null, x, 32, w, 34, activo ? (i == 2 ? DORADO : colors::BLUE_GREEN) : colors::DIALOG_DARK);
            sdl::text::render(sdl::Texture::Null, x + 16, 37, 17, sdl::text::NO_WRAP,
                              activo ? colors::CLEAR_COLOR : (i == 2 ? DORADO : colors::DIALOG_LIGHT), textos[i]);
            x -= 10;
        }
        sdl::text::render(sdl::Texture::Null, x - 70, 42, 15, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, "ZL/ZR");
    }

    // ---- Franja de abajo: el juego elegido y las cuentas ----
    sdl::render_line(sdl::Texture::Null, 164, 545, 1244, 545, colors::DIV_COLOR);
    const int entrada = m_titleView->get_selected_entry();
    if (entrada < 0)
    {
        sdl::text::render(sdl::Texture::Null, 176, 566, 20, sdl::text::NO_WRAP, colors::DIALOG_LIGHT,
                          strings::tr("Nothing in this filter. ZL/ZR to change it.", "No hay juegos en este filtro. ZL/ZR para cambiarlo."));
    }
    else if (data::TitleInfo *info = data::get_title_info_by_id(m_user->get_application_id_at(entrada)))
    {
        // Respaldos del elegido: se lee su carpeta una vez por seleccion.
        if (m_cacheEntrada != entrada)
        {
            m_cacheEntrada = entrada;
            m_cacheRespaldos = 0;
            m_cacheMasNuevo.clear();
            fslib::Directory carpeta{config::get_working_directory() / info->get_path_safe_title(), false};
            for (int i = 0; carpeta.is_open() && i < static_cast<int>(carpeta.get_count()); i++)
            {
                ++m_cacheRespaldos;
                std::string n{carpeta[i].get_filename()};
                if (n.size() > 4 && n.substr(n.size() - 4) == ".zip") { n.resize(n.size() - 4); }
                if (n.size() >= 19 && n[n.size() - 15] == '-' && n[n.size() - 9] == '_')
                {
                    const std::string sello = n.substr(n.size() - 19);
                    if (sello > m_cacheMasNuevo) { m_cacheMasNuevo = sello; }
                }
            }
        }
        const std::vector<std::string> nombre = ui::lista::partir(info->get_title(), 21, 1060, 1);
        if (!nombre.empty()) { sdl::text::render(sdl::Texture::Null, 176, 558, 21, sdl::text::NO_WRAP, colors::WHITE, nombre[0]); }
        std::string detalle = m_cacheRespaldos == 0 ? std::string{strings::tr("No backups on the console", "Sin respaldos en la consola")}
                                                    : stringutil::get_formatted_string(strings::tr("%d backups", "%d respaldos"), m_cacheRespaldos);
        if (m_cacheMasNuevo.size() == 19)
        {
            detalle += stringutil::get_formatted_string(strings::tr("  ·  newest %s/%s/%s %s:%s", "  ·  el más nuevo: %s/%s/%s %s:%s"),
                                                        m_cacheMasNuevo.substr(8, 2).c_str(), m_cacheMasNuevo.substr(5, 2).c_str(),
                                                        m_cacheMasNuevo.substr(0, 4).c_str(), m_cacheMasNuevo.substr(11, 2).c_str(),
                                                        m_cacheMasNuevo.substr(14, 2).c_str());
        }
        if (partidas::hay_para(m_user->get_application_id_at(entrada))) { detalle += strings::tr("  ·  $full save available$", "  ·  $hay partida completa$"); }
        sdl::text::render(sdl::Texture::Null, 176, 592, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, detalle);
    }
    {
        const bool nube = remote::get_remote_storage() != nullptr;
        const std::string cuentas = stringutil::get_formatted_string(
            nube ? strings::tr("%d with backup  ·  %d in the cloud  ·  #cloud connected#", "%d con respaldo  ·  %d en la nube  ·  #nube conectada#")
                 : strings::tr("%d with backup  ·  cloud not set up", "%d con respaldo  ·  nube sin configurar"),
            c.conRespaldo, c.enNube);
        const int w = sdl::text::get_width(16, cuentas);
        sdl::text::render(sdl::Texture::Null, 1244 - w, 594, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, cuentas);
    }
}

void TitleSelectState::sub_update()
{
    TitleSelectCommon::sub_update(); // keep the base behavior (control guide sub-update).
    m_refreshPending = true;
}

void TitleSelectState::refresh() { m_titleView->refresh(); }

//                      ---- Private functions ----

bool TitleSelectState::title_count_check()
{
    const int titleCount = m_user->get_total_data_entries();

    if (titleCount <= 0)
    {
        BaseState::deactivate();
        return false;
    }
    return true;
}

void TitleSelectState::create_backup_menu()
{
    const int selected             = m_titleView->get_selected_entry();
    if (selected < 0) { return; }
    const uint64_t applicationID   = m_user->get_application_id_at(selected);
    data::TitleInfo *titleInfo     = data::get_title_info_by_id(applicationID);
    const FsSaveDataInfo *saveInfo = m_user->get_save_info_at(selected);

    auto backupMenu = std::make_shared<BackupMenuState>(m_user, titleInfo, saveInfo);
    StateManager::push_state(backupMenu);
    m_titleView->play_sound();
}

void TitleSelectState::create_title_option_menu()
{
    const int selected             = m_titleView->get_selected_entry();
    if (selected < 0) { return; }
    const uint64_t applicationID   = m_user->get_application_id_at(selected);
    data::TitleInfo *titleInfo     = data::get_title_info_by_id(applicationID);
    const FsSaveDataInfo *saveInfo = m_user->get_save_info_at(selected);

    TitleOptionState::create_and_push(m_user, titleInfo, saveInfo, this);
}

void TitleSelectState::deactivate_state()
{
    m_titleView->reset();
    BaseState::deactivate();
}

void TitleSelectState::add_remove_favorite()
{
    const int selected           = m_titleView->get_selected_entry();
    if (selected < 0) { return; }
    const uint64_t applicationID = m_user->get_application_id_at(selected);

    config::add_remove_favorite(applicationID);

    // This applies to all users.
    data::UserList userList;
    data::get_users(userList);
    for (data::User *user : userList) { user->sort_data(); }

    // This is dirty, but it's the only way to pull it off without rewriting tons of code.
    const int count = m_user->get_total_data_entries();
    int i{};
    for (i = 0; i < count; i++)
    {
        const uint64_t appIDAt = m_user->get_application_id_at(i);
        if (appIDAt == applicationID) { break; }
    }
    m_titleView->set_selected(i);

    MainMenuState::refresh_view_states();
}
