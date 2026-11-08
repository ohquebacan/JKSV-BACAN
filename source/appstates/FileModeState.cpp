#include "appstates/FileModeState.hpp"

#include "appstates/FileOptionState.hpp"
#include "config/config.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "mathutil.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"

#include <algorithm>
#include <cctype>
#include <vector>
#include <set>
#include <cmath>
#include <string>

namespace
{
    /// @brief This is the X position of the file mode pop up.
    constexpr int PERMA_X = 15;

    /// @brief This is the target Y of the pop-up. The starting Y is the height of the screen.
    constexpr int TARGET_Y = 91;

    std::string tamano_corto(int64_t bytes)
    {
        if (bytes < 0) { return "?"; }
        if (bytes < 1024) { return stringutil::get_formatted_string("%lld B", static_cast<long long>(bytes)); }
        if (bytes < 1024 * 1024) { return stringutil::get_formatted_string("%.0f KB", bytes / 1024.0); }
        if (bytes < 1024LL * 1024 * 1024) { return stringutil::get_formatted_string("%.1f MB", bytes / (1024.0 * 1024.0)); }
        return stringutil::get_formatted_string("%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }

    // Nombre que no cabe: se corta al final con "…", por caracteres enteros.
    std::string cortar_nombre(std::string texto, int size, int maximo)
    {
        if (sdl::text::get_width(size, texto) <= maximo) { return texto; }
        while (!texto.empty() && sdl::text::get_width(size, texto + "…") > maximo)
        {
            unsigned char quitado{};
            do {
                quitado = static_cast<unsigned char>(texto.back());
                texto.pop_back();
            } while (!texto.empty() && (quitado & 0xC0) == 0x80);
        }
        return texto + "…";
    }

    // Ruta que no cabe: se muestra el final, con "…" al inicio. Se quita de a un caracter entero (UTF-8).
    std::string final_de_ruta(std::string ruta, int size, int maximo)
    {
        if (sdl::text::get_width(size, ruta) <= maximo) { return ruta; }
        while (!ruta.empty() && sdl::text::get_width(size, "…" + ruta) > maximo)
        {
            size_t largo = 1;
            const unsigned char c = static_cast<unsigned char>(ruta[0]);
            if (c >= 0xF0) { largo = 4; }
            else if (c >= 0xE0) { largo = 3; }
            else if (c >= 0xC0) { largo = 2; }
            ruta.erase(0, std::min(largo, ruta.size()));
        }
        return "…" + ruta;
    }
}

//                      ---- Construction ----

FileModeState::FileModeState(std::string_view mountA, std::string_view mountB, int64_t journalSize, bool isSystem)
    : m_mountA(mountA)
    , m_mountB(mountB)
    , m_isSystem(isSystem)
    , m_allowSystem(config::get_by_key(config::keys::ALLOW_WRITING_TO_SYSTEM))
    , m_journalSize(journalSize)
    , m_transition(PERMA_X, graphics::SCREEN_HEIGHT, 0, 0, PERMA_X, TARGET_Y, 0, 0, ui::Transition::DEFAULT_THRESHOLD)
    , m_state(State::Rising)
{
    FileModeState::initialize_static_members();
    FileModeState::initialize_paths();
    FileModeState::initialize_menus();
}

//                      ---- Public functions ----

void FileModeState::update()
{
    switch (m_state)
    {
        case State::Rising:
        case State::Dropping: FileModeState::update_y(); break;
        case State::Open:     FileModeState::update_handle_input(); break;
    }
}

void FileModeState::render()
{
    static constexpr int ANCHO  = 1250;
    static constexpr int ALTO   = 555;
    static constexpr int MITAD  = 617;
    static constexpr int TOPE_Y = 44; // lo de arriba de cada panel: la ruta

    const bool hasFocus = BaseState::has_focus();
    const int y         = m_transition.get_y();

    sm_renderTarget->clear(colors::TRANSPARENT);
    sm_controlGuide->render(sdl::Texture::Null, hasFocus);

    // Rediseno: panel del tema con borde (en lugar del marco de imagen) y la ruta de cada lado arriba;
    // la del lado activo, resaltada.
    sdl::render_rect_fill(sdl::Texture::Null, PERMA_X, y, ANCHO, ALTO, colors::PANEL_SOLID);
    sdl::render_line(sdl::Texture::Null, PERMA_X, y, PERMA_X + ANCHO, y, colors::DIV_COLOR);
    sdl::render_line(sdl::Texture::Null, PERMA_X, y + ALTO, PERMA_X + ANCHO, y + ALTO, colors::DIV_COLOR);
    sdl::render_line(sdl::Texture::Null, PERMA_X, y, PERMA_X, y + ALTO, colors::DIV_COLOR);
    sdl::render_line(sdl::Texture::Null, PERMA_X + ANCHO, y, PERMA_X + ANCHO, y + ALTO, colors::DIV_COLOR);

    const bool activaA = m_target == Target::MountA;
    sdl::render_line(sm_renderTarget, MITAD, 0, MITAD, 538, colors::DIV_COLOR);
    sdl::render_rect_fill(sm_renderTarget, activaA ? 0 : MITAD + 1, 0, activaA ? MITAD : 1234 - MITAD - 1, 3, colors::BLUE_GREEN);
    // Ruta y, a la derecha, el espacio libre de esa unidad.
    for (int lado = 0; lado < 2; lado++)
    {
        const bool esA           = lado == 0;
        const fslib::Path &ruta  = esA ? m_pathA : m_pathB;
        const int baseX         = esA ? 0 : MITAD + 1;
        const std::string libre = tamano_corto(fslib::get_device_free_space(fslib::Path{(esA ? m_mountA : m_mountB) + ":/"}));
        const std::string txt   = stringutil::get_formatted_string(strings::tr("%s free", "%s libres"), libre.c_str());
        const int lw            = sdl::text::get_width(16, txt);
        sdl::text::render(sm_renderTarget, baseX + 600 - lw - 20, 14, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, txt);
        sdl::text::render(sm_renderTarget, baseX + 16, 12, 18, sdl::text::NO_WRAP, (esA == activaA) ? colors::BLUE_GREEN : colors::DIALOG_LIGHT,
                          final_de_ruta(ruta.string(), 18, 580 - lw - 40));
    }
    sdl::render_line(sm_renderTarget, 0, TOPE_Y, 1234, TOPE_Y, colors::DIV_COLOR);

    FileModeState::dibujar_lista(Target::MountA, 0, MITAD, hasFocus && activaA);
    FileModeState::dibujar_lista(Target::MountB, MITAD + 1, 1234 - MITAD - 1, hasFocus && !activaA);

    // Dentro del panel, abajo: el orden y las marcas del lado activo (fuera del panel lo tapaba la barra de
    // controles).
    {
        const char *modo = m_modoOrden == 0 ? strings::tr("name", "nombre") : m_modoOrden == 1 ? strings::tr("size", "tamaño") : strings::tr("type", "tipo");
        const int marcas = static_cast<int>((activaA ? m_marcasA : m_marcasB).size());
        std::string pie  = marcas > 0 ? stringutil::get_formatted_string(strings::tr("%d marked  ·  X: copy, move or delete them", "%d marcados  ·  X: copiarlos, moverlos o borrarlos"), marcas)
                                      : std::string{strings::tr("A or Y on a file: mark it  ·  X: options", "A o Y sobre un archivo: marcarlo  ·  X: opciones")};
        pie += stringutil::get_formatted_string(strings::tr("  ·  R: sort by %s", "  ·  R: orden por %s"), modo);
        sdl::render_line(sm_renderTarget, 0, 504, 1234, 504, colors::DIV_COLOR);
        sdl::text::render(sm_renderTarget, 16, 512, 16, sdl::text::NO_WRAP, marcas > 0 ? colors::BLUE_GREEN : colors::DIALOG_LIGHT, pie);
    }

    sm_renderTarget->render(sdl::Texture::Null, PERMA_X + 8, y + 8);
}

//                      ---- Private functions ----

void FileModeState::initialize_static_members()
{
    // Frame coords and dimensions.
    static constexpr int FRAME_WIDTH  = 1250;
    static constexpr int FRAME_HEIGHT = 555;

    // Inner target coords and dimensions.
    static constexpr int INNER_WIDTH  = 1234;
    static constexpr int INNER_HEIGHT = 538;

    // This is the name of the render target for the main body.
    static constexpr std::string_view RENDER_TARGET_NAME = "FMRenderTarget";

    if (sm_frame && sm_renderTarget && sm_controlGuide) { return; }

    sm_frame        = ui::Frame::create(PERMA_X, graphics::SCREEN_HEIGHT, FRAME_WIDTH, FRAME_HEIGHT);
    sm_renderTarget = sdl::TextureManager::load(RENDER_TARGET_NAME, INNER_WIDTH, INNER_HEIGHT, SDL_TEXTUREACCESS_TARGET);
    sm_controlGuide = ui::ControlGuide::create(strings::get_by_name(strings::names::CONTROL_GUIDES, 4));
}

void FileModeState::initialize_paths()
{
    m_pathA = m_mountA + ":/";
    m_pathB = m_mountB + ":/";
}

void FileModeState::initialize_menus()
{
    // Menu A.
    static constexpr int MENU_A_X = 8;

    // Menu B.
    static constexpr int MENU_B_X = 630;

    // Shared.
    static constexpr int MENU_Y             = 52; // debajo de la ruta
    static constexpr int MENU_WIDTH         = 594;
    static constexpr int MENU_FONT_SIZE     = 20;
    static constexpr int MENU_TARGET_HEIGHT = 538;

    m_dirMenuA = ui::Menu::create(MENU_A_X, MENU_Y, MENU_WIDTH, MENU_FONT_SIZE, MENU_TARGET_HEIGHT);
    m_dirMenuB = ui::Menu::create(MENU_B_X, MENU_Y, MENU_WIDTH, MENU_FONT_SIZE, MENU_TARGET_HEIGHT);

    FileModeState::initialize_directory_menu(m_pathA, m_dirA, *m_dirMenuA.get());
    FileModeState::initialize_directory_menu(m_pathB, m_dirB, *m_dirMenuB.get());
}

void FileModeState::initialize_directory_menu(const fslib::Path &path, fslib::Directory &directory, ui::Menu &menu)
{

    directory.open(path);
    if (!directory.is_open()) { return; }

    // Rediseno: sin [D]/[F]. Las carpetas van en el color del tema y terminan en "/"; los archivos, normal.
    menu.reset();
    menu.add_option(strings::tr("This folder", "Esta carpeta"));
    menu.add_option(strings::tr("Up one level", "Subir un nivel"));

    // Orden: carpetas primero y luego segun el modo (nombre, tamano o tipo). La lista guarda el orden y las
    // marcas se limpian al cambiar de carpeta.
    const bool ladoA            = &directory == &m_dirA;
    std::vector<int> &orden     = ladoA ? m_ordenA : m_ordenB;
    (ladoA ? m_marcasA : m_marcasB).clear();
    orden.clear();
    for (int i = 0; i < static_cast<int>(directory.get_count()); i++) { orden.push_back(i); }
    auto minus = [](const char *t) {
        std::string r{t};
        for (char &c : r) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return r;
    };
    auto extension = [&](const char *t) {
        const std::string n = minus(t);
        const size_t punto  = n.find_last_of('.');
        return punto == std::string::npos ? std::string{} : n.substr(punto + 1);
    };
    std::stable_sort(orden.begin(), orden.end(), [&](int a, int b) {
        const fslib::DirectoryEntry &ea = directory[a];
        const fslib::DirectoryEntry &eb = directory[b];
        if (ea.is_directory() != eb.is_directory()) { return ea.is_directory(); }
        if (m_modoOrden == 1 && !ea.is_directory() && ea.get_size() != eb.get_size()) { return ea.get_size() > eb.get_size(); }
        if (m_modoOrden == 2 && extension(ea.get_filename()) != extension(eb.get_filename()))
        {
            return extension(ea.get_filename()) < extension(eb.get_filename());
        }
        return minus(ea.get_filename()) < minus(eb.get_filename());
    });
    for (const int i : orden) { menu.add_option(directory[i].get_filename()); }
}

void FileModeState::update_y() noexcept
{
    // Update the transition.
    m_transition.update();

    // Grab the Y and update the frame.
    const int y = m_transition.get_y();
    sm_frame->set_y(y);

    // Conditions for changing to next state.
    const bool finishedRising   = m_state == State::Rising && m_transition.in_place_xy();
    const bool finishedDropping = m_state == State::Dropping && m_transition.in_place_xy();
    if (finishedRising) { m_state = State::Open; }
    else if (finishedDropping) { FileModeState::deactivate_state(); }
}

void FileModeState::update_handle_input() noexcept
{
    // Get whether or not the state has focus.
    const bool hasFocus = BaseState::has_focus();

    // Grab references to the current target we're working with.
    ui::Menu &menu              = FileModeState::get_source_menu();
    fslib::Path &path           = FileModeState::get_source_path();
    fslib::Directory &directory = FileModeState::get_source_directory();

    // Input bools.
    const bool aPressed     = input::button_pressed(HidNpadButton_A);
    const bool bPressed     = input::button_pressed(HidNpadButton_B);
    const bool xPressed     = input::button_pressed(HidNpadButton_X);
    const bool zlZRPressed  = input::button_pressed(HidNpadButton_ZL) || input::button_pressed(HidNpadButton_ZR);
    const bool minusPressed = input::button_pressed(HidNpadButton_Minus);

    // Conditions
    const bool yPressed = input::button_pressed(HidNpadButton_Y);
    const bool rPressed = input::button_pressed(HidNpadButton_R);
    if (yPressed)
    {
        // Marcar o desmarcar la fila elegida para copiar, mover o borrar varias a la vez.
        const int real = indice_real(m_target, menu.get_selected());
        if (real >= 0)
        {
            std::set<int> &marcas = m_target == Target::MountA ? m_marcasA : m_marcasB;
            if (!marcas.erase(real)) { marcas.insert(real); }
        }
    }
    else if (rPressed)
    {
        // Cambiar el orden: nombre, tamano, tipo.
        m_modoOrden = (m_modoOrden + 1) % 3;
        FileModeState::initialize_directory_menu(m_pathA, m_dirA, *m_dirMenuA.get());
        FileModeState::initialize_directory_menu(m_pathB, m_dirB, *m_dirMenuB.get());
    }
    else if (aPressed) { FileModeState::enter_selected(path, directory, menu); }
    else if (bPressed) { FileModeState::up_one_directory(path, directory, menu); }
    else if (xPressed) { FileModeState::open_option_menu(directory, menu); }
    else if (zlZRPressed) { FileModeState::change_target(); }
    else if (minusPressed)
    {
        m_state = State::Dropping;
        m_transition.set_target_y(graphics::SCREEN_HEIGHT);
    }

    // Update the menu and control guide.
    menu.update(hasFocus);
    sm_controlGuide->update(hasFocus);
}

void FileModeState::enter_selected(fslib::Path &path, fslib::Directory &directory, ui::Menu &menu)
{
    const int selected = menu.get_selected();

    switch (selected)
    {
        case 0: return;
        case 1: FileModeState::up_one_directory(path, directory, menu); break;
        default:
        {
            const int dirIndex                 = indice_real(m_target, selected);
            if (dirIndex < 0) { break; }
            const fslib::DirectoryEntry &entry = directory[dirIndex];
            if (entry.is_directory()) { FileModeState::enter_directory(path, directory, menu, entry); }
            else
            {
                // Rediseno: A sobre un archivo lo marca o desmarca (antes no hacia nada).
                std::set<int> &marcas = m_target == Target::MountA ? m_marcasA : m_marcasB;
                if (!marcas.erase(dirIndex)) { marcas.insert(dirIndex); }
            }
        }
        break;
    }
}

void FileModeState::open_option_menu(fslib::Directory &directory, ui::Menu &menu)
{
    const int selected = menu.get_selected();

    // Don't push the menu if the '..' is highlighted.
    if (selected == 0 || selected > 1) { FileOptionState::create_and_push(this); }
}

void FileModeState::change_target() { m_target = m_target == Target::MountA ? Target::MountB : Target::MountA; }

void FileModeState::up_one_directory(fslib::Path &path, fslib::Directory &directory, ui::Menu &menu)
{
    if (FileModeState::path_is_root(path)) { return; }

    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == path.NOT_FOUND) { return; }
    else if (lastSlash <= 0) { lastSlash = 1; }

    fslib::Path newPath{path.sub_path(lastSlash)};
    path = std::move(newPath);
    FileModeState::initialize_directory_menu(path, directory, menu);
}

void FileModeState::enter_directory(fslib::Path &path,
                                    fslib::Directory &directory,
                                    ui::Menu &menu,
                                    const fslib::DirectoryEntry &entry)
{
    if (!entry.is_directory()) { return; }

    path /= entry.get_filename();
    FileModeState::initialize_directory_menu(path, directory, menu);
}

ui::Menu &FileModeState::get_source_menu() noexcept
{
    return m_target == Target::MountA ? *m_dirMenuA.get() : *m_dirMenuB.get();
}

ui::Menu &FileModeState::get_destination_menu() noexcept
{
    return m_target == Target::MountA ? *m_dirMenuB.get() : *m_dirMenuA.get();
}

fslib::Path &FileModeState::get_source_path() noexcept { return m_target == Target::MountA ? m_pathA : m_pathB; }

fslib::Path &FileModeState::get_destination_path() noexcept { return m_target == Target::MountA ? m_pathB : m_pathA; }

fslib::Directory &FileModeState::get_source_directory() noexcept { return m_target == Target::MountA ? m_dirA : m_dirB; }

fslib::Directory &FileModeState::get_destination_directory() noexcept { return m_target == Target::MountA ? m_dirB : m_dirA; }

void FileModeState::deactivate_state() noexcept
{
    // This should be already set to this, but just to be sure.
    sm_frame->set_y(graphics::SCREEN_HEIGHT);

    // Close both mount points.
    fslib::close_file_system(m_mountA);
    fslib::close_file_system(m_mountB);

    // Reset the control guide.
    sm_controlGuide->reset();

    // Mark the state for deletion.
    BaseState::deactivate();
}

int FileModeState::indice_real(Target lado, int fila) const
{
    const std::vector<int> &orden = lado == Target::MountA ? m_ordenA : m_ordenB;
    const int i                   = fila - 2;
    return (i >= 0 && i < static_cast<int>(orden.size())) ? orden[i] : -1;
}

std::vector<int> FileModeState::marcadas(Target lado) const
{
    const std::set<int> &marcas = lado == Target::MountA ? m_marcasA : m_marcasB;
    return std::vector<int>(marcas.begin(), marcas.end());
}

void FileModeState::limpiar_marcas(Target lado) { (lado == Target::MountA ? m_marcasA : m_marcasB).clear(); }

void FileModeState::dibujar_lista(Target lado, int x, int ancho, bool activa)
{
    static constexpr int ARRIBA = 52;
    static constexpr int ABAJO  = 504; // debajo va el aviso de marcas y orden
    static constexpr int ALTO   = ABAJO - ARRIBA;
    static constexpr int FILA   = 38;

    const bool esA                = lado == Target::MountA;
    ui::Menu &menu                = esA ? *m_dirMenuA : *m_dirMenuB;
    fslib::Directory &dir         = esA ? m_dirA : m_dirB;
    const std::vector<int> &orden = esA ? m_ordenA : m_ordenB;
    const std::set<int> &marcas   = esA ? m_marcasA : m_marcasB;
    double &scroll                = esA ? m_scrollA : m_scrollB;
    const int total               = 2 + static_cast<int>(orden.size());
    const int elegida             = menu.get_selected();

    // Desplazamiento suave para que la elegida quede a la vista.
    {
        const double arriba = elegida * FILA - FILA * 2;
        const double abajo  = (elegida + 1) * FILA + FILA * 2 - ALTO;
        double objetivo     = scroll;
        if (arriba < objetivo) { objetivo = arriba < 0 ? 0 : arriba; }
        else if (abajo > objetivo) { objetivo = abajo; }
        scroll += (objetivo - scroll) / 3.0;
    }
    const int d = static_cast<int>(scroll);

    for (int fila = 0; fila < total; fila++)
    {
        const int fy = ARRIBA + fila * FILA - d;
        if (fy < ARRIBA - 4 || fy + FILA > ABAJO) { continue; }
        const bool sel = fila == elegida;
        if (sel)
        {
            sdl::render_rect_fill(sm_renderTarget, x + 4, fy + 1, ancho - 8, FILA - 2, colors::DIALOG_DARK);
            sdl::render_rect_fill(sm_renderTarget, x + 4, fy + 1, 4, FILA - 2, activa ? colors::BLUE_GREEN : colors::DIV_COLOR);
        }
        if (fila < 2)
        {
            const char *t = fila == 0 ? strings::tr("This folder", "Esta carpeta") : strings::tr("Up one level", "Subir un nivel");
            sdl::text::render(sm_renderTarget, x + 20, fy + 9, 18, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, t);
            continue;
        }
        const int real = orden[fila - 2];
        const fslib::DirectoryEntry &e = dir[real];
        int tx = x + 20;
        // Casilla de marca: rellena con check si esta marcada.
        if (!marcas.empty() || sel)
        {
            const bool marcada = marcas.count(real) > 0;
            if (marcada) { sdl::render_rect_fill(sm_renderTarget, tx, fy + 10, 18, 18, colors::BLUE_GREEN); }
            else
            {
                sdl::render_line(sm_renderTarget, tx, fy + 10, tx + 17, fy + 10, colors::DIV_COLOR);
                sdl::render_line(sm_renderTarget, tx, fy + 27, tx + 17, fy + 27, colors::DIV_COLOR);
                sdl::render_line(sm_renderTarget, tx, fy + 10, tx, fy + 27, colors::DIV_COLOR);
                sdl::render_line(sm_renderTarget, tx + 17, fy + 10, tx + 17, fy + 27, colors::DIV_COLOR);
            }
            tx += 28;
        }
        std::string tam{};
        if (!e.is_directory()) { tam = tamano_corto(e.get_size()); }
        const int tw = tam.empty() ? 0 : sdl::text::get_width(16, tam);
        std::string nombre{e.get_filename()};
        if (e.is_directory()) { nombre += "/"; }
        nombre = cortar_nombre(nombre, 19, ancho - (tx - x) - tw - 40);
        sdl::text::render(sm_renderTarget, tx, fy + 9, 19, sdl::text::NO_WRAP,
                          e.is_directory() ? colors::BLUE_GREEN : (sel ? colors::WHITE : colors::WHITE), nombre);
        if (!tam.empty()) { sdl::text::render(sm_renderTarget, x + ancho - tw - 18, fy + 11, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, tam); }
    }
}
