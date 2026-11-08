#include "appstates/BackupMenuState.hpp"

#include "StateManager.hpp"
#include "appstates/ConfirmState.hpp"
#include "appstates/FadeState.hpp"
#include "appstates/CloudFolderPickerState.hpp"
#include "appstates/ProgressState.hpp"
#include "appstates/TaskState.hpp"
#include "config/config.hpp"
#include "error.hpp"
#include "fs/fs.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "keyboard/keyboard.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"
#include "sys/sys.hpp"
#include "tasks/backup.hpp"
#include "ui/PopMessageManager.hpp"
#include "ui/TextScroll.hpp"
#include "ui/ListaSecciones.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Rediseno: pantalla completa. Columna del juego a la izquierda, lista de respaldos a la derecha.
    constexpr int COLUMNA_JUEGO = 420;
    constexpr int ANCHO_LISTA   = 1280 - COLUMNA_JUEGO - 40;
    constexpr int ALTO_FILA     = 54;
    constexpr int ALTO_SECCION  = 44;

    // "PREFIJO - YYYY-MM-DD_HH-MM-SS[.zip]" -> etiqueta y fecha legible. Un nombre sin fecha se muestra tal cual.
    struct Respaldo
    {
        std::string etiqueta;
        std::string fecha;
    };

    // Hoy / Ayer / dd/mm/aaaa, con la hora.
    std::string fecha_legible(int anio, int mes, int dia, int hora, int minuto)
    {
        const time_t ahora = time(nullptr);
        struct tm hoy{};
        localtime_r(&ahora, &hoy);
        const time_t ayerT = ahora - 86400;
        struct tm ayer{};
        localtime_r(&ayerT, &ayer);
        std::string cuando{};
        if (anio == hoy.tm_year + 1900 && mes == hoy.tm_mon + 1 && dia == hoy.tm_mday) { cuando = strings::tr("Today", "Hoy"); }
        else if (anio == ayer.tm_year + 1900 && mes == ayer.tm_mon + 1 && dia == ayer.tm_mday) { cuando = strings::tr("Yesterday", "Ayer"); }
        else { cuando = stringutil::get_formatted_string("%02d/%02d/%04d", dia, mes, anio); }
        return cuando + stringutil::get_formatted_string("  ·  %02d:%02d", hora, minuto);
    }

    // Recorta a un ancho, por caracteres enteros (medio caracter UTF-8 congela el dibujo de texto).
    std::string cortar(std::string texto, int size, int maximo)
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

    std::string tamano_legible(int64_t bytes)
    {
        if (bytes < 1024) { return stringutil::get_formatted_string("%lld B", static_cast<long long>(bytes)); }
        if (bytes < 1024 * 1024) { return stringutil::get_formatted_string("%.0f KB", bytes / 1024.0); }
        return stringutil::get_formatted_string("%.1f MB", bytes / (1024.0 * 1024.0));
    }

    Respaldo leer_nombre(std::string_view nombre, std::string_view usuario)
    {
        if (nombre.size() > 4 && nombre.substr(nombre.size() - 4) == ".zip") { nombre.remove_suffix(4); }
        Respaldo r{};
        constexpr size_t LARGO = 19;
        const bool conFecha    = nombre.size() >= LARGO && nombre[nombre.size() - LARGO + 4] == '-' && nombre[nombre.size() - LARGO + 10] == '_';
        if (!conFecha)
        {
            r.fecha = std::string{nombre};
            return r;
        }
        const std::string_view sello = nombre.substr(nombre.size() - LARGO);
        std::string_view prefijo     = nombre.substr(0, nombre.size() - LARGO);
        while (!prefijo.empty() && (prefijo.back() == ' ' || prefijo.back() == '-')) { prefijo.remove_suffix(1); }

        if (prefijo.rfind("AUTO", 0) == 0) { r.etiqueta = "AUTO"; }
        else if (prefijo.rfind("PRE-SYNC", 0) == 0) { r.etiqueta = "PRE-SYNC"; }
        else if (prefijo.rfind("PARTIDA OQB", 0) == 0) { r.etiqueta = strings::tr("FULL SAVE", "PARTIDA"); }
        else if (prefijo.empty() || prefijo == usuario) { r.etiqueta = strings::tr("MANUAL", "MANUAL"); }
        else { r.etiqueta = std::string{prefijo}; }

        // Hoy / Ayer / dd-mm-aaaa, mas la hora.
        const int anio = std::atoi(std::string{sello.substr(0, 4)}.c_str());
        const int mes  = std::atoi(std::string{sello.substr(5, 2)}.c_str());
        const int dia  = std::atoi(std::string{sello.substr(8, 2)}.c_str());
        const std::string hora{sello.substr(11, 2)};
        const std::string minuto{sello.substr(14, 2)};

        r.fecha = fecha_legible(anio, mes, dia, std::atoi(hora.c_str()), std::atoi(minuto.c_str()));
        return r;
    }

    // Backups are named "[prefix] - YYYY-MM-DD_HH-MM-SS[.zip]". That 19-char timestamp sorts chronologically
    // as plain text, so keying on it lets us list newest-first regardless of the name prefix (user, AUTO, etc.).
    std::string backup_sort_key(std::string_view name)
    {
        constexpr std::string_view zipExt = ".zip";
        if (name.size() > zipExt.size() && name.substr(name.size() - zipExt.size()) == zipExt)
        {
            name = name.substr(0, name.size() - zipExt.size());
        }

        constexpr size_t stampLength = 19; // YYYY-MM-DD_HH-MM-SS
        if (name.size() >= stampLength) { return std::string(name.substr(name.size() - stampLength)); }
        return std::string(name);
    }
} // namespace

//                      ---- Construction ----

BackupMenuState::BackupMenuState(data::User *user, data::TitleInfo *titleInfo, const FsSaveDataInfo *saveInfo)
    : m_user(user)
    , m_titleInfo(titleInfo)
    , m_saveInfo(saveInfo)
    , m_saveType(m_user->get_account_save_type())
    , m_directoryPath(config::get_working_directory() / m_titleInfo->get_path_safe_title())
    , m_dataStruct(std::make_shared<BackupMenuState::DataStruct>())
    , m_controlGuide(strings::get_by_name(strings::names::CONTROL_GUIDES, 2))
{
    BackupMenuState::initialize_static_members();
    BackupMenuState::ensure_target_directory();
    BackupMenuState::initialize_remote_storage();
    BackupMenuState::initialize_task_data();
    BackupMenuState::initialize_info_string();
    BackupMenuState::save_data_check();
    BackupMenuState::refresh();

    // If the game's name is broken, the cloud folder won't match across consoles. Nudge the user to fix it.
    if (m_titleInfo->name_is_broken() && remote::get_remote_storage())
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                            strings::tr("Broken name: hold ZL to link its cloud folder.", "Nombre danado: manten ZL 1s para asociar su carpeta de la nube."));
    }
}

//                      ---- Public functions ----

void BackupMenuState::update()
{
    // Grab focus once and only once.
    const bool hasFocus = BaseState::has_focus();

    // Update the panel first.
    sm_slidePanel->update(hasFocus);

    // Grab these.
    const bool isOpen   = sm_slidePanel->is_open();
    const bool isClosed = sm_slidePanel->is_closed();

    // If the panel is closed, deactivate. If it's not open, return.
    if (isClosed) { BackupMenuState::deactivate_state(); }
    else if (!isOpen) { return; }

    // Grab the currently selected index.
    int selected{};
    {
        std::lock_guard menuGuard{sm_menuMutex};
        selected = sm_backupMenu->get_selected();
    }

    // Input bools.
    const bool aPressed  = input::button_pressed(HidNpadButton_A);
    const bool bPressed  = input::button_pressed(HidNpadButton_B);
    const bool xPressed  = input::button_pressed(HidNpadButton_X);
    const bool yPressed  = input::button_pressed(HidNpadButton_Y);
    const bool zrPressed = input::button_pressed(HidNpadButton_ZR);
    const bool lPressed  = input::button_pressed(HidNpadButton_L);

    // ZL must be HELD ~1 second to open the cloud-folder picker, so it never pops up by accident. The picker
    // works for ANY game (not just broken-named ones), so the user can re-assign or clear a previous
    // assignment now that decompressed NACPs expose the real name.
    static constexpr int ZL_HOLD_FRAMES = 60; // ~1 second at 60 fps
    const bool zlHeld                   = input::button_held(HidNpadButton_ZL) && remote::get_remote_storage();
    if (zlHeld) { ++m_zlHoldFrames; }
    else { m_zlHoldFrames = 0; }

    // Conditions.
    const bool partidaSelected = selected >= 0 && selected < static_cast<int>(m_menuEntries.size()) &&
                                 m_menuEntries[selected].type == MenuEntryType::Partida;
    const bool getPartida      = aPressed && partidaSelected;
    const bool newSelected     = selected == 0;
    const bool newBackup       = aPressed && newSelected && m_saveHasData;
    const bool overwriteBackup = aPressed && !newSelected && m_saveHasData;
    const bool restoreBackup   = yPressed && !newSelected;
    const bool deleteBackup    = xPressed && !newSelected;
    const bool uploadBackup    = zrPressed && !newSelected;
    const bool reloadRemote    = lPressed && remote::get_remote_storage();
    const bool pickCloudFolder = m_zlHoldFrames == ZL_HOLD_FRAMES; // fires once on the frame it crosses 1s
    const bool popEmpty        = aPressed && !m_saveHasData;

    if (getPartida) { BackupMenuState::confirm_partida(); }
    else if (newBackup) { BackupMenuState::name_and_create_backup(); }
    else if (overwriteBackup) { BackupMenuState::confirm_overwrite(); }
    else if (restoreBackup) { BackupMenuState::confirm_restore(); }
    else if (deleteBackup) { BackupMenuState::confirm_delete(); }
    else if (uploadBackup) { BackupMenuState::upload_backup(); }
    else if (reloadRemote) { BackupMenuState::reload_remote_listing(); }
    else if (pickCloudFolder) { BackupMenuState::open_cloud_folder_picker(); }
    else if (popEmpty) { BackupMenuState::pop_save_empty(); }
    else if (bPressed) { sm_slidePanel->close(); }
    else if (sm_slidePanel->is_closed()) { BackupMenuState::deactivate_state(); }

    // Lock and update the menu.
    std::lock_guard menuGuard{sm_menuMutex};
    sm_backupMenu->update(hasFocus);
}

void BackupMenuState::render()
{
    static constexpr int LINEA_A_Y = 42;
    static constexpr int LINEA_B_Y = 648;
    static constexpr int LISTA_X   = COLUMNA_JUEGO + 20;

    sm_slidePanel->clear_target();
    const bool hasFocus        = BaseState::has_focus();
    sdl::SharedTexture &target = sm_slidePanel->get_target();

    // Columna del juego, como el mockup: icono, nombre, de quien es y los datos de la partida.
    sdl::render_rect_fill(target, 0, 0, COLUMNA_JUEGO, 720, colors::PANEL_SOLID);
    sdl::render_line(target, COLUMNA_JUEGO, 0, COLUMNA_JUEGO, 720, colors::DIV_COLOR);
    {
        static sdl::SharedTexture esquinas = sdl::TextureManager::load("tileMask", "romfs:/Textures/TileMask.png");
        constexpr int IX = 48, IY = 44, IL = 200;
        sdl::SharedTexture icono = m_titleInfo->get_icon();
        if (icono) { icono->render_stretched(target, IX, IY, IL, IL); }
        if (esquinas)
        {
            esquinas->set_color_mod(colors::PANEL_SOLID);
            esquinas->render_stretched(target, IX, IY, IL, IL);
        }

        constexpr int ANCHO_TEXTO = COLUMNA_JUEGO - 96;
        // Titulo en hasta tres lineas reales (repartido por palabras): antes se calculaba a ojo y un nombre
        // largo tapaba la linea de abajo.
        const std::vector<std::string> lineas = ui::lista::partir(m_titleInfo->get_title(), 26, ANCHO_TEXTO, 3);
        int y = 268;
        for (const std::string &l : lineas)
        {
            sdl::text::render(target, IX, y, 26, sdl::text::NO_WRAP, colors::WHITE, l);
            y += 34;
        }
        y += 6;
        const std::string deQuien = stringutil::get_formatted_string(strings::tr("`%s`'s save", "Partida de `%s`"), m_user->get_nickname());
        sdl::text::render(target, IX, y, 18, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, deQuien);
        y += 44;

        // Datos: etiqueta atenuada, valor a la derecha.
        const int locales = m_directoryListing.is_open() ? static_cast<int>(m_directoryListing.get_count()) : 0;
        const int nube    = static_cast<int>(m_remoteListing.size());
        auto dato         = [&](const char *etiqueta, const std::string &valor)
        {
            sdl::text::render(target, IX, y, 17, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, etiqueta);
            sdl::text::render(target, IX + 150, y, 17, sdl::text::NO_WRAP, colors::WHITE, cortar(valor, 17, COLUMNA_JUEGO - IX - 170));
            y += 32;
        };
        dato(strings::tr("Last played", "Última partida"), m_ultimaPartida.empty() ? std::string{"—"} : m_ultimaPartida);
        dato(strings::tr("Size", "Tamaño"), m_saveHasData ? tamano_legible(m_tamanoPartida) : std::string{strings::tr("empty", "vacía")});
        dato(strings::tr("Backups", "Respaldos"),
             remote::get_remote_storage() ? stringutil::get_formatted_string(strings::tr("%d console · %d cloud", "%d consola · %d nube"), locales, nube)
                                          : stringutil::get_formatted_string(strings::tr("%d on the console", "%d en la consola"), locales));

        // Estado de la nube en un recuadro. El alto sale de cuantas lineas ocupa cada texto, para que nada se
        // encime ni se salga de la columna.
        y += 12;
        {
            const char *linea1{};
            const char *linea2{};
            if (!remote::get_remote_storage())
            {
                linea1 = strings::tr("Cloud not set up", "Nube sin configurar");
                linea2 = strings::tr("Extras > Cloud", "Extras › Configurar nube");
            }
            else
            {
                std::string localMasNuevo{}, nubeMasNueva{};
                for (int i = 0; i < locales; i++)
                {
                    const std::string k = backup_sort_key(m_directoryListing[i].get_filename());
                    if (k > localMasNuevo) { localMasNuevo = k; }
                }
                for (const remote::Item *item : m_remoteListing)
                {
                    const std::string k = backup_sort_key(item->get_name());
                    if (k > nubeMasNueva) { nubeMasNueva = k; }
                }
                const bool falta = nube == 0 || localMasNuevo > nubeMasNueva;
                linea1 = nube == 0 ? strings::tr("Nothing in the cloud yet", "Nada en la nube todavía")
                         : falta   ? strings::tr("Newest backup not uploaded", "Lo más nuevo no está en la nube")
                                   : strings::tr("The cloud is up to date", "La nube está al día");
                linea2 = falta ? strings::tr("Select it and press ZR", "Elígelo y pulsa ZR") : strings::tr("Nothing to upload", "Nada que subir");
            }
            const int anchoCaja = COLUMNA_JUEGO - IX - 32;
            const int anchoTxt  = anchoCaja - 28;
            const int lineas1   = sdl::text::get_width(17, linea1) / anchoTxt + 1;
            const int lineas2   = sdl::text::get_width(15, linea2) / anchoTxt + 1;
            const int alto      = 14 + lineas1 * 23 + 4 + lineas2 * 20 + 12;
            sdl::render_rect_fill(target, IX - 8, y, anchoCaja, alto, colors::DIALOG_DARK);
            sdl::text::render(target, IX + 6, y + 12, 17, anchoTxt, colors::WHITE, linea1);
            sdl::text::render(target, IX + 6, y + 14 + lineas1 * 23 + 2, 15, anchoTxt, colors::DIALOG_LIGHT, linea2);
            y += alto + 18;
        }
        if (!m_partidas.empty())
        {
            sdl::text::render(target, IX, y, 18, sdl::text::NO_WRAP, colors::WHITE, strings::tr("$Full save available$", "$Hay partida completa$"));
        }
    }

    // Lista de respaldos a la derecha.
    sdl::render_line(target, LISTA_X, LINEA_A_Y, 1260, LINEA_A_Y, colors::DIV_COLOR);
    sdl::render_line(target, LISTA_X, LINEA_B_Y, 1260, LINEA_B_Y, colors::DIV_COLOR);
    const int guiaX = 1280 - sdl::text::get_width(22, m_controlGuide) - 40;
    sdl::text::render(target, guiaX, 673, 22, sdl::text::NO_WRAP, colors::WHITE, m_controlGuide);

    sm_menuRenderTarget->clear(colors::TRANSPARENT);
    {
        std::lock_guard menuGuard{sm_menuMutex};
        BackupMenuState::render_lista(sm_menuRenderTarget, hasFocus);
    }
    sm_menuRenderTarget->render(target, LISTA_X, 50);

    sm_slidePanel->render(sdl::Texture::Null, hasFocus);
}

void BackupMenuState::refresh()
{
    // Grab pointer to remote service.
    remote::Storage *remote = remote::get_remote_storage();

    // Re-open and refresh directory.
    m_directoryListing.open(m_directoryPath);
    // If neither are valid, return.
    if (!remote && !m_directoryListing.is_open()) { return; }

    // Lock the menu.
    std::lock_guard menuGuard{sm_menuMutex};

    // Clear the menu and the entry array.
    sm_backupMenu->reset();
    m_menuEntries.clear();

    // Grab the "New" string and add it & it's NULL entry in the vector.
    const char *optionNew = strings::get_by_name(strings::names::BACKUPMENU_MENU, 0);
    sm_backupMenu->add_option(optionNew);
    m_menuEntries.push_back({MenuEntryType::Null, 0});

    // Partidas completas del catalogo de OQB, justo debajo de "Nuevo": es lo que se busca al entrar.
    m_partidas = partidas::para(m_titleInfo->get_application_id());
    for (int i = 0; i < static_cast<int>(m_partidas.size()); i++)
    {
        const std::string option = stringutil::get_formatted_string("$%s$ %s", strings::tr("Full save:", "Partida completa:"), m_partidas[i].titulo.c_str());
        sm_backupMenu->add_option(option);
        m_menuEntries.push_back({MenuEntryType::Partida, i});
    }

    // Remote->Local. Only list the remote when we actually entered this game's folder; if resolution left us
    // at the root we must NOT show the root listing (that would present every game's backups as this one's,
    // risking a wrong-game restore). In that case fall through to local-only.
    if (remote && !remote->is_at_root())
    {
        const std::string_view prefix = remote->get_prefix();
        remote->get_directory_listing(m_remoteListing);

        // Newest first: sort by the timestamp embedded in each backup's name (descending).
        std::sort(m_remoteListing.begin(),
                  m_remoteListing.end(),
                  [](const remote::Item *a, const remote::Item *b)
                  { return backup_sort_key(a->get_name()) > backup_sort_key(b->get_name()); });

        int index{};
        for (const remote::Item *item : m_remoteListing)
        {
            const std::string_view name = item->get_name();
            const std::string option    = stringutil::get_formatted_string("%s %s", prefix.data(), name.data());

            sm_backupMenu->add_option(option);
            m_menuEntries.push_back({MenuEntryType::Remote, index++});
        }
    }

    // Sort an index list so the stored indices keep pointing at the right entry in m_directoryListing while
    // still presenting local backups newest-first.
    std::vector<int> localOrder;
    localOrder.reserve(static_cast<size_t>(m_directoryListing.get_count()));
    for (int i = 0; i < static_cast<int>(m_directoryListing.get_count()); i++) { localOrder.push_back(i); }
    std::sort(localOrder.begin(),
              localOrder.end(),
              [this](int a, int b)
              { return backup_sort_key(m_directoryListing[a].get_filename()) >
                       backup_sort_key(m_directoryListing[b].get_filename()); });

    for (const int i : localOrder)
    {
        sm_backupMenu->add_option(m_directoryListing[i].get_filename());
        m_menuEntries.push_back({MenuEntryType::Local, i});
    }
}

void BackupMenuState::reinitialize_remote()
{
    // Pick up a freshly-assigned custom folder name: recompute the local path and re-resolve the remote folder,
    // exactly like opening the game does, so the menu shows the right content immediately (no need to re-enter).
    m_directoryPath = config::get_working_directory() / m_titleInfo->get_path_safe_title();
    BackupMenuState::ensure_target_directory();
    BackupMenuState::initialize_remote_storage();
    BackupMenuState::refresh();
}

void BackupMenuState::save_data_written()
{
    // Temporarily mount the save.
    fs::ScopedSaveMount tempMount{fs::DEFAULT_SAVE_MOUNT, m_saveInfo};

    // Check to see if the root is empty.
    fslib::Directory saveRoot{fs::DEFAULT_SAVE_ROOT, false};

    // Just check if the root is empty.
    m_saveHasData = saveRoot.get_count() > 0;
}

//                      ---- Private functions ----

void BackupMenuState::initialize_static_members()
{
    if (sm_backupMenu && sm_slidePanel && sm_menuRenderTarget && sm_panelWidth) { return; }

    sm_panelWidth       = 1280;
    sm_backupMenu       = ui::Menu::create(8, 8, ANCHO_LISTA - 16, 22, 600);
    sm_slidePanel       = ui::SlideOutPanel::create(sm_panelWidth, ui::SlideOutPanel::Side::Right);
    sm_menuRenderTarget = sdl::TextureManager::load("backupMenuTarget", ANCHO_LISTA, 600, SDL_TEXTUREACCESS_TARGET);
}

void BackupMenuState::ensure_target_directory()
{
    const int popTicks    = ui::PopMessageManager::DEFAULT_TICKS;
    const char *popFailed = strings::get_by_name(strings::names::BACKUPMENU_POPS, 12);

    // If this is enabled, don't bother.
    const remote::Storage *remote = remote::get_remote_storage();
    const bool autoUpload         = config::get_by_key(config::keys::AUTO_UPLOAD);
    const bool keepLocal          = config::get_by_key(config::keys::KEEP_LOCAL_BACKUPS);
    const bool directoryNeeded    = (!remote || !autoUpload || keepLocal) && !fslib::directory_exists(m_directoryPath);
    const bool directoryFailed    = directoryNeeded && error::fslib(fslib::create_directory(m_directoryPath));
    if (directoryNeeded && directoryFailed) { ui::PopMessageManager::push_message(popTicks, popFailed); }
}

void BackupMenuState::initialize_task_data()
{
    // The other members are set upon actions being taken.
    m_dataStruct->user          = m_user;
    m_dataStruct->titleInfo     = m_titleInfo;
    m_dataStruct->saveInfo      = m_saveInfo;
    m_dataStruct->basePath      = &m_directoryPath;
    m_dataStruct->spawningState = this;
}

void BackupMenuState::initialize_info_string()
{
    const char *nickname         = m_user->get_nickname();
    const char *title            = m_titleInfo->get_title();
    const std::string infoString = stringutil::get_formatted_string("`%s` - %s", nickname, title);
    m_titleScroll = ui::TextScroll::create(infoString, COLUMNA_JUEGO + 28, 8, ANCHO_LISTA - 16, 30, 22, colors::WHITE, colors::TRANSPARENT);

    // Rediseno: el nombre del juego ya esta en la columna de la izquierda.
}

void BackupMenuState::save_data_check()
{
    {
        fs::ScopedSaveMount saveMount{fs::DEFAULT_SAVE_MOUNT, m_saveInfo};
        fslib::Directory saveRoot{fs::DEFAULT_SAVE_ROOT};
        m_saveHasData = saveRoot.is_open() && saveRoot.get_count() > 0;

        // Rediseno: cuanto ocupa la partida (suma de sus archivos).
        m_tamanoPartida = 0;
        std::vector<fslib::Path> pendientes{fs::DEFAULT_SAVE_ROOT};
        while (!pendientes.empty())
        {
            const fslib::Path carpeta = pendientes.back();
            pendientes.pop_back();
            fslib::Directory dir{carpeta};
            for (int i = 0; dir.is_open() && i < static_cast<int>(dir.get_count()); i++)
            {
                if (dir[i].is_directory()) { pendientes.push_back(carpeta / dir[i]); }
                else { m_tamanoPartida += dir[i].get_size(); }
            }
        }
    }

    // Cuando se jugo por ultima vez: la marca de tiempo del save (no cambia al respaldar).
    m_ultimaPartida.clear();
    FsSaveDataExtraData extra{};
    if (fs::read_save_extra_data(m_saveInfo, extra) && extra.timestamp > 0)
    {
        const time_t t = static_cast<time_t>(extra.timestamp);
        struct tm cuando{};
        localtime_r(&t, &cuando);
        m_ultimaPartida = fecha_legible(cuando.tm_year + 1900, cuando.tm_mon + 1, cuando.tm_mday, cuando.tm_hour, cuando.tm_min);
    }
}

void BackupMenuState::initialize_remote_storage()
{
    remote::Storage *remote = remote::get_remote_storage();
    if (!remote) { return; }

    // Always resolve the per-title folder relative to the JKSV root. Every other navigation site pairs
    // change_directory() with return_to_root(); doing it here too keeps the parent from leaking in from a
    // previous state, which otherwise nests/creates title folders in the wrong place.
    remote->return_to_root();

    const bool supportsUtf8            = remote->supports_utf8();
    const std::string_view remoteTitle = supportsUtf8 ? m_titleInfo->get_title() : m_titleInfo->get_path_safe_title();

    // Resolve this game's folder. The in-memory listing can be stale right after boot (it loads async), so a
    // cache miss doesn't mean the folder is absent. create_directory() settles it against the server: it makes
    // the folder, or — if the server reports it already exists — re-registers it in the cache. Either way we
    // get a real folder to enter, instead of bailing out with the directory stuck at the root (which would
    // then list EVERY game's backups as if they belonged to this one).
    bool resolved = remote->directory_exists(remoteTitle);
    if (!resolved) { resolved = remote->create_directory(remoteTitle); }
    if (!resolved) { return; }

    // Re-pull this folder's children so we show its real contents even when the cache entry was just
    // synthesized (stale cache), and so backups another console deleted don't linger as phantoms.
    remote->reload_folder(remoteTitle);

    const remote::Item *remoteDir = remote->get_directory_by_name(remoteTitle);
    if (!remoteDir) { return; }

    remote->change_directory(remoteDir);
}

void BackupMenuState::name_and_create_backup()
{
    // Size of the buffer for naming backups.
    static constexpr size_t SIZE_NAME_LENGTH = 0x80;

    // Zip extension because it's used in multiple spots.
    static constexpr const char *STRING_ZIP_EXT = ".zip";

    // Remote storage pointer. This is only used for testing if it's valid at this point.
    remote::Storage *remote = remote::get_remote_storage();

    // Config needed.
    const bool autoName   = config::get_by_key(config::keys::AUTO_NAME_BACKUPS);
    const bool autoUpload = config::get_by_key(config::keys::AUTO_UPLOAD);
    const bool exportZip  = autoUpload || config::get_by_key(config::keys::EXPORT_TO_ZIP);

    // Input.
    const bool zrHeld = input::button_held(HidNpadButton_ZR);

    // Whether or not we should skip the keyboard.
    const bool autoNamed = (autoName || zrHeld); // This can be eval'd here.

    // This is the buffer for naming. It's auto filled as [User] - [Date].
    char name[SIZE_NAME_LENGTH + 1] = {0};
    {
        const char *nickname   = m_user->get_path_safe_nickname();
        const std::string date = stringutil::get_date_string();
        std::snprintf(name, SIZE_NAME_LENGTH, "%s - %s", nickname, date.c_str());
    }

    // Doing this like this so the strings don't linger. Dictionary entries for the keyboard.
    keyboard::Dictionary dictionary{};
    {
        // Array of dictionary strings.
        const std::array<std::string, 8> dictionaryStrings = {
            stringutil::get_date_string(stringutil::DateFormat::Year_Month_Day),
            stringutil::get_date_string(stringutil::DateFormat::Year_Day_Month),
            stringutil::get_date_string(stringutil::DateFormat::YearMonthDay),
            stringutil::get_date_string(stringutil::DateFormat::YearDayMonth),
            stringutil::get_date_string(stringutil::DateFormat::AscTime),
            m_user->get_path_safe_nickname(),
            STRING_ZIP_EXT};

        // Add them before continuing.
        for (const std::string_view word : dictionaryStrings) { dictionary.add_word_to_list(word); }
    }

    // Header string for the keyboard.
    const char *keyboardHeader = strings::get_by_name(strings::names::KEYBOARD, 0);
    // Stores whether or not the input was successful.
    const bool named =
        autoNamed || keyboard::get_input(SwkbdType_QWERTY, name, keyboardHeader, name, SIZE_NAME_LENGTH, dictionary);
    if (!named) { return; }

    // Send the signal that the backup task should signal completion.
    m_dataStruct->killTask = true;

    // Check for and append zip extension if needed.
    const bool hasZipExt = std::strstr(name, STRING_ZIP_EXT); // This might not be the best check.
    const bool needsZip  = !hasZipExt && (autoUpload || exportZip);
    if (needsZip) { std::strncat(name, STRING_ZIP_EXT, SIZE_NAME_LENGTH); }

    // This is used by both if the keep local is enabled anyway.
    m_dataStruct->path = m_directoryPath / name;

    if (autoUpload && remote) // If both autoUpload and remote is valid.
    {
        // Set the name.
        m_dataStruct->remoteName = name;

        // Start the process.
        ProgressState::create_push_fade(tasks::backup::create_new_backup_remote, m_dataStruct);
    }
    else
    {
        // Start the process.
        ProgressState::create_push_fade(tasks::backup::create_new_backup_local, m_dataStruct);
    }
}

void BackupMenuState::confirm_overwrite()
{
    // Grab selected index and entry.
    const int selected     = sm_backupMenu->get_selected();
    const MenuEntry &entry = m_menuEntries.at(selected);

    // Whether or not holding is required.
    const bool holdRequired = config::get_by_key(config::keys::HOLD_FOR_OVERWRITE);

    // Template/format for the confirmation.
    const char *confirmTemplate = strings::get_by_name(strings::names::BACKUPMENU_CONFS, 0);

    if (entry.type == MenuEntryType::Remote)
    {
        m_dataStruct->remoteItem = m_remoteListing.at(entry.index);
        const char *itemName     = m_dataStruct->remoteItem->get_name().data();
        std::string query        = stringutil::get_formatted_string(confirmTemplate, itemName);

        ConfirmProgress::create_push_fade(query, holdRequired, tasks::backup::overwrite_backup_remote, nullptr, m_dataStruct);
    }
    else if (entry.type == MenuEntryType::Local)
    {
        m_dataStruct->path     = m_directoryPath / m_directoryListing[entry.index];
        const char *targetName = m_directoryListing[entry.index].get_filename();
        std::string query      = stringutil::get_formatted_string(confirmTemplate, targetName);

        ConfirmProgress::create_push_fade(query, holdRequired, tasks::backup::overwrite_backup_local, nullptr, m_dataStruct);
    }
}

void BackupMenuState::confirm_restore()
{
    // Tired of typing it out.
    static constexpr int POP_TICKS = ui::PopMessageManager::DEFAULT_TICKS;

    // Grab selected.
    const int selected = sm_backupMenu->get_selected();

    // Entry reference.
    const MenuEntry &entry = m_menuEntries.at(selected);

    // Config needed.
    const bool holdRequired = config::get_by_key(config::keys::HOLD_FOR_RESTORATION);
    const bool allowSystem  = config::get_by_key(config::keys::ALLOW_WRITING_TO_SYSTEM);

    // Template for confirmation string.
    const char *confirmTemplate = strings::get_by_name(strings::names::BACKUPMENU_CONFS, 1);

    // Whether or not we're working with a system type.
    const bool isSystem = BackupMenuState::user_is_system();

    // Whether or not the restoration is valid under the current conditions.
    const bool isValidRestore = !isSystem || allowSystem;
    if (!isValidRestore)
    {
        // Pop and return on trying to restore system when not enabled.
        const char *popInvalid = strings::get_by_name(strings::names::BACKUPMENU_POPS, 6);
        ui::PopMessageManager::push_message(POP_TICKS, popInvalid);
        return;
    }

    if (entry.type == MenuEntryType::Local)
    {
        // Target path we're working with.
        const fslib::Path target{m_directoryPath / m_directoryListing[entry.index]};

        // Check if it's a directory. Ensure it has contents either way.
        const bool targetIsDirectory = fslib::directory_exists(target);
        const bool backupIsGood      = targetIsDirectory ? fs::directory_has_contents(target) : fs::zip_has_contents(target);
        if (!backupIsGood)
        {
            const char *popEmpty = strings::get_by_name(strings::names::BACKUPMENU_POPS, 1);
            ui::PopMessageManager::push_message(POP_TICKS, popEmpty);
            return;
        }

        // Move the target to the struct path to pass it.
        m_dataStruct->path = std::move(target);

        // Construct our confirmation string.
        const char *targetName = m_directoryListing[entry.index].get_filename();
        std::string query      = stringutil::get_formatted_string(confirmTemplate, targetName);

        // Begin process.
        ConfirmProgress::create_push_fade(query, holdRequired, tasks::backup::restore_backup_local, nullptr, m_dataStruct);
    }
    else if (entry.type == MenuEntryType::Remote)
    {
        // Pointer to target we're working with.
        remote::Item *target = m_remoteListing[entry.index];

        // String for the confirmation
        std::string query = stringutil::get_formatted_string(confirmTemplate, target->get_name().data());

        // Set data needed.
        m_dataStruct->remoteItem = target;
        m_dataStruct->path       = m_directoryPath;

        // Pass go and collect $200.
        ConfirmProgress::create_push_fade(query, holdRequired, tasks::backup::restore_backup_remote, nullptr, m_dataStruct);
    }
}

void BackupMenuState::confirm_partida()
{
    static constexpr int POP_TICKS = ui::PopMessageManager::DEFAULT_TICKS;

    const int selected = sm_backupMenu->get_selected();
    MenuEntry entry{};
    {
        std::lock_guard menuGuard{sm_menuMutex};
        if (selected < 0 || selected >= static_cast<int>(m_menuEntries.size())) { return; }
        entry = m_menuEntries[selected];
        if (entry.type != MenuEntryType::Partida || entry.index >= static_cast<int>(m_partidas.size())) { return; }
    }

    // Igual que restaurar: no se escribe en saves del sistema salvo que este permitido.
    const bool allowSystem = config::get_by_key(config::keys::ALLOW_WRITING_TO_SYSTEM);
    if (BackupMenuState::user_is_system() && !allowSystem)
    {
        const char *popInvalid = strings::get_by_name(strings::names::BACKUPMENU_POPS, 6);
        ui::PopMessageManager::push_message(POP_TICKS, popInvalid);
        return;
    }

    // refresh() puede rearmar la lista desde el hilo de una tarea: se copia con el menu bloqueado.
    partidas::Partida partida{};
    {
        std::lock_guard menuGuard{sm_menuMutex};
        partida = m_partidas[entry.index];
    }
    m_dataStruct->url  = partida.url;
    m_dataStruct->raiz = partida.raiz;

    // Siempre manteniendo A: reemplaza la partida de este usuario (queda respaldada antes).
    std::string query = stringutil::get_formatted_string(
        strings::tr("Replace your save with \"%s\"? Your current save is backed up first.",
                    "Reemplazar tu partida por \"%s\"? Antes se respalda la que tienes."),
        partida.titulo.c_str());
    if (!partida.autor.empty())
    {
        query += stringutil::get_formatted_string(strings::tr(" (by %s)", " (de %s)"), partida.autor.c_str());
    }
    ConfirmProgress::create_push_fade(query, true, tasks::backup::restore_partida, nullptr, m_dataStruct);
}

void BackupMenuState::confirm_delete()
{
    // Grab the selected index and entry.
    const int selected     = sm_backupMenu->get_selected();
    const MenuEntry &entry = m_menuEntries.at(selected);

    // Whether or not the use desires to be forced to hold the button.
    const bool holdRequired = config::get_by_key(config::keys::HOLD_FOR_DELETION);

    // Template for the confirmation string.
    const char *confirmTemplate = strings::get_by_name(strings::names::BACKUPMENU_CONFS, 2);

    if (entry.type == MenuEntryType::Local)
    {
        // Target path to delete.
        m_dataStruct->path = m_directoryPath / m_directoryListing[entry.index];

        // This is just the name for the confirmation.
        const char *targetName = m_directoryListing[entry.index].get_filename();
        std::string query      = stringutil::get_formatted_string(confirmTemplate, targetName);

        ConfirmTask::create_push_fade(query, holdRequired, tasks::backup::delete_backup_local, nullptr, m_dataStruct);
    }
    else if (entry.type == MenuEntryType::Remote)
    {
        m_dataStruct->remoteItem = m_remoteListing.at(entry.index);
        const char *itemName     = m_dataStruct->remoteItem->get_name().data();
        std::string query        = stringutil::get_formatted_string(confirmTemplate, itemName);

        ConfirmTask::create_push_fade(query, holdRequired, tasks::backup::delete_backup_remote, nullptr, m_dataStruct);
    }
}

void BackupMenuState::upload_backup()
{
    // Sin nube no hay a donde subir: avisarlo en vez de no hacer nada.
    if (!remote::get_remote_storage())
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                            strings::tr("The cloud isn't set up: Extras > Set up the cloud.",
                                                        "La nube no está configurada: Extras › Configurar nube."));
        return;
    }
    static constexpr int POP_TICKS = ui::PopMessageManager::DEFAULT_TICKS;

    // Grab pointer to remote. Don't continue if it's not valid.
    remote::Storage *remote = remote::get_remote_storage();
    if (error::is_null(remote)) { return; }

    // Grab index, entry. If isn't local, it can't be uploaded.
    const int selected     = sm_backupMenu->get_selected();
    const MenuEntry &entry = m_menuEntries[selected];
    if (entry.type != BackupMenuState::MenuEntryType::Local) { return; }

    // Our final target.
    fslib::Path target{m_directoryPath / m_directoryListing[entry.index]};

    // If it's a directory, pop and bail. Only zip can be uploaded because it's simpler that way.
    const bool isDir = fslib::directory_exists(target);
    if (isDir)
    {
        const char *popNotZip = strings::get_by_name(strings::names::BACKUPMENU_POPS, 13);
        ui::PopMessageManager::push_message(POP_TICKS, popNotZip);
        return;
    }

    // Move our final path to our data struct.
    m_dataStruct->path = std::move(target);

    // Grab the name of the target. If it already exists, patch. If not, new upload.
    const std::string_view itemName = m_dataStruct->path.get_filename();
    const bool exists               = remote->file_exists(itemName);
    if (exists)
    {
        // This is needed, since we're patching and overwriting something.
        const bool holdRequired = config::get_by_key(config::keys::HOLD_FOR_OVERWRITE);

        // Get the target.
        remote::Item *remoteItem = remote->get_file_by_name(itemName);
        const char *queryFormat  = strings::get_by_name(strings::names::BACKUPMENU_CONFS, 0);
        const char *itemName     = remoteItem->get_name().data();

        // Confirmation string.
        std::string query = stringutil::get_formatted_string(queryFormat, itemName);

        // Push the confirmation.
        ConfirmProgress::create_push_fade(query, holdRequired, tasks::backup::patch_backup, nullptr, m_dataStruct);
    }
    else { ProgressState::create_push_fade(tasks::backup::upload_backup, m_dataStruct); }
}

void BackupMenuState::reload_remote_listing()
{
    remote::Storage *remote = remote::get_remote_storage();
    if (error::is_null(remote)) { return; }

    // Network I/O, so run it on a task thread with a spinner instead of freezing the UI.
    TaskState::create_push_fade(tasks::backup::reload_remote, m_dataStruct);
}

void BackupMenuState::open_cloud_folder_picker()
{
    CloudFolderPickerState::create_and_push(m_titleInfo, this);
}

void BackupMenuState::pop_save_empty()
{
    const int ticks      = ui::PopMessageManager::DEFAULT_TICKS;
    const char *popEmpty = strings::get_by_name(strings::names::BACKUPMENU_POPS, 0);
    ui::PopMessageManager::push_message(ticks, popEmpty);
}

void BackupMenuState::deactivate_state()
{
    sm_slidePanel->clear_elements();
    sm_slidePanel->reset();
    sm_backupMenu->reset();

    remote::Storage *remote = remote::get_remote_storage();
    if (remote) { remote->return_to_root(); }

    BaseState::deactivate();
}

void BackupMenuState::render_lista(sdl::SharedTexture &target, bool hasFocus)
{
    // Filas con su posicion vertical; una seccion nueva agrega su titulo antes.
    const int seleccionada = sm_backupMenu->get_selected();
    const int total        = static_cast<int>(m_menuEntries.size());

    std::vector<int> posiciones(total, 0);
    std::vector<int> titulos{};
    std::vector<MenuEntryType> tipoTitulo{};
    int y                = 0;
    MenuEntryType previo = MenuEntryType::Null;
    for (int i = 0; i < total; i++)
    {
        const MenuEntryType tipo = m_menuEntries[i].type;
        if (i > 0 && tipo != previo)
        {
            y += 10;
            titulos.push_back(y);
            tipoTitulo.push_back(tipo);
            y += ALTO_SECCION - 10;
        }
        posiciones[i] = y;
        y += tipo == MenuEntryType::Partida ? ALTO_FILA + 14 : ALTO_FILA;
        previo = tipo;
    }

    // Que la fila elegida quede a la vista, con un desplazamiento suave.
    if (seleccionada >= 0 && seleccionada < total)
    {
        const double arriba = posiciones[seleccionada] - 60;
        const double abajo  = posiciones[seleccionada] + ALTO_FILA + 80 - 590;
        double objetivo     = m_listaY;
        if (arriba < objetivo) { objetivo = arriba < 0 ? 0 : arriba; }
        else if (abajo > objetivo) { objetivo = abajo; }
        m_listaY += (objetivo - m_listaY) / 4.0;
    }
    const int desplaza = static_cast<int>(m_listaY);

    for (size_t t = 0; t < titulos.size(); t++)
    {
        const int ty = titulos[t] - desplaza;
        if (ty < -ALTO_SECCION || ty > 600) { continue; }
        const int locales = m_directoryListing.is_open() ? static_cast<int>(m_directoryListing.get_count()) : 0;
        const std::string texto =
            tipoTitulo[t] == MenuEntryType::Partida  ? std::string{strings::tr("$FULL SAVE$", "$PARTIDA COMPLETA$")}
            : tipoTitulo[t] == MenuEntryType::Remote ? stringutil::get_formatted_string(strings::tr("#IN THE CLOUD#  ·  %d", "#EN LA NUBE#  ·  %d"), static_cast<int>(m_remoteListing.size()))
                                                     : stringutil::get_formatted_string(strings::tr("`ON THE CONSOLE`  ·  %d", "`EN LA CONSOLA`  ·  %d"), locales);
        sdl::text::render(target, 16, ty + 8, 17, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, texto);
    }

    for (int i = 0; i < total; i++)
    {
        const MenuEntry &e = m_menuEntries[i];
        const int alto     = e.type == MenuEntryType::Partida ? ALTO_FILA + 14 : ALTO_FILA;
        const int fy       = posiciones[i] - desplaza;
        if (fy + alto < 0 || fy > 600) { continue; }

        const bool elegida = i == seleccionada;
        if (elegida)
        {
            sdl::render_rect_fill(target, 0, fy, ANCHO_LISTA, alto - 4, colors::DIALOG_DARK);
            sdl::render_rect_fill(target, 0, fy, 5, alto - 4, hasFocus ? colors::BLUE_GREEN : colors::DIV_COLOR);
        }
        else { sdl::render_line(target, 16, fy + alto - 3, ANCHO_LISTA - 16, fy + alto - 3, colors::DIV_COLOR); }

        const sdl::Color texto = elegida ? colors::BLUE_GREEN : colors::WHITE;
        if (e.type == MenuEntryType::Null)
        {
            sdl::text::render(target, 24, fy + 13, 24, sdl::text::NO_WRAP, texto, strings::tr("+  New backup", "+  Nuevo respaldo"));
            const char *pista = m_saveHasData ? strings::tr("A: create", "A: crear") : strings::tr("the save is empty", "la partida esta vacia");
            const int pw      = sdl::text::get_width(16, pista);
            sdl::text::render(target, ANCHO_LISTA - pw - 20, fy + 18, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, pista);
        }
        else if (e.type == MenuEntryType::Partida)
        {
            if (e.index < static_cast<int>(m_partidas.size()))
            {
                const partidas::Partida &p = m_partidas[e.index];
                sdl::text::render(target, 24, fy + 8, 22, sdl::text::NO_WRAP, texto, cortar(p.titulo, 22, ANCHO_LISTA - 200));
                if (!p.autor.empty())
                {
                    const std::string de = stringutil::get_formatted_string(strings::tr("by %s", "de %s"), p.autor.c_str());
                    sdl::text::render(target, 24, fy + 38, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, de);
                }
                const char *accion = strings::tr("$A: use it$", "$A: ponerla$");
                const int aw       = sdl::text::get_width(18, accion);
                sdl::text::render(target, ANCHO_LISTA - aw - 20, fy + 22, 18, sdl::text::NO_WRAP, colors::WHITE, accion);
            }
        }
        else
        {
            std::string_view nombre{};
            int64_t bytes{-1};
            const bool enNube = e.type == MenuEntryType::Remote;
            if (enNube && e.index < static_cast<int>(m_remoteListing.size()))
            {
                nombre = m_remoteListing[e.index]->get_name();
                bytes  = static_cast<int64_t>(m_remoteListing[e.index]->get_size());
            }
            else if (e.type == MenuEntryType::Local && e.index < static_cast<int>(m_directoryListing.get_count()))
            {
                nombre = m_directoryListing[e.index].get_filename();
                bytes  = m_directoryListing[e.index].is_directory() ? -1 : m_directoryListing[e.index].get_size();
            }
            // Tamano a la derecha.
            if (bytes > 0)
            {
                const std::string t = tamano_legible(bytes);
                const int tw        = sdl::text::get_width(16, t);
                sdl::text::render(target, ANCHO_LISTA - tw - 20, fy + 18, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, t);
            }
            Respaldo r = leer_nombre(nombre, m_user->get_path_safe_nickname());
            int tx     = 24;
            // En la nube: etiqueta NUBE rellena (azul); en la consola, la etiqueta del respaldo con borde.
            static constexpr sdl::Color AZUL_NUBE = {0x8EB4E6FF};
            if (enNube)
            {
                const char *n = strings::tr("CLOUD", "NUBE");
                const int ew  = sdl::text::get_width(14, n) + 16;
                sdl::render_rect_fill(target, tx, fy + 15, ew, 23, AZUL_NUBE);
                sdl::text::render(target, tx + 8, fy + 19, 14, sdl::text::NO_WRAP, colors::CLEAR_COLOR, n);
                tx += ew + 10;
            }
            if (!r.etiqueta.empty())
            {
                const sdl::Color borde = enNube ? AZUL_NUBE : colors::BLUE_GREEN;
                const int ew           = sdl::text::get_width(14, r.etiqueta) + 16;
                const int ey           = fy + 15;
                sdl::render_line(target, tx, ey, tx + ew, ey, borde);
                sdl::render_line(target, tx, ey + 22, tx + ew, ey + 22, borde);
                sdl::render_line(target, tx, ey, tx, ey + 22, borde);
                sdl::render_line(target, tx + ew, ey, tx + ew, ey + 22, borde);
                sdl::text::render(target, tx + 8, ey + 4, 14, sdl::text::NO_WRAP, borde, r.etiqueta);
                tx += ew + 16;
            }
            sdl::text::render(target, tx, fy + 15, 20, sdl::text::NO_WRAP, texto, cortar(r.fecha, 20, ANCHO_LISTA - tx - 120));
        }
    }
}
