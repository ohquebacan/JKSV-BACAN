#include "appstates/GoogleDriveGuideState.hpp"

#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "remote/remote.hpp"
#include "sdl.hpp"

namespace
{
    // Pre-wrapped so each line fits the screen with NO_WRAP (deterministic layout).
    constexpr const char *GUIDE_LINES[] = {
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
    for (const char *line : GUIDE_LINES)
    {
        sdl::text::render(sdl::Texture::Null, START_X, y, BODY_SIZE, sdl::text::NO_WRAP, colors::WHITE, line);
        y += LINE_HEIGHT;
    }

    // Status line: green if the credential file is present, red if it's missing.
    y += 12;
    const char *status =
        m_hasConfig ? "Estado: client_secret.json DETECTADO." : "Estado: client_secret.json NO encontrado.";
    sdl::text::render(sdl::Texture::Null,
                      START_X,
                      y,
                      STATUS_SIZE,
                      sdl::text::NO_WRAP,
                      m_hasConfig ? colors::GREEN : colors::RED,
                      status);

    // Back hint.
    y += 48;
    sdl::text::render(sdl::Texture::Null, START_X, y, 20, sdl::text::NO_WRAP, colors::WHITE, "Pulsa B para volver.");
}
