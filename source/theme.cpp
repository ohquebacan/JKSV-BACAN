#include "graphics/theme.hpp"

#include "config/config.hpp"
#include "graphics/colors.hpp"
#include "sdl.hpp"

#include <array>

namespace
{
    struct Tema
    {
        const char *nombre;
        uint32_t fondo;     // CLEAR_COLOR
        uint32_t panel;     // SLIDE_PANEL_CLEAR (con algo de transparencia)
        uint32_t tarjeta;   // DIALOG_DARK: cuadros de dialogo e iconos genericos
        uint32_t linea;     // DIV_COLOR
        uint32_t seleccion; // BLUE_GREEN: barra y pulso de lo seleccionado
        uint32_t barra;     // BAR_GREEN: progreso
    };

    // Mismas gamas que el mockup del rediseno.
    constexpr std::array<Tema, 6> TEMAS = {{
        {"Ámbar", 0x100E0AFF, 0x15120DF2, 0x2B251BFF, 0x3D3427FF, 0xE3BF7AFF, 0xD2A458FF},
        {"Bosque", 0x0C0F0DFF, 0x101411F2, 0x232A25FF, 0x323B35FF, 0x8FC9A0FF, 0x6FB487FF},
        {"Grafito", 0x0E0F11FF, 0x131417F2, 0x26282DFF, 0x373A41FF, 0xA9B8C8FF, 0x8AA0B6FF},
        {"Amatista", 0x0F0D12FF, 0x141118F2, 0x29232FFF, 0x3A3243FF, 0xC0A8EEFF, 0xA68AE0FF},
        {"Rubí", 0x110C0DFF, 0x171012F2, 0x2E2124FF, 0x423034FF, 0xEE9A9AFF, 0xDE7C7CFF},
        {"Verde OQB", 0x0B0F0AFF, 0x0F140EF2, 0x222B20FF, 0x34402FFF, 0x9DEA9AFF, 0x34D583FF},
    }};

    void aplicar(int indice) noexcept
    {
        if (indice < 0 || indice >= static_cast<int>(TEMAS.size())) { indice = 0; }
        const Tema &t             = TEMAS[indice];
        colors::CLEAR_COLOR       = {t.fondo};
        colors::SLIDE_PANEL_CLEAR = {t.panel};
        colors::PANEL_SOLID       = {t.panel | 0xFF};
        colors::DIALOG_DARK       = {t.tarjeta};
        colors::DIV_COLOR         = {t.linea};
        colors::BLUE_GREEN        = {t.seleccion};
        colors::BAR_GREEN         = {t.barra};
        // El caracter de color ` resalta con el color de seleccion: se vuelve a registrar con el del tema.
        sdl::text::add_color_character(L'`', colors::BLUE_GREEN);
    }
}

int theme::count() noexcept { return static_cast<int>(TEMAS.size()); }

const char *theme::name(int index) noexcept
{
    if (index < 0 || index >= theme::count()) { index = 0; }
    return TEMAS[index].nombre;
}

void theme::apply_from_config() noexcept { aplicar(config::get_by_key(config::keys::THEME)); }

void theme::cycle() noexcept
{
    const int siguiente = (config::get_by_key(config::keys::THEME) + 1) % theme::count();
    config::set_by_key(config::keys::THEME, static_cast<uint8_t>(siguiente));
    aplicar(siguiente);
}
