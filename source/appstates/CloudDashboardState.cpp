#include "appstates/CloudDashboardState.hpp"

#include "config/config.hpp"
#include "data/data.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view DASHBOARD_TARGET = "CloudDashboardTarget";

    // Extract the YYYY-MM-DD_HH-MM-SS stamp (last 19 chars before optional .zip) from a backup name.
    std::string date_key(std::string_view name)
    {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".zip") { name = name.substr(0, name.size() - 4); }
        constexpr size_t stamp = 19;
        return name.size() >= stamp ? std::string(name.substr(name.size() - stamp)) : std::string{};
    }

    // "2026-06-15_10-00-00" -> "06-15 10:00"; empty key -> "--".
    std::string pretty_date(const std::string &key)
    {
        if (key.size() < 16) { return strings::tr("none", "ninguno"); }
        // YYYY-MM-DD_HH-MM-SS -> dd/mm/aaaa · hh:mm
        return key.substr(8, 2) + "/" + key.substr(5, 2) + "/" + key.substr(0, 4) + "  ·  " + key.substr(11, 2) + ":" + key.substr(14, 2);
    }

    // Newest local backup date for a game's working-directory folder.
    std::string local_latest(const data::TitleInfo *titleInfo)
    {
        const fslib::Path dir{config::get_working_directory() / titleInfo->get_path_safe_title()};
        // Guard: never open a Directory on a path that doesn't exist (a favorite with no backups froze here).
        if (!fslib::directory_exists(dir)) { return {}; }

        fslib::Directory listing{dir, false};
        std::string latest;
        if (listing.is_open())
        {
            const int64_t count = listing.get_count();
            for (int64_t i = 0; i < count; i++)
            {
                const std::string key = date_key(listing[i].get_filename());
                if (key > latest) { latest = key; }
            }
        }
        return latest;
    }

    // Newest cloud backup date for a game's remote folder.
    std::string cloud_latest(remote::Storage *remote, const data::TitleInfo *titleInfo)
    {
        if (!remote) { return {}; }

        remote->return_to_root();
        const std::string_view remoteTitle =
            remote->supports_utf8() ? titleInfo->get_title() : titleInfo->get_path_safe_title();
        if (!remote->directory_exists(remoteTitle)) { return {}; }

        remote::Item *folder = remote->get_directory_by_name(remoteTitle);
        if (!folder) { return {}; }

        remote->change_directory(folder);
        remote::Storage::DirectoryListing files;
        remote->get_directory_listing(files);

        std::string latest;
        for (remote::Item *file : files)
        {
            if (file->is_directory()) { continue; }
            const std::string key = date_key(file->get_name());
            if (key > latest) { latest = key; }
        }

        remote->return_to_root();
        return latest;
    }
} // namespace

//                      ---- Construction ----

CloudDashboardState::CloudDashboardState()
    : m_renderTarget(sdl::TextureManager::load(DASHBOARD_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(strings::tr("\uE0E1 Back", "\uE0E1 Atrás")))
{
    m_menu = ui::Menu::create(16, 10, 1040, 23, 555);
    CloudDashboardState::build_rows();
}

//                      ---- Public functions ----

void CloudDashboardState::update()
{
    const bool hasFocus = BaseState::has_focus();

    m_menu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    if (input::button_pressed(HidNpadButton_B)) { BaseState::deactivate(); }
}

void CloudDashboardState::sub_update() { m_controlGuide->sub_update(); }

void CloudDashboardState::render()
{
    const bool hasFocus = BaseState::has_focus();

    ui::lista::marco(strings::tr("Favorites in the cloud", "Favoritos en la nube"));
    if (m_filas.empty())
    {
        // Sin favoritos: una tarjeta que explica como marcarlos.
        sdl::render_rect_fill(sdl::Texture::Null, 340, 250, 740, 170, colors::DIALOG_DARK);
        sdl::text::render(sdl::Texture::Null, 380, 285, 26, 660, colors::WHITE, strings::tr("You have no favorite games yet", "Aún no tienes juegos favoritos"));
        sdl::text::render(sdl::Texture::Null, 380, 335, 19, 660, colors::DIALOG_LIGHT,
                          strings::tr("In the game grid, press Y on a game to mark it with the heart. Your favorites show up here with their cloud status.",
                                      "En la cuadrícula de juegos, pulsa Y sobre uno para marcarlo con el corazón. Aquí verás tus favoritos y su estado en la nube."));
    }
    else { ui::lista::dibujar(m_filas, m_menu->get_selected(), m_desplazamiento, hasFocus); }
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

void CloudDashboardState::build_rows()
{
    // NOTE: do NOT reload() here. This runs on the main/render thread, and a synchronous network call freezes
    // the whole app. Use the already-loaded in-memory listing (refreshed at startup and when opening games).
    remote::Storage *remote = remote::get_remote_storage();

    std::set<uint64_t> seen;
    int favorites = 0;

    data::UserList userList;
    data::get_users(userList);
    for (data::User *user : userList)
    {
        if (user->get_account_save_type() == FsSaveDataType_System) { continue; }

        const int total = static_cast<int>(user->get_total_data_entries());
        for (int i = 0; i < total; i++)
        {
            const uint64_t applicationID = user->get_application_id_at(i);
            if (!config::is_favorite(applicationID)) { continue; }
            if (!seen.insert(applicationID).second) { continue; }

            data::TitleInfo *titleInfo = data::get_title_info_by_id(applicationID);
            if (!titleInfo) { continue; }

            const std::string localKey = local_latest(titleInfo);
            const std::string cloudKey = cloud_latest(remote, titleInfo);

            // Rediseno: una fila por juego, agrupada por lo que hay que hacer.
            ui::lista::Fila f{};
            int orden{};
            if (!localKey.empty() && !cloudKey.empty() && localKey == cloudKey)
            {
                f.seccion     = strings::tr("UP TO DATE", "AL DÍA");
                f.valor       = strings::tr("Up to date", "Al día");
                f.valorActivo = true;
                orden         = 2;
            }
            else if (!localKey.empty() && (cloudKey.empty() || localKey > cloudKey))
            {
                f.seccion = strings::tr("TO UPLOAD", "PARA SUBIR");
                f.valor   = strings::tr("Upload", "Subir");
                orden     = 0;
            }
            else if (!cloudKey.empty())
            {
                f.seccion = strings::tr("TO DOWNLOAD", "PARA BAJAR");
                f.valor   = strings::tr("Download", "Bajar");
                orden     = 1;
            }
            else
            {
                f.seccion = strings::tr("NO BACKUPS", "SIN RESPALDOS");
                f.valor   = strings::tr("None", "Ninguno");
                orden     = 3;
            }
            f.titulo      = titleInfo->get_title();
            f.descripcion = strings::tr("Newest on the console: ", "Lo más nuevo en la consola: ") + pretty_date(localKey) + "\n" +
                            strings::tr("Newest in the cloud: ", "Lo más nuevo en la nube: ") + pretty_date(cloudKey);
            // Se inserta en su grupo, manteniendo el orden subir, bajar, al dia, sin respaldos.
            auto donde = std::find_if(m_filas.begin(), m_filas.end(), [&](const ui::lista::Fila &x) {
                const int o = x.seccion == strings::tr("TO UPLOAD", "PARA SUBIR") ? 0 : x.seccion == strings::tr("TO DOWNLOAD", "PARA BAJAR") ? 1 : x.seccion == strings::tr("UP TO DATE", "AL DÍA") ? 2 : 3;
                return o > orden;
            });
            m_filas.insert(donde, std::move(f));
            ++favorites;
        }
    }

    // El menu solo maneja la seleccion: una opcion por fila, en el mismo orden.
    for (const ui::lista::Fila &f : m_filas) { m_menu->add_option(f.titulo); }
    (void)favorites;
}
