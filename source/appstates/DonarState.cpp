#include "appstates/DonarState.hpp"

#include "StateManager.hpp"
#include "appstates/FadeState.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "strings/strings.hpp"
#include "ui/ListaSecciones.hpp"

#include <ctime>
#include <fstream>
#include <string>

extern "C"
{
#include "qrcodegen.h"
}

namespace
{
    // El mismo enlace que la invitacion de OQB Updater.
    constexpr const char *URL_DONAR = "https://www.paypal.com/ncp/payment/F8U8BWLLQ7MY6";
    // Aperturas y la ultima vez que salio, en segundos desde 1970.
    constexpr const char *REGISTRO = "sdmc:/config/JKSV/donar.txt";
    constexpr long PRIMERA        = 3;
    constexpr long long CADA      = 3LL * 24 * 60 * 60;
    constexpr int ESPERA          = 4; // segundos antes de poder cerrarla

    // Tarjeta centrada: el texto a la izquierda y el QR a la derecha.
    constexpr int CW = 820, CH = 420;
    constexpr int CX = (1280 - CW) / 2, CY = (720 - CH) / 2;
    constexpr int PAD = 44;
    constexpr int QR  = 230;
    constexpr int TEXTO_W = CW - PAD * 3 - QR;
    constexpr int BOTON_H = 52, BOTON_W = 300, BOTON_Y = CY + CH - PAD - BOTON_H;
}

DonarState::DonarState()
    : m_caja(ui::DialogBox::create(CX, CY, CW, CH))
{
    std::vector<uint8_t> temp(qrcodegen_BUFFER_LEN_MAX), qr(qrcodegen_BUFFER_LEN_MAX);
    if (qrcodegen_encodeText(URL_DONAR, temp.data(), qr.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                             qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
    {
        m_lado = qrcodegen_getSize(qr.data());
        m_qr.resize(m_lado * m_lado);
        for (int y = 0; y < m_lado; y++)
        {
            for (int x = 0; x < m_lado; x++) { m_qr[y * m_lado + x] = qrcodegen_getModule(qr.data(), x, y) ? 1 : 0; }
        }
    }
}

bool DonarState::toca()
{
    long aperturas      = 0;
    long long ultima    = 0;
    {
        std::ifstream f{REGISTRO};
        if (f) { f >> aperturas >> ultima; }
    }
    ++aperturas;
    const long long ahora = static_cast<long long>(std::time(nullptr));
    const bool sale       = aperturas >= PRIMERA && (ultima <= 0 || ahora - ultima >= CADA);
    std::ofstream f{REGISTRO, std::ios::trunc};
    if (f) { f << aperturas << ' ' << ultima << '\n'; }
    return sale;
}

void DonarState::create_push_fade()
{
    // Se anota cuando de verdad sale: si esta vez le gano el aviso de version nueva, sale la proxima.
    long aperturas = 0;
    long long ultima = 0;
    {
        std::ifstream f{REGISTRO};
        if (f) { f >> aperturas >> ultima; }
    }
    {
        std::ofstream f{REGISTRO, std::ios::trunc};
        if (f) { f << aperturas << ' ' << static_cast<long long>(std::time(nullptr)) << '\n'; }
    }

    auto estado = DonarState::create();
    FadeState::create_and_push(colors::DIM_BACKGROUND, colors::ALPHA_FADE_BEGIN, colors::ALPHA_FADE_END, estado);
}

int DonarState::restantes() const
{
    const auto pasado = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_inicio).count();
    return pasado >= ESPERA ? 0 : static_cast<int>(ESPERA - pasado);
}

void DonarState::update()
{
    if (!m_guarda)
    {
        // Hasta soltar todo lo que venia apretado.
        m_guarda = !input::button_held(HidNpadButton_A) && !input::button_held(HidNpadButton_B);
        return;
    }
    if (DonarState::restantes() > 0) { return; }
    if (input::button_pressed(HidNpadButton_A) || input::button_pressed(HidNpadButton_B)) { DonarState::cerrar(); }
}

void DonarState::render()
{
    const bool hasFocus = BaseState::has_focus();

    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, graphics::SCREEN_WIDTH, graphics::SCREEN_HEIGHT, colors::DIM_BACKGROUND);
    m_caja->render(sdl::Texture::Null, hasFocus);

    int y = CY + 36;
    sdl::text::render(sdl::Texture::Null, CX + PAD, y, 15, sdl::text::NO_WRAP, colors::BLUE_GREEN, strings::tr("SUPPORT", "APOYO"));
    y += 28;
    sdl::text::render(sdl::Texture::Null, CX + PAD, y, 30, sdl::text::NO_WRAP, colors::WHITE,
                      strings::tr("Buy us a coffee", "Invítanos un café"));
    y += 54;
    const std::string texto =
        strings::tr("JKSV BACAN is free and will stay free. If it has helped you and you want to support the project, scan the code "
                    "with your phone and give whatever amount you like. It is completely voluntary.",
                    "JKSV BACÁN es gratis y lo seguirá siendo. Si te ha servido y quieres apoyar el proyecto, escanea el código con "
                    "tu teléfono y colabora con la cantidad que prefieras. Es totalmente voluntario.");
    for (const std::string &l : ui::lista::partir(texto, 19, TEXTO_W, 7))
    {
        sdl::text::render(sdl::Texture::Null, CX + PAD, y, 19, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, l);
        y += 27;
    }

    // El QR, con su margen blanco para que el telefono lo lea.
    const int qx = CX + CW - PAD - QR, qy = CY + PAD + 8;
    sdl::render_rect_fill(sdl::Texture::Null, qx, qy, QR, QR, colors::WHITE);
    if (m_lado > 0)
    {
        const int modulo = (QR - 24) / m_lado;
        const int ox = qx + (QR - modulo * m_lado) / 2, oy = qy + (QR - modulo * m_lado) / 2;
        for (int my = 0; my < m_lado; my++)
        {
            for (int mx = 0; mx < m_lado; mx++)
            {
                if (m_qr[my * m_lado + mx]) { sdl::render_rect_fill(sdl::Texture::Null, ox + mx * modulo, oy + my * modulo, modulo, modulo, colors::BLACK); }
            }
        }
    }

    // «En otra ocasión», con la cuenta regresiva mientras no se puede cerrar.
    const int quedan        = DonarState::restantes();
    const std::string boton = quedan > 0 ? std::string{strings::tr("Maybe later", "En otra ocasión")} + " (" + std::to_string(quedan) + ")"
                                         : std::string{strings::tr("Maybe later", "En otra ocasión")};
    ui::lista::pildora(sdl::Texture::Null, CX + PAD, BOTON_Y, BOTON_W, BOTON_H, quedan > 0 ? colors::PANEL_SOLID : colors::BLUE_GREEN);
    const int tw = sdl::text::get_width(20, boton.c_str());
    sdl::text::render(sdl::Texture::Null, CX + PAD + (BOTON_W - tw) / 2, BOTON_Y + (BOTON_H - 20) / 2 - 2, 20, sdl::text::NO_WRAP,
                      quedan > 0 ? colors::WHITE : colors::CLEAR_COLOR, boton);
}

void DonarState::cerrar()
{
    FadeState::create_and_push(colors::DIM_BACKGROUND, colors::ALPHA_FADE_END, colors::ALPHA_FADE_BEGIN, nullptr);
    BaseState::deactivate();
}
