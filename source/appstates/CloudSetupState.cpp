#include "appstates/CloudSetupState.hpp"

#include "appstates/CloudDashboardState.hpp"
#include "appstates/ConfirmState.hpp"
#include "appstates/GoogleDriveGuideState.hpp"
#include "appstates/MainMenuState.hpp"
#include "appstates/WebDavFormState.hpp"
#include "config/config.hpp"
#include "data/data.hpp"
#include "fslib.hpp"
#include "tasks/backup.hpp"
#include "tasks/mainmenu.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "json.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "ui/ListaSecciones.hpp"
#include "ui/PopMessageManager.hpp"

#include <string>
#include <vector>

namespace
{
    /// @brief Render target name. Unique so it doesn't clash with the shared "SecondaryTarget".
    constexpr std::string_view CLOUD_TARGET = "CloudSetupTarget";

    /// @brief Menu option indexes.
    enum
    {
        WEBDAV_KOOFR,
        WEBDAV_NEXTCLOUD,
        WEBDAV_GENERIC,
        GOOGLE_DRIVE_GUIDE,
        VIEW_CONFIG,
        DASHBOARD,
        UPLOAD_FAVORITES,
        DOWNLOAD_FAVORITES,
        RESTORE_ALL,
        SMART_SYNC_TOGGLE,
        RETENTION
    };

    // Rediseno: orden en pantalla, por secciones, con titulo corto y para que sirve cada opcion.
    struct OpcionNube
    {
        int accion;
        int seccion;
        const char *en, *es, *descEn, *descEs;
    };
    constexpr OpcionNube ORDEN[] = {
        {WEBDAV_KOOFR, 0, "Koofr (recommended)", "Koofr (recomendado)",
         "Free 10 GB. Create an app password on koofr.net (Preferences > Password) and enter it here.",
         "10 GB gratis. Crea una contraseña de aplicación en koofr.net (Preferencias › Contraseña) y escríbela aquí."},
        {WEBDAV_NEXTCLOUD, 0, "Nextcloud", "Nextcloud", "Your own Nextcloud server. You'll need its address, your user and an app password.",
         "Tu propio servidor Nextcloud. Necesitas su dirección, tu usuario y una contraseña de aplicación."},
        {WEBDAV_GENERIC, 0, "Another WebDAV server", "Otro servidor WebDAV", "Any service with WebDAV: you fill in the address, folder, user and password.",
         "Cualquier servicio con WebDAV: llenas la dirección, la carpeta, el usuario y la contraseña."},
        {GOOGLE_DRIVE_GUIDE, 0, "Google Drive", "Google Drive", "Step-by-step guide: Google Drive needs a credentials file you create on the PC.",
         "Guía paso a paso: Google Drive necesita un archivo de credenciales que se crea en la computadora."},
        {VIEW_CONFIG, 1, "See or edit the connection", "Ver o editar la conexión", "Shows the address and folder in use, and lets you change them.",
         "Muestra la dirección y la carpeta que se usan, y deja cambiarlas."},
        {DASHBOARD, 2, "Status of your favorites", "Estado de tus favoritos", "For each favorite game: whether the newest backup is on the console or in the cloud.",
         "Por cada juego favorito: si el respaldo más nuevo está en la consola o en la nube."},
        {UPLOAD_FAVORITES, 2, "Upload favorites", "Subir favoritos", "Backs up every favorite game and uploads it to the cloud.",
         "Respalda cada juego favorito y lo sube a la nube."},
        {DOWNLOAD_FAVORITES, 2, "Download favorites", "Bajar favoritos", "Brings the newest cloud backup of each favorite. Your current save is backed up first.",
         "Trae el respaldo más nuevo de la nube de cada favorito. Antes se respalda tu partida actual."},
        {RESTORE_ALL, 3, "Restore everything from the cloud", "Restaurar todo desde la nube",
         "For a new or formatted console: restores the newest backup of every installed game, creating its save if needed.",
         "Para una consola nueva o formateada: restaura el respaldo más nuevo de cada juego instalado y crea su partida si hace falta."},
        {SMART_SYNC_TOGGLE, 3, "Protect the newer save", "Proteger la partida más nueva",
         "Before replacing a save with one from the cloud, checks which was played last and keeps the newer one.",
         "Antes de reemplazar una partida con la de la nube, revisa cuál se jugó al último y conserva la más nueva."},
        {RETENTION, 3, "Clean up old backups", "Limpiar respaldos viejos", "Keeps only the newest backups of each game and deletes the rest. Press A to change how many.",
         "Conserva solo los respaldos más nuevos de cada juego y borra el resto. Pulsa A para cambiar cuántos."},
    };
    constexpr int TOTAL_NUBE = sizeof(ORDEN) / sizeof(ORDEN[0]);
} // namespace

//                      ---- Construction ----

CloudSetupState::CloudSetupState()
    : m_renderTarget(sdl::TextureManager::load(CLOUD_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(strings::get_by_name(strings::names::CONTROL_GUIDES, 3)))
{
    CloudSetupState::initialize_menu();
}

//                      ---- Public functions ----

void CloudSetupState::update()
{
    const bool hasFocus = BaseState::has_focus();
    const bool aPressed = input::button_pressed(HidNpadButton_A);
    const bool bPressed = input::button_pressed(HidNpadButton_B);

    m_menu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    const int fila = m_menu->get_selected();
    if (aPressed && fila >= 0 && fila < TOTAL_NUBE)
    {
        switch (ORDEN[fila].accion)
        {
            case WEBDAV_KOOFR:       CloudSetupState::configure_koofr(); break;
            case WEBDAV_NEXTCLOUD:   CloudSetupState::configure_nextcloud(); break;
            case WEBDAV_GENERIC:     CloudSetupState::configure_generic(); break;
            case GOOGLE_DRIVE_GUIDE: CloudSetupState::show_google_drive_guide(); break;
            case VIEW_CONFIG:        CloudSetupState::show_current_config(); break;
            case DASHBOARD:          CloudDashboardState::create_and_push(); break;
            case UPLOAD_FAVORITES:   CloudSetupState::upload_favorites(); break;
            case DOWNLOAD_FAVORITES: CloudSetupState::download_favorites(); break;
            case RESTORE_ALL:        CloudSetupState::restore_all_from_cloud(); break;
            case SMART_SYNC_TOGGLE:  CloudSetupState::cycle_smart_sync(); break;
            case RETENTION:          CloudSetupState::cycle_retention(); break;
        }
    }
    else if (bPressed) { BaseState::deactivate(); }
}

void CloudSetupState::sub_update() { m_controlGuide->sub_update(); }

void CloudSetupState::render()
{
    const bool hasFocus = BaseState::has_focus();

    // Rediseno: misma estructura que Ajustes, con el estado de la conexion arriba a la derecha.
    const bool conectada = remote::get_remote_storage() != nullptr;
    ui::lista::marco(strings::tr("Cloud", "Nube"), conectada ? strings::tr("Connected", "Conectada") : strings::tr("Not set up", "Sin configurar"), conectada);

    std::vector<ui::lista::Fila> filas{};
    for (const OpcionNube &o : ORDEN)
    {
        ui::lista::Fila f{};
        f.seccion     = o.seccion == 0   ? strings::tr("CONNECT", "CONECTAR")
                        : o.seccion == 1 ? strings::tr("YOUR CONNECTION", "TU CONEXIÓN")
                        : o.seccion == 2 ? strings::tr("FAVORITES", "FAVORITOS")
                                         : strings::tr("ADVANCED", "AVANZADO");
        f.titulo      = strings::tr(o.en, o.es);
        f.descripcion = strings::tr(o.descEn, o.descEs);
        if (o.accion == VIEW_CONFIG)
        {
            f.valor       = conectada ? strings::tr("Connected", "Conectada") : strings::tr("Not set up", "Sin configurar");
            f.valorActivo = conectada;
        }
        else if (o.accion == SMART_SYNC_TOGGLE)
        {
            const bool on = config::get_by_key(config::keys::SMART_SYNC) != 0;
            f.valor       = on ? strings::tr("On", "Encendido") : strings::tr("Off", "Apagado");
            f.valorActivo = on;
        }
        else if (o.accion == RETENTION)
        {
            const uint8_t keep = config::get_by_key(config::keys::BACKUP_RETENTION);
            f.valor            = keep == 0 ? std::string{strings::tr("Off", "Apagado")} : strings::tr("Keep ", "Conservar ") + std::to_string(keep);
            f.valorActivo      = keep != 0;
        }
        else if (!conectada && (o.seccion == 2 || o.accion == RESTORE_ALL))
        {
            f.valor = strings::tr("Needs a connection", "Requiere conexión");
        }
        filas.push_back(std::move(f));
    }
    ui::lista::dibujar(filas, m_menu->get_selected(), m_desplazamiento, hasFocus);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void CloudSetupState::initialize_menu()
{
    if (!m_menu) { m_menu = ui::Menu::create(32, 10, 1000, 23, 555); }

    // Rediseno: el menu solo maneja la seleccion, en el orden de ORDEN; los textos los dibuja ui::lista.
    for (const OpcionNube &o : ORDEN) { m_menu->add_option(strings::tr(o.en, o.es)); }
    CloudSetupState::update_smart_label();
    CloudSetupState::update_retention_label();
}

void CloudSetupState::cycle_smart_sync()
{
    const uint8_t value = config::get_by_key(config::keys::SMART_SYNC) ? 0 : 1;
    config::set_by_key(config::keys::SMART_SYNC, value);
    config::save();
    CloudSetupState::update_smart_label();
}

void CloudSetupState::update_smart_label()
{
    // Rediseno: el valor lo dibuja ui::lista en render().
}

void CloudSetupState::upload_favorites()
{
    remote::Storage *remote = remote::get_remote_storage();
    if (!remote)
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, strings::tr("No cloud configured.", "No hay nube configurada."));
        return;
    }

    auto data = std::make_shared<MainMenuState::DataStruct>();
    data::get_users(data->userList);

    const char *query = strings::tr("Upload the current save of your favorite (heart) games to the cloud?", "Subir la partida actual de tus juegos favoritos (corazon) a la nube?");
    ConfirmProgress::create_push_fade(query, false, tasks::mainmenu::upload_favorites_remote, nullptr, data);
}

void CloudSetupState::download_favorites()
{
    remote::Storage *remote = remote::get_remote_storage();
    if (!remote)
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, strings::tr("No cloud configured.", "No hay nube configurada."));
        return;
    }

    auto data = std::make_shared<MainMenuState::DataStruct>();
    data::get_users(data->userList);

    // Hold-to-confirm: this OVERWRITES local saves (a PRE-SYNC local backup is made first for safety).
    const char *query = strings::tr("OVERWRITE local saves of your favorites with the cloud version? A local PRE-SYNC backup is made first. Skips if your local save is newer.", "SOBRESCRIBIR partidas locales de tus favoritos con la version de la nube? Se hace un backup local PRE-SYNC antes. Se salta si tu local es mas nuevo.");
    ConfirmProgress::create_push_fade(query, true, tasks::backup::download_favorites_remote, nullptr, data);
}

void CloudSetupState::restore_all_from_cloud()
{
    remote::Storage *remote = remote::get_remote_storage();
    if (!remote)
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, strings::tr("No cloud configured.", "No hay nube configurada."));
        return;
    }

    auto data = std::make_shared<MainMenuState::DataStruct>();

    // Hold-to-confirm: creates save containers for games never launched and OVERWRITES existing saves.
    const char *query = strings::tr("Restore your ENTIRE cloud on this console? Creates saves for installed games you never opened and OVERWRITES existing ones (with PRE-SYNC backup). Non-installed games are skipped.", "Restaurar TODA tu nube en esta consola? Crea las partidas de juegos instalados que nunca abriste y SOBRESCRIBE las existentes (con backup PRE-SYNC). Juegos no instalados se omiten.");
    ConfirmProgress::create_push_fade(query, true, tasks::backup::restore_all_from_cloud, nullptr, data);
}

void CloudSetupState::cycle_retention()
{
    uint8_t keep = config::get_by_key(config::keys::BACKUP_RETENTION);
    keep         = keep == 0 ? 5 : keep == 5 ? 10 : keep == 10 ? 20 : 0;
    config::set_by_key(config::keys::BACKUP_RETENTION, keep);
    config::save();
    CloudSetupState::update_retention_label();
}

void CloudSetupState::update_retention_label()
{
    // Rediseno: el valor lo dibuja ui::lista en render().
}

void CloudSetupState::configure_koofr()
{
    // Koofr's WebDAV root is /dav/Koofr/ ; the JKSV folder lives under it. Origin is host-only.
    WebDavFormState::create_and_push("https://app.koofr.net", "dav/Koofr/JKSV", "", "");
}

void CloudSetupState::configure_nextcloud()
{
    // Nextcloud exposes WebDAV at remote.php/dav/files/<user>/ ; edit the user/folder in the form.
    WebDavFormState::create_and_push("https://", "remote.php/dav/files/usuario/JKSV", "", "");
}

void CloudSetupState::configure_generic() { WebDavFormState::create_and_push("https://", "", "", ""); }

void CloudSetupState::show_google_drive_guide() { GoogleDriveGuideState::create_and_push(); }

void CloudSetupState::show_current_config()
{
    if (!fslib::file_exists(remote::PATH_WEBDAV_CONFIG))
    {
        const char *message = fslib::file_exists(remote::PATH_GOOGLE_DRIVE_CONFIG)
                                  ? strings::tr("Google Drive configured (client_secret.json present).", "Google Drive configurado (client_secret.json presente).")
                                  : strings::tr("No WebDAV configured.", "No hay WebDAV configurado.");
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, message);
        return;
    }

    // Load the current values and open the form so the user can see (and fix) them, password included.
    std::string origin{}, basepath{}, username{}, password{};

    json::Object config = json::new_object(json_object_from_file, remote::PATH_WEBDAV_CONFIG.data());
    if (config)
    {
        json_object *originObject   = json::get_object(config, "origin");
        json_object *basepathObject = json::get_object(config, "basepath");
        json_object *usernameObject = json::get_object(config, "username");
        json_object *passwordObject = json::get_object(config, "password");
        if (originObject) { origin = json_object_get_string(originObject); }
        if (basepathObject) { basepath = json_object_get_string(basepathObject); }
        if (usernameObject) { username = json_object_get_string(usernameObject); }
        if (passwordObject) { password = json_object_get_string(passwordObject); }
    }

    WebDavFormState::create_and_push(origin, basepath, username, password);
}
