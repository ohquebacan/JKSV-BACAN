#pragma once
#include "sdl.hpp"

#include <string>
#include <string_view>
#include <vector>

// Rediseno: lista por secciones (titulo corto, valor a la derecha) con un panel que explica lo elegido.
// La usan Ajustes y Extras; la seleccion y la navegacion siguen siendo las de ui::Menu.
namespace ui::lista
{
    struct Fila
    {
        std::string seccion;     // se dibuja un titulo cuando cambia respecto de la fila anterior
        std::string titulo;      // corto
        std::string valor;       // a la derecha (vacio = nada)
        bool valorActivo{};      // valor en el color del tema (Encendido) o atenuado (Apagado)
        std::string descripcion; // va en el panel de la derecha cuando la fila esta elegida
    };

    /// @brief Fondo de pantalla completa del rediseno (para pantallas que se apilan encima de todo): fondo,
    /// columna, lineas, titulo y, si se pasa, una pastilla de estado a la derecha.
    void marco(std::string_view titulo, std::string_view estado = {}, bool estadoActivo = false);

    /// @brief Reparte un texto en lineas que caben en `ancho` (por palabras; una palabra que no cabe se corta).
    /// Si pasa de `maxLineas`, la ultima termina en "…". Siempre corta por caracteres enteros (UTF-8).
    std::vector<std::string> partir(std::string_view texto, int size, int ancho, int maxLineas);

    /// @brief Rectangulo con las puntas redondeadas (suavizadas), para pildoras y etiquetas.
    void pildora(sdl::SharedTexture &target, int x, int y, int w, int h, sdl::Color color);

    /// @brief Dibuja la lista en x=176..816 y el panel en x=852..1244, entre y=97 y 648.
    void dibujar(const std::vector<Fila> &filas, int elegida, double &desplazamiento, bool hasFocus);
}
