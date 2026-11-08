#include "appstates/WebDavFormState.hpp"

#include <vector>

#include "error.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "ui/ListaSecciones.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "json.hpp"
#include "keyboard/keyboard.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "sys/sys.hpp"
#include "ui/PopMessageManager.hpp"

#include <array>
#include <string>

namespace
{
    constexpr std::string_view FORM_TARGET = "WebDavFormTarget";
    constexpr std::string_view CONFIG_DIRECTORY = "sdmc:/config/JKSV";
    constexpr size_t FIELD_SIZE = 512;

    /// @brief Menu row indexes.
    enum
    {
        FIELD_ORIGIN,
        FIELD_BASEPATH,
        FIELD_USERNAME,
        FIELD_PASSWORD,
        TOGGLE_PASSWORD,
        SAVE
    };

    std::string mask(std::string_view value) { return std::string(value.length(), '*'); }
} // namespace

//                      ---- Construction ----

WebDavFormState::WebDavFormState(std::string_view origin,
                                 std::string_view basepath,
                                 std::string_view username,
                                 std::string_view password)
    : m_renderTarget(sdl::TextureManager::load(FORM_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(strings::tr("\uE0E0 Edit   \uE0E1 Back", "\uE0E0 Editar   \uE0E1 Atrás")))
    , m_origin(origin)
    , m_basepath(basepath)
    , m_username(username)
    , m_password(password)
{
    m_menu = ui::Menu::create(32, 10, 1000, 23, 555);

    // Add the rows once; update_options() fills their text.
    for (int i = 0; i < 6; i++) { m_menu->add_option(""); }
    WebDavFormState::update_options();
}

//                      ---- Public functions ----

void WebDavFormState::update()
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
            case FIELD_ORIGIN:
            case FIELD_BASEPATH:
            case FIELD_USERNAME:
            case FIELD_PASSWORD:   WebDavFormState::edit_field(m_menu->get_selected()); break;
            case TOGGLE_PASSWORD:  m_showPassword = !m_showPassword; WebDavFormState::update_options(); break;
            case SAVE:             WebDavFormState::save_and_connect(); break;
        }
    }
    else if (bPressed) { BaseState::deactivate(); }
}

void WebDavFormState::sub_update() { m_controlGuide->sub_update(); }

void WebDavFormState::render()
{
    const bool hasFocus = BaseState::has_focus();

    // Rediseno: secciones con el valor de cada campo a la derecha y la ayuda en el panel.
    ui::lista::marco(strings::tr("WebDAV connection", "Conexión WebDAV"));

    const char *servidor = strings::tr("SERVER", "SERVIDOR");
    const char *cuenta   = strings::tr("ACCOUNT", "CUENTA");
    const char *listo    = strings::tr("DONE", "LISTO");
    std::vector<ui::lista::Fila> filas(6);
    filas[FIELD_ORIGIN]   = {servidor, strings::tr("Server address", "Dirección del servidor"), m_origin, !m_origin.empty(),
                             strings::tr("Only https:// and the server name, with no folder. For Koofr: https://app.koofr.net",
                                         "Solo https:// y el nombre del servidor, sin carpeta. Para Koofr: https://app.koofr.net")};
    filas[FIELD_BASEPATH] = {servidor, strings::tr("Folder", "Carpeta"), m_basepath, !m_basepath.empty(),
                             strings::tr("Where your backups go, without a leading /. For Koofr: dav/Koofr/JKSV",
                                         "Dónde van tus respaldos, sin / al inicio. Para Koofr: dav/Koofr/JKSV")};
    filas[FIELD_USERNAME] = {cuenta, strings::tr("User", "Usuario"), m_username, !m_username.empty(),
                             strings::tr("The e-mail or user you sign in with.", "El correo o usuario con el que entras.")};
    filas[FIELD_PASSWORD] = {cuenta, strings::tr("App password", "Contraseña de aplicación"),
                             m_password.empty() ? std::string{strings::tr("Empty", "Vacía")} : (m_showPassword ? m_password : mask(m_password)),
                             !m_password.empty(),
                             strings::tr("Use an app password, not your account password. In Koofr: Preferences > Password > App passwords.",
                                         "Usa una contraseña de aplicación, no la de tu cuenta. En Koofr: Preferencias › Contraseña › Contraseñas de aplicación.")};
    filas[TOGGLE_PASSWORD] = {cuenta, strings::tr("Show password", "Mostrar contraseña"),
                              m_showPassword ? strings::tr("Visible", "Visible") : strings::tr("Hidden", "Oculta"), m_showPassword,
                              strings::tr("Shows or hides the password on this screen.", "Muestra u oculta la contraseña en esta pantalla.")};
    filas[SAVE]           = {listo, strings::tr("Save and connect", "Guardar y conectar"), "", false,
                             strings::tr("Saves the connection and checks it right away. Your backups go to that folder.",
                                         "Guarda la conexión y la prueba enseguida. Tus respaldos irán a esa carpeta.")};
    ui::lista::dibujar(filas, m_menu->get_selected(), m_desplazamiento, hasFocus);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void WebDavFormState::update_options()
{
    m_menu->edit_option(FIELD_ORIGIN, std::string("Origin:      ") + m_origin);
    m_menu->edit_option(FIELD_BASEPATH, std::string("Basepath:    ") + m_basepath);
    m_menu->edit_option(FIELD_USERNAME, strings::tr("User:        ", "Usuario:     ") + m_username);
    m_menu->edit_option(FIELD_PASSWORD, strings::tr("Password:    ", "Contrasena:  ") + (m_showPassword ? m_password : mask(m_password)));
    m_menu->edit_option(TOGGLE_PASSWORD, m_showPassword ? strings::tr("[Hide password]", "[Ocultar contrasena]")
                                                        : strings::tr("[Show password]", "[Mostrar contrasena]"));
    m_menu->edit_option(SAVE, strings::tr("Save and connect", "Guardar y conectar"));
}

void WebDavFormState::edit_field(int field)
{
    std::string *target = nullptr;
    const char *header  = nullptr;

    switch (field)
    {
        case FIELD_ORIGIN:   target = &m_origin;   header = strings::tr("Origin (only https://host, no path)", "Origin (solo https://host, sin ruta)"); break;
        case FIELD_BASEPATH: target = &m_basepath; header = strings::tr("Basepath (full path, no leading /)", "Basepath (ruta completa, sin / inicial)"); break;
        case FIELD_USERNAME: target = &m_username; header = strings::tr("User", "Usuario"); break;
        case FIELD_PASSWORD: target = &m_password; header = strings::tr("Password", "Contrasena"); break;
        default:             return;
    }

    std::array<char, FIELD_SIZE> buffer = {0};
    if (keyboard::get_input(SwkbdType_Normal, target->c_str(), header, buffer.data(), FIELD_SIZE))
    {
        *target = buffer.data();
        WebDavFormState::update_options();
    }
}

void WebDavFormState::save_and_connect()
{
    const int popTicks = ui::PopMessageManager::DEFAULT_TICKS;

    // Make sure the config directory exists.
    if (!fslib::directory_exists(CONFIG_DIRECTORY)) { error::fslib(fslib::create_directory(CONFIG_DIRECTORY)); }

    // Build the JSON with json-c so the values are escaped correctly.
    json::Object root = json::new_object(json_object_new_object);
    json::add_object(root, "origin", json_object_new_string(m_origin.c_str()));
    json::add_object(root, "basepath", json_object_new_string(m_basepath.c_str()));
    json::add_object(root, "username", json_object_new_string(m_username.c_str()));
    json::add_object(root, "password", json_object_new_string(m_password.c_str()));

    const std::string jsonString = json_object_get_string(root.get());

    // Remove the old file first so a shorter write leaves no stale bytes.
    if (fslib::file_exists(remote::PATH_WEBDAV_CONFIG)) { error::fslib(fslib::delete_file(remote::PATH_WEBDAV_CONFIG)); }

    bool wrote = false;
    {
        fslib::File configFile{remote::PATH_WEBDAV_CONFIG,
                               FsOpenMode_Create | FsOpenMode_Write,
                               static_cast<int64_t>(jsonString.length())};
        if (configFile.is_open())
        {
            configFile << jsonString;
            wrote = true;
        }
    }

    if (!wrote)
    {
        ui::PopMessageManager::push_message(popTicks, strings::tr("Error saving webdav.json", "Error guardando webdav.json"));
        return;
    }

    ui::PopMessageManager::push_message(popTicks, strings::tr("WebDAV saved. Connecting...", "WebDAV guardado. Conectando..."));

    // Same entry point used at startup; re-reads the config and rebuilds the storage handle.
    sys::threadpool::push_job(remote::initialize, nullptr);

    // Close the form.
    BaseState::deactivate();
}
