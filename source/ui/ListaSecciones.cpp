#include "ui/ListaSecciones.hpp"

#include "graphics/colors.hpp"

#include <algorithm>

namespace
{
    constexpr int LISTA_X      = 176;
    constexpr int LISTA_Y      = 104;
    constexpr int LISTA_ANCHO  = 640;
    constexpr int LISTA_ALTO   = 540;
    constexpr int ALTO_FILA    = 46;
    constexpr int ALTO_SECCION = 46;
}

void ui::lista::dibujar(const std::vector<Fila> &filas, int elegida, double &desplazamiento, bool hasFocus, int reservaPanel)
{
    static sdl::SharedTexture lienzo =
        sdl::TextureManager::load("listaSecciones", LISTA_ANCHO, LISTA_ALTO, SDL_TEXTUREACCESS_TARGET);

    const int total = static_cast<int>(filas.size());
    std::vector<int> ys(total, 0);
    std::vector<std::pair<int, const std::string *>> secciones{};
    int y = 0;
    for (int i = 0; i < total; i++)
    {
        if (!filas[i].seccion.empty() && (i == 0 || filas[i].seccion != filas[i - 1].seccion))
        {
            if (i > 0) { y += 10; }
            secciones.emplace_back(y, &filas[i].seccion);
            y += ALTO_SECCION - (i > 0 ? 10 : 0);
        }
        ys[i] = y;
        y += ALTO_FILA;
    }

    // La elegida a la vista, con desplazamiento suave.
    if (elegida >= 0 && elegida < total)
    {
        const double arriba = ys[elegida] - 70;
        const double abajo  = ys[elegida] + ALTO_FILA + 40 - LISTA_ALTO;
        double objetivo     = desplazamiento;
        if (arriba < objetivo) { objetivo = arriba < 0 ? 0 : arriba; }
        else if (abajo > objetivo) { objetivo = abajo; }
        desplazamiento += (objetivo - desplazamiento) / 4.0;
    }
    const int d = static_cast<int>(desplazamiento);

    lienzo->clear(colors::TRANSPARENT);
    for (const auto &[sy, texto] : secciones)
    {
        const int ty = sy - d;
        if (ty < -ALTO_SECCION || ty > LISTA_ALTO) { continue; }
        sdl::text::render(lienzo, 14, ty + 14, 16, sdl::text::NO_WRAP, colors::BLUE_GREEN, *texto);
    }
    for (int i = 0; i < total; i++)
    {
        const int fy = ys[i] - d;
        if (fy + ALTO_FILA < 0 || fy > LISTA_ALTO) { continue; }
        const bool sel = i == elegida;
        if (sel)
        {
            sdl::render_rect_fill(lienzo, 0, fy, LISTA_ANCHO, ALTO_FILA - 4, colors::DIALOG_DARK);
            sdl::render_rect_fill(lienzo, 0, fy, 5, ALTO_FILA - 4, hasFocus ? colors::BLUE_GREEN : colors::DIV_COLOR);
        }
        else { sdl::render_line(lienzo, 14, fy + ALTO_FILA - 3, LISTA_ANCHO - 14, fy + ALTO_FILA - 3, colors::DIV_COLOR); }

        sdl::text::render(lienzo, 22, fy + 11, 20, sdl::text::NO_WRAP, sel ? colors::BLUE_GREEN : colors::WHITE, filas[i].titulo);
        if (!filas[i].valor.empty())
        {
            int size = 18;
            while (size > 12 && sdl::text::get_width(size, filas[i].valor) > 230) { --size; }
            const int vw = sdl::text::get_width(size, filas[i].valor);
            sdl::text::render(lienzo, LISTA_ANCHO - vw - 18, fy + 12, size, sdl::text::NO_WRAP,
                              filas[i].valorActivo ? colors::BLUE_GREEN : colors::DIALOG_LIGHT, filas[i].valor);
        }
    }
    lienzo->render(sdl::Texture::Null, LISTA_X, LISTA_Y);

    // Panel: titulo, valor y para que sirve.
    if (elegida >= 0 && elegida < total)
    {
        const Fila &f = filas[elegida];
        sdl::render_rect_fill(sdl::Texture::Null, PANEL_X, LISTA_Y, PANEL_ANCHO, LISTA_ALTO - 10, colors::SLIDE_PANEL_CLEAR);
        sdl::render_line(sdl::Texture::Null, PANEL_X, LISTA_Y, PANEL_X, LISTA_Y + LISTA_ALTO - 10, colors::DIV_COLOR);
        const int y0 = LISTA_Y + reservaPanel;
        sdl::text::render(sdl::Texture::Null, PANEL_X + 28, y0 + 30, 24, PANEL_ANCHO - 56, colors::WHITE, f.titulo);
        int py = y0 + 30 + 24 * ((sdl::text::get_width(24, f.titulo) / (PANEL_ANCHO - 56)) + 1) + 18;
        if (!f.valor.empty())
        {
            sdl::text::render(sdl::Texture::Null, PANEL_X + 28, py, 20, PANEL_ANCHO - 56, f.valorActivo ? colors::BLUE_GREEN : colors::DIALOG_LIGHT, f.valor);
            py += 44;
        }
        sdl::render_line(sdl::Texture::Null, PANEL_X + 28, py, PANEL_X + PANEL_ANCHO - 28, py, colors::DIV_COLOR);
        sdl::text::render(sdl::Texture::Null, PANEL_X + 28, py + 20, 18, PANEL_ANCHO - 56, colors::DIALOG_LIGHT, f.descripcion);
    }
}

void ui::lista::marco(std::string_view titulo, std::string_view estado, bool estadoActivo)
{
    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, 1280, 720, colors::CLEAR_COLOR);
    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, 140, 720, colors::PANEL_SOLID);
    sdl::render_line(sdl::Texture::Null, 140, 0, 140, 720, colors::DIV_COLOR);
    sdl::render_line(sdl::Texture::Null, 140, 96, 1280, 96, colors::DIV_COLOR);
    sdl::render_line(sdl::Texture::Null, 140, 648, 1280, 648, colors::DIV_COLOR);
    sdl::text::render(sdl::Texture::Null, 176, 30, 34, sdl::text::NO_WRAP, colors::WHITE, titulo);
    if (!estado.empty())
    {
        const int ew = sdl::text::get_width(18, estado) + 32;
        ui::lista::pildora(sdl::Texture::Null, 1244 - ew, 34, ew, 32, estadoActivo ? colors::BLUE_GREEN : colors::DIALOG_DARK);
        sdl::text::render(sdl::Texture::Null, 1244 - ew + 16, 37, 18, sdl::text::NO_WRAP, estadoActivo ? colors::CLEAR_COLOR : colors::WHITE, estado);
    }
}

namespace
{
    size_t largo_utf8(unsigned char c)
    {
        if (c >= 0xF0) { return 4; }
        if (c >= 0xE0) { return 3; }
        if (c >= 0xC0) { return 2; }
        return 1;
    }

    // Lo que cabe de `texto` en `ancho`, por caracteres enteros; con `puntos`, deja lugar para "…".
    std::string lo_que_cabe(const std::string &texto, int size, int ancho, bool puntos)
    {
        std::string out{};
        for (size_t i = 0; i < texto.size();)
        {
            const size_t l = std::min(largo_utf8(static_cast<unsigned char>(texto[i])), texto.size() - i);
            const std::string prueba = out + texto.substr(i, l);
            if (sdl::text::get_width(size, puntos ? prueba + "…" : prueba) > ancho) { break; }
            out = prueba;
            i += l;
        }
        return out;
    }
}

std::vector<std::string> ui::lista::partir(std::string_view texto, int size, int ancho, int maxLineas)
{
    std::vector<std::string> palabras{};
    {
        std::string actual{};
        for (const char c : texto)
        {
            if (c == ' ' || c == '\n')
            {
                if (!actual.empty()) { palabras.push_back(actual); }
                actual.clear();
            }
            else { actual += c; }
        }
        if (!actual.empty()) { palabras.push_back(actual); }
    }

    std::vector<std::string> lineas{};
    std::string linea{};
    for (size_t p = 0; p < palabras.size(); p++)
    {
        std::string palabra   = palabras[p];
        const std::string con = linea.empty() ? palabra : linea + " " + palabra;
        if (sdl::text::get_width(size, con) <= ancho)
        {
            linea = con;
            continue;
        }
        if (!linea.empty())
        {
            lineas.push_back(linea);
            linea.clear();
        }
        // Una palabra que no cabe sola: se parte por caracteres.
        while (sdl::text::get_width(size, palabra) > ancho)
        {
            const std::string trozo = lo_que_cabe(palabra, size, ancho, false);
            if (trozo.empty()) { break; }
            lineas.push_back(trozo);
            palabra.erase(0, trozo.size());
        }
        linea = palabra;
    }
    if (!linea.empty()) { lineas.push_back(linea); }

    if (maxLineas > 0 && static_cast<int>(lineas.size()) > maxLineas)
    {
        std::string resto = lineas[maxLineas - 1];
        for (size_t i = maxLineas; i < lineas.size(); i++) { resto += " " + lineas[i]; }
        lineas.resize(maxLineas);
        lineas.back() = lo_que_cabe(resto, size, ancho, true) + "…";
    }
    return lineas;
}

void ui::lista::pildora(sdl::SharedTexture &target, int x, int y, int w, int h, sdl::Color color)
{
    static sdl::SharedTexture circulo = sdl::TextureManager::load("dialogCornersRound", "romfs:/Textures/DialogCornersRound.png");
    const int r = h / 2;
    if (!circulo || w < h)
    {
        sdl::render_rect_fill(target, x, y, w, h, color);
        return;
    }
    circulo->set_color_mod(color);
    circulo->render_part_stretched(target, 0, 0, 16, 32, x, y, r, h);
    sdl::render_rect_fill(target, x + r, y, w - 2 * r, h, color);
    circulo->render_part_stretched(target, 16, 0, 16, 32, x + w - r, y, r, h);
}
