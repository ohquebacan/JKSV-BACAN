#pragma once
#include "sdl.hpp"

namespace colors
{
    // Los que no son constexpr los cambia el tema (theme.hpp): fondo, paneles, lineas, seleccion y barra.
    inline constexpr sdl::Color WHITE             = {0xFFFFFFFF};
    inline constexpr sdl::Color BLACK             = {0x000000FF};
    inline constexpr sdl::Color RED               = {0xFF0000FF};
    inline constexpr sdl::Color DARK_RED          = {0xDD0000FF};
    inline constexpr sdl::Color GREEN             = {0x00FF00FF};
    inline constexpr sdl::Color BLUE              = {0x0099EEFF};
    inline constexpr sdl::Color YELLOW            = {0xF8FC00FF};
    inline constexpr sdl::Color PINK              = {0xFF4444FF};
    inline sdl::Color BLUE_GREEN        = {0x00FFC5FF};
    inline sdl::Color CLEAR_COLOR       = {0x2D2D2DFF};
    inline constexpr sdl::Color CLEAR_PANEL       = {0x0D0D0DFF};
    inline sdl::Color DIALOG_DARK       = {0x505050FF};
    inline constexpr sdl::Color DIALOG_LIGHT      = {0xDCDCDCFF};
    inline constexpr sdl::Color DIM_BACKGROUND    = {0x000000B4}; // rediseno: negro, no azulado
    inline constexpr sdl::Color TRANSPARENT       = {0x00000000};
    inline sdl::Color PANEL_SOLID       = {0x15120DFF}; // columna de usuarios (opaco, tema)
    inline sdl::Color SLIDE_PANEL_CLEAR = {0x000000CC};
    inline sdl::Color DIV_COLOR         = {0x707070FF};
    inline constexpr sdl::Color GUIDE_COLOR       = {0x0000007F};
    inline sdl::Color BAR_GREEN         = {0x11CC33FF};
    inline constexpr sdl::Color GOLD              = {0xFFD700FF};

    inline constexpr uint8_t ALPHA_FADE_BEGIN = 0x00;
    inline constexpr uint8_t ALPHA_FADE_END   = 0x80;
} // namespace colors
