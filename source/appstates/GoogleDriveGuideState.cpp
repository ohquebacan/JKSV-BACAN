#include "appstates/GoogleDriveGuideState.hpp"

#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "remote/remote.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"

namespace
{
    // Pre-wrapped so each line fits the screen with NO_WRAP (deterministic layout).
    constexpr const char *GUIDE_LINES_EN[] = {
        "Google Drive needs OAuth credentials that only YOU",
        "can create (Google requires it). Once, on a PC:",
        "",
        "1. console.cloud.google.com  ->  create a project.",
        "2. Enable 'Google Drive API'.",
        "3. Consent screen: add yourself as a test user.",
        "",
        "4. Create OAuth credentials of type",
        "   'TVs and Limited Input devices'.",
        "5. Download the JSON, rename it to client_secret.json",
        "   and put it in  sd:/config/JKSV/",
        "",
        "Restart JKSV with that file and sign in with the",
        "code at  google.com/device.",
    };

    constexpr const char *GUIDE_LINES_ES[] = {
        "Google Drive necesita credenciales OAuth que solo TU",
        "puedes crear (Google lo exige). Una vez, en una PC:",
        "",
        "1. console.cloud.google.com  ->  crea un proyecto.",
        "2. Habilita 'Google Drive API'.",
        "3. Pantalla de consentimiento: agregate como usuario",
        "   de prueba.",
        "4. Crea credenciales OAuth tipo",
        "   'TV y dispositivos de entrada limitada'.",
        "5. Descarga el JSON, renombralo a client_secret.json",
        "   y ponlo en  sd:/config/JKSV/",
        "",
        "Reinicia JKSV con ese archivo e inicia sesion con el",
        "codigo en  google.com/device.",
    };

    constexpr int START_X      = 80;
    constexpr int START_Y      = 56;
    constexpr int LINE_HEIGHT  = 30;
    constexpr int BODY_SIZE    = 22;
    constexpr int STATUS_SIZE  = 24;
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
    // Opaque background so the menu behind doesn't bleed through.
    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, graphics::SCREEN_WIDTH, graphics::SCREEN_HEIGHT, colors::CLEAR_COLOR);

    int y = START_Y;
    const bool spanish = strings::is_spanish();
    const auto &lines  = spanish ? GUIDE_LINES_ES : GUIDE_LINES_EN;
    for (const char *line : lines)
    {
        sdl::text::render(sdl::Texture::Null, START_X, y, BODY_SIZE, sdl::text::NO_WRAP, colors::WHITE, line);
        y += LINE_HEIGHT;
    }

    // Status line: green if the credential file is present, red if it's missing.
    y += 12;
    const char *status =
        m_hasConfig ? strings::tr("Status: client_secret.json DETECTED.", "Estado: client_secret.json DETECTADO.")
                    : strings::tr("Status: client_secret.json NOT found.", "Estado: client_secret.json NO encontrado.");
    sdl::text::render(sdl::Texture::Null,
                      START_X,
                      y,
                      STATUS_SIZE,
                      sdl::text::NO_WRAP,
                      m_hasConfig ? colors::GREEN : colors::RED,
                      status);

    // Back hint.
    y += 48;
    sdl::text::render(sdl::Texture::Null, START_X, y, 20, sdl::text::NO_WRAP, colors::WHITE, strings::tr("Press B to go back.", "Pulsa B para volver."));
}
