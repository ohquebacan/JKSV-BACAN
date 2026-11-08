#include "appstates/UpdateNoticeState.hpp"

#include "StateManager.hpp"
#include "appstates/FadeState.hpp"
#include "appstates/ProgressState.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "strings/strings.hpp"
#include "sys/sys.hpp"
#include "tasks/update.hpp"
#include "ui/ListaSecciones.hpp"

#include <vector>

namespace
{
    // Tarjeta centrada.
    constexpr int CW = 760, CH = 384;
    constexpr int CX = (1280 - CW) / 2, CY = (720 - CH) / 2;
    constexpr int PAD = 44;
    constexpr int ANCHO = CW - PAD * 2;

    // Botones.
    constexpr int BOTON_H = 52, BOTON_Y = CY + CH - PAD - BOTON_H, BOTON_GAP = 16;
    constexpr int BOTON_W = (ANCHO - BOTON_GAP) / 2;
}

UpdateNoticeState::UpdateNoticeState()
    : m_caja(ui::DialogBox::create(CX, CY, CW, CH))
    , m_cursor(sdl::SoundManager::load("MenuCursor", "romfs:/Sound/MenuCursor.wav"))
    , m_pop(sdl::SoundManager::load("ConfirmPop", "romfs:/Sound/ConfirmPop.wav"))
{
    const std::string nueva = tasks::update::version_nueva();
    m_texto = strings::tr("You have JKSV BACAN ", "Tienes JKSV BACÁN ") + std::string{tasks::update::version_actual()} +
              strings::tr(" and version ", " y ya salió la ") + nueva +
              strings::tr(". Updating only replaces the app: your backups and saves stay as they are.",
                          ". Actualizar cambia solo la app: tus respaldos y partidas no se tocan.");

    // Notas del release: van en una o dos lineas; se juntan los saltos de linea y se quitan los "#" de markdown.
    for (const char c : tasks::update::notas_nueva())
    {
        if (c == '\r' || c == '#') { continue; }
        if (c == '\n') { if (!m_notas.empty() && m_notas.back() != ' ') { m_notas += ' '; } continue; }
        m_notas += c;
    }
    while (!m_notas.empty() && m_notas.back() == ' ') { m_notas.pop_back(); }

    m_pop->play();
}

void UpdateNoticeState::create_push_fade()
{
    auto estado = UpdateNoticeState::create();
    FadeState::create_and_push(colors::DIM_BACKGROUND, colors::ALPHA_FADE_BEGIN, colors::ALPHA_FADE_END, estado);
}

void UpdateNoticeState::update()
{
    const bool a     = input::button_pressed(HidNpadButton_A);
    const bool b     = input::button_pressed(HidNpadButton_B);
    const bool left  = input::button_pressed(HidNpadButton_AnyLeft);
    const bool right = input::button_pressed(HidNpadButton_AnyRight);

    if (!m_guarda)
    {
        // Hasta soltar todo lo que venia apretado.
        m_guarda = !input::button_held(HidNpadButton_A) && !input::button_held(HidNpadButton_B);
        return;
    }

    if (left && m_opcion != 0) { m_opcion = 0; m_cursor->play(); }
    else if (right && m_opcion != 1) { m_opcion = 1; m_cursor->play(); }
    else if (a) { UpdateNoticeState::cerrar(m_opcion == 0); }
    else if (b) { UpdateNoticeState::cerrar(false); } // B = Más tarde: es la opcion que no cambia nada.
}

void UpdateNoticeState::render()
{
    const bool hasFocus = BaseState::has_focus();

    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, graphics::SCREEN_WIDTH, graphics::SCREEN_HEIGHT, colors::DIM_BACKGROUND);
    m_caja->render(sdl::Texture::Null, hasFocus);

    int y = CY + 36;
    sdl::text::render(sdl::Texture::Null, CX + PAD, y, 15, sdl::text::NO_WRAP, colors::BLUE_GREEN, strings::tr("UPDATE", "ACTUALIZACIÓN"));
    y += 28;
    sdl::text::render(sdl::Texture::Null, CX + PAD, y, 30, sdl::text::NO_WRAP, colors::WHITE,
                      strings::tr("A new version is out", "Hay una versión nueva"));
    y += 52;

    for (const std::string &l : ui::lista::partir(m_texto, 19, ANCHO, 3))
    {
        sdl::text::render(sdl::Texture::Null, CX + PAD, y, 19, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, l);
        y += 27;
    }

    if (!m_notas.empty())
    {
        y += 8;
        const std::string notas = strings::tr("What's new: ", "Novedades: ") + m_notas;
        for (const std::string &l : ui::lista::partir(notas, 17, ANCHO, 2))
        {
            sdl::text::render(sdl::Texture::Null, CX + PAD, y, 17, sdl::text::NO_WRAP, colors::WHITE, l);
            y += 25;
        }
    }

    // Botones: el elegido con el color del tema.
    const char *etiquetas[2] = {strings::tr("Update now", "Actualizar ahora"), strings::tr("Later", "Más tarde")};
    for (int i = 0; i < 2; i++)
    {
        const int bx      = CX + PAD + i * (BOTON_W + BOTON_GAP);
        const bool activo = i == m_opcion;
        ui::lista::pildora(sdl::Texture::Null, bx, BOTON_Y, BOTON_W, BOTON_H, activo ? colors::BLUE_GREEN : colors::PANEL_SOLID);
        const int tw = sdl::text::get_width(20, etiquetas[i]);
        sdl::text::render(sdl::Texture::Null, bx + (BOTON_W - tw) / 2, BOTON_Y + (BOTON_H - 20) / 2 - 2, 20, sdl::text::NO_WRAP,
                          activo ? colors::CLEAR_COLOR : colors::WHITE, etiquetas[i]);
    }

    sdl::text::render(sdl::Texture::Null, CX + PAD, BOTON_Y + BOTON_H + 12, 15, sdl::text::NO_WRAP, colors::DIALOG_LIGHT,
                      strings::tr("If you pick Later, it asks again next time you open JKSV.",
                                  "Si eliges Más tarde, te vuelve a avisar la próxima vez que abras JKSV."));
}

void UpdateNoticeState::cerrar(bool actualizar)
{
    FadeState::create_and_push(colors::DIM_BACKGROUND, colors::ALPHA_FADE_END, colors::ALPHA_FADE_BEGIN, nullptr);
    BaseState::deactivate();
    if (actualizar)
    {
        auto datos = std::make_shared<sys::Task::DataStruct>();
        ProgressState::create_push_fade(tasks::update::download_update, datos);
    }
}
