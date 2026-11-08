#include "appstates/GoogleDriveGuideState.hpp"

#include <string>

#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "ui/ListaSecciones.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "remote/remote.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"

namespace
{
    // Pre-wrapped so each line fits the screen with NO_WRAP (deterministic layout).
} // namespace

//                      ---- Construction ----

GoogleDriveGuideState::GoogleDriveGuideState() : m_hasConfig(fslib::file_exists(remote::PATH_GOOGLE_DRIVE_CONFIG)) {}

//                      ---- Public functions ----

void GoogleDriveGuideState::update()
{
    if (input::button_pressed(HidNpadButton_B) || input::button_pressed(HidNpadButton_A)) { BaseState::deactivate(); }
}

void GoogleDriveGuideState::render()
{
    // Rediseno: marco comun, estado del archivo arriba a la derecha y los pasos numerados en tarjetas.
    ui::lista::marco("Google Drive",
                     m_hasConfig ? strings::tr("File found", "Archivo encontrado") : strings::tr("File missing", "Falta el archivo"),
                     m_hasConfig);

    sdl::text::render(sdl::Texture::Null, 176, 116, 19, 1060, colors::DIALOG_LIGHT,
                      strings::tr("Google Drive needs a credentials file that only you can create (Google requires it). It's done once, on a computer:",
                                  "Google Drive necesita un archivo de credenciales que solo tú puedes crear (Google lo exige). Se hace una vez, en una computadora:"));

    struct Paso
    {
        const char *en, *es;
    };
    static constexpr Paso PASOS[] = {
        {"At console.cloud.google.com, create a project.", "En console.cloud.google.com, crea un proyecto."},
        {"Enable the Google Drive API.", "Habilita la API de Google Drive."},
        {"On the consent screen, add yourself as a test user.", "En la pantalla de consentimiento, agrégate como usuario de prueba."},
        {"Create OAuth credentials of type \"TVs and Limited Input devices\".", "Crea credenciales OAuth de tipo \"TV y dispositivos de entrada limitada\"."},
        {"Download the JSON, rename it client_secret.json and put it in sd:/config/JKSV/",
         "Descarga el JSON, renómbralo client_secret.json y ponlo en sd:/config/JKSV/"},
    };
    int y = 172;
    for (int i = 0; i < 5; i++)
    {
        sdl::render_rect_fill(sdl::Texture::Null, 176, y, 1068, 64, colors::DIALOG_DARK);
        sdl::render_rect_fill(sdl::Texture::Null, 176, y, 5, 64, colors::BLUE_GREEN);
        const std::string numero = std::to_string(i + 1);
        sdl::text::render(sdl::Texture::Null, 204, y + 18, 26, sdl::text::NO_WRAP, colors::BLUE_GREEN, numero);
        sdl::text::render(sdl::Texture::Null, 248, y + 21, 20, 980, colors::WHITE, strings::tr(PASOS[i].en, PASOS[i].es));
        y += 76;
    }
    sdl::text::render(sdl::Texture::Null, 176, y + 10, 19, 1060, colors::DIALOG_LIGHT,
                      strings::tr("Then restart JKSV and sign in with the code it shows at google.com/device.",
                                  "Luego reinicia JKSV e inicia sesión con el código que muestra en google.com/device."));
    sdl::text::render(sdl::Texture::Null, 1060, 672, 20, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, strings::tr("\uE0E1 Back", "\uE0E1 Atrás"));
}
