#include "appstates/CloudSetupState.hpp"

#include "appstates/GoogleDriveGuideState.hpp"
#include "appstates/WebDavFormState.hpp"
#include "config/config.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "json.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "ui/PopMessageManager.hpp"

#include <string>

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
        RETENTION
    };
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

    if (aPressed)
    {
        switch (m_menu->get_selected())
        {
            case WEBDAV_KOOFR:       CloudSetupState::configure_koofr(); break;
            case WEBDAV_NEXTCLOUD:   CloudSetupState::configure_nextcloud(); break;
            case WEBDAV_GENERIC:     CloudSetupState::configure_generic(); break;
            case GOOGLE_DRIVE_GUIDE: CloudSetupState::show_google_drive_guide(); break;
            case VIEW_CONFIG:        CloudSetupState::show_current_config(); break;
            case RETENTION:          CloudSetupState::cycle_retention(); break;
        }
    }
    else if (bPressed) { BaseState::deactivate(); }
}

void CloudSetupState::sub_update() { m_controlGuide->sub_update(); }

void CloudSetupState::render()
{
    const bool hasFocus = BaseState::has_focus();

    m_renderTarget->clear(colors::TRANSPARENT);
    m_menu->render(m_renderTarget, hasFocus);

    // This is a pushed overlay on top of the Extras/main menu, which keeps rendering behind us. Paint an opaque
    // full-screen background first so those menus don't bleed through and overlap our text.
    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, graphics::SCREEN_WIDTH, graphics::SCREEN_HEIGHT, colors::CLEAR_COLOR);

    m_renderTarget->render(sdl::Texture::Null, 201, 91);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void CloudSetupState::initialize_menu()
{
    if (!m_menu) { m_menu = ui::Menu::create(32, 10, 1000, 23, 555); }

    m_menu->add_option("WebDAV: Koofr");
    m_menu->add_option("WebDAV: Nextcloud");
    m_menu->add_option("WebDAV: Otro (generico)");
    m_menu->add_option("Google Drive (guia)");
    m_menu->add_option("Ver / editar configuracion actual");
    m_menu->add_option(""); // Retention row; filled by update_retention_label().
    CloudSetupState::update_retention_label();
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
    const uint8_t keep      = config::get_by_key(config::keys::BACKUP_RETENTION);
    const std::string label = keep == 0 ? "Auto-limpieza de copias: Off"
                                        : "Auto-limpieza de copias: mantener " + std::to_string(keep);
    m_menu->edit_option(RETENTION, label);
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
                                  ? "Google Drive configurado (client_secret.json presente)."
                                  : "No hay WebDAV configurado.";
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
