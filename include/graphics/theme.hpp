#pragma once
#include <cstdint>

// Temas de color de JKSV BACAN. Cambian los colores de colors.hpp que no son constexpr (fondo, paneles,
// lineas, seleccion, barra), asi cada pantalla toma el tema sin tocarla. 0 = Ambar, el de por defecto.
namespace theme
{
    /// @brief Cuantos temas hay.
    int count() noexcept;

    /// @brief Nombre del tema, para Ajustes.
    const char *name(int index) noexcept;

    /// @brief Aplica el tema guardado en la config (clave "Tema"). Fuera de rango: Ambar.
    void apply_from_config() noexcept;

    /// @brief Pasa al siguiente tema, lo guarda y lo aplica.
    void cycle() noexcept;
}
