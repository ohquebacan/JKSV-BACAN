#include "appstates/ExtrasMenuState.hpp"

#include <vector>
#include "remote/remote.hpp"
#include "ui/ListaSecciones.hpp"

#include "appstates/CloudSetupState.hpp"
#include "appstates/FileModeState.hpp"
#include "appstates/MainMenuState.hpp"
#include "data/data.hpp"
#include "error.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "keyboard/keyboard.hpp"
#include "strings/strings.hpp"
#include "ui/PopMessageManager.hpp"

#include <string_view>

namespace
{
    // This target is shared be a lot of states.
    constexpr std::string_view SECONDARY_TARGET = "SecondaryTarget";

    // Enum for switch case readability.
    enum
    {
        REINIT_DATA,
        SD_TO_SD_BROWSER,
        BIS_PRODINFO_F,
        BIS_SAFE,
        BIS_SYSTEM,
        BIS_USER,
        TERMINATE_PROCESS,
        CLOUD_SETUP
    };

    // Rediseno: orden en pantalla, por secciones, con titulos cortos y para que sirve cada uno.
    struct Extra
    {
        int accion;
        int seccion;
        const char *en, *es, *descEn, *descEs;
    };
    constexpr Extra ORDEN[] = {
        {CLOUD_SETUP, 0, "Set up the cloud", "Configurar nube", "Connect Koofr, Nextcloud or another WebDAV server, or Google Drive, to back up your saves online.",
         "Conecta Koofr, Nextcloud u otro servidor WebDAV, o Google Drive, para respaldar tus partidas en internet."},
        {REINIT_DATA, 0, "Reload games and saves", "Recargar juegos y partidas", "Reads the console again. Useful after installing or deleting games while JKSV is open.",
         "Vuelve a leer la consola. Sirve si instalaste o borraste juegos con JKSV abierto."},
        {SD_TO_SD_BROWSER, 1, "SD card browser", "Explorador de la SD", "Copy, move and delete files on the SD card.",
         "Copia, mueve y borra archivos de la SD."},
        {TERMINATE_PROCESS, 1, "Close a running process", "Cerrar un proceso", "Closes a process that keeps a save in use, so it can be backed up.",
         "Cierra un proceso que tiene una partida en uso, para poder respaldarla."},
        {BIS_PRODINFO_F, 2, "PRODINFOF partition", "Partición PRODINFOF", "Copies this system partition to the SD card. For advanced users.",
         "Copia esta partición del sistema a la SD. Para usuarios avanzados."},
        {BIS_SAFE, 2, "SAFE partition", "Partición SAFE", "Copies this system partition to the SD card. For advanced users.",
         "Copia esta partición del sistema a la SD. Para usuarios avanzados."},
        {BIS_SYSTEM, 2, "SYSTEM partition", "Partición SYSTEM", "Copies this system partition to the SD card. For advanced users.",
         "Copia esta partición del sistema a la SD. Para usuarios avanzados."},
        {BIS_USER, 2, "USER partition", "Partición USER", "Copies this system partition to the SD card. For advanced users.",
         "Copia esta partición del sistema a la SD. Para usuarios avanzados."},
    };
    constexpr int TOTAL_EXTRAS = sizeof(ORDEN) / sizeof(ORDEN[0]);
} // namespace

// Definition at bottom.
static void finish_reinitialization();

//                      ---- Construction ----

ExtrasMenuState::ExtrasMenuState()
    : m_renderTarget(sdl::TextureManager::load(SECONDARY_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(strings::get_by_name(strings::names::CONTROL_GUIDES, 5)))
{
    ExtrasMenuState::initialize_menu();
}

//                      ---- Public functions ----

void ExtrasMenuState::update()
{
    const bool hasFocus = BaseState::has_focus();
    const bool aPressed = input::button_pressed(HidNpadButton_A);
    const bool bPressed = input::button_pressed(HidNpadButton_B);

    m_extrasMenu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    const int fila = m_extrasMenu->get_selected();
    if (aPressed && fila >= 0 && fila < TOTAL_EXTRAS)
    {
        switch (ORDEN[fila].accion)
        {
            case REINIT_DATA:       ExtrasMenuState::reinitialize_data(); break;
            case SD_TO_SD_BROWSER:  ExtrasMenuState::sd_to_sd_browser(); break;
            case BIS_PRODINFO_F:    ExtrasMenuState::prodinfof_to_sd(); break;
            case BIS_SAFE:          ExtrasMenuState::safe_to_sd(); break;
            case BIS_SYSTEM:        ExtrasMenuState::system_to_sd(); break;
            case BIS_USER:          ExtrasMenuState::user_to_sd(); break;
            case TERMINATE_PROCESS: ExtrasMenuState::terminate_process(); break;
            case CLOUD_SETUP:       CloudSetupState::create_and_push(); break;
        }
    }
    else if (bPressed) { BaseState::deactivate(); }
}

void ExtrasMenuState::sub_update() { m_controlGuide->sub_update(); }

void ExtrasMenuState::render()
{
    const bool hasFocus = BaseState::has_focus();

    std::vector<ui::lista::Fila> filas{};
    for (const Extra &e : ORDEN)
    {
        ui::lista::Fila f{};
        f.seccion     = e.seccion == 0   ? strings::tr("CLOUD AND DATA", "NUBE Y DATOS")
                        : e.seccion == 1 ? strings::tr("TOOLS", "HERRAMIENTAS")
                                         : strings::tr("COPY PARTITIONS (ADVANCED)", "COPIAR PARTICIONES (AVANZADO)");
        f.titulo      = strings::tr(e.en, e.es);
        f.descripcion = strings::tr(e.descEn, e.descEs);
        if (e.accion == CLOUD_SETUP)
        {
            const bool conNube = remote::get_remote_storage() != nullptr;
            f.valor            = conNube ? strings::tr("Connected", "Conectada") : strings::tr("Not set up", "Sin configurar");
            f.valorActivo      = conNube;
        }
        filas.push_back(std::move(f));
    }
    ui::lista::dibujar(filas, m_extrasMenu->get_selected(), m_desplazamiento, hasFocus);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void ExtrasMenuState::initialize_menu()
{
    if (!m_extrasMenu) { m_extrasMenu = ui::Menu::create(32, 10, 1000, 23, 555); }

    // Rediseno: el menu solo maneja la seleccion; los textos los dibuja ui::lista en el orden de ORDEN.
    for (const Extra &e : ORDEN) { m_extrasMenu->add_option(strings::tr(e.en, e.es)); }
}

void ExtrasMenuState::reinitialize_data() { data::launch_initialization(true, finish_reinitialization); }

void ExtrasMenuState::sd_to_sd_browser() { FileModeState::create_and_push("sdmc", "sdmc", false); }

void ExtrasMenuState::prodinfof_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("prodinfo-f", FsBisPartitionId_CalibrationFile));
    if (mountError) { return; }

    FileModeState::create_and_push("prodinfo-f", "sdmc", false, true);
}

void ExtrasMenuState::safe_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("safe", FsBisPartitionId_SafeMode));
    if (mountError) { return; }

    FileModeState::create_and_push("safe", "sdmc", false, true);
}

void ExtrasMenuState::system_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("system", FsBisPartitionId_System));
    if (mountError) { return; }

    FileModeState::create_and_push("system", "sdmc", false, true);
}

void ExtrasMenuState::user_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("user", FsBisPartitionId_User));
    if (mountError) { return; }

    FileModeState::create_and_push("user", "sdmc", false, true);
}

void ExtrasMenuState::terminate_process() {}

static void finish_reinitialization()
{
    const int popTicks     = ui::PopMessageManager::DEFAULT_TICKS;
    const char *popSuccess = strings::get_by_name(strings::names::EXTRASMENU_POPS, 0);

    MainMenuState::refresh_view_states();
    ui::PopMessageManager::push_message(popTicks, popSuccess);
}
