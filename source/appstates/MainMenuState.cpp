#include "appstates/MainMenuState.hpp"
#include "fslib.hpp"
#include "json.hpp"
#include "ui/ListaSecciones.hpp"
#include "appstates/CloudHubState.hpp"
#include "appstates/CloudSetupState.hpp"
#include "partidas.hpp"
#include "nube.hpp"

#include "StateManager.hpp"
#include "appstates/ConfirmState.hpp"
#include "appstates/ExtrasMenuState.hpp"
#include "appstates/SettingsState.hpp"
#include "appstates/TextTitleSelectState.hpp"
#include "appstates/TitleSelectCommon.hpp"
#include "appstates/TitleSelectState.hpp"
#include "appstates/UpdateNoticeState.hpp"
#include "appstates/UserOptionState.hpp"
#include "config/config.hpp"
#include "data/data.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "remote/remote.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"
#include "sys/sys.hpp"
#include "tasks/mainmenu.hpp"
#include "tasks/update.hpp"
#include "ui/PopMessageManager.hpp"

#include <algorithm>
#include <string_view>

//                      ---- Construction ----

MainMenuState::MainMenuState()
    : m_renderTarget(sdl::TextureManager::load("mainRailTarget2", 140, 700, SDL_TEXTUREACCESS_TARGET))
    , m_background(sdl::TextureManager::load("mainBackground", "romfs:/Textures/MenuBackground.png"))
    , m_settingsIcon(sdl::TextureManager::load("gearGlyph", "romfs:/Textures/GearGlyph.png"))
    , m_extrasIcon(sdl::TextureManager::load("extrasGlyph", "romfs:/Textures/ExtrasGlyph.png"))
    , m_cloudIcon(sdl::TextureManager::load("cloudGlyphRail", "romfs:/Textures/CloudGlyph.png"))
    , m_mainMenu(ui::IconMenu::create(0, 14, 700))
    , m_controlGuide(ui::ControlGuide::create(strings::tr("\uE0E0 Enter    \uE0E3 Back up everything    \uE0E2 User options",
                                                          "\uE0E0 Entrar    \uE0E3 Respaldar todo    \uE0E2 Opciones de usuario")))
    , m_dataStruct(std::make_shared<MainMenuState::DataStruct>())
{
    MainMenuState::initialize_settings_extras();
    MainMenuState::initialize_menu();
    MainMenuState::initialize_view_states();
    MainMenuState::initialize_data_struct();
    MainMenuState::check_for_update();
}

//                      ---- Public functions ----

void MainMenuState::update()
{
    const int selected  = m_mainMenu->get_selected();
    const bool hasFocus = BaseState::has_focus();
    const bool aPressed = input::button_pressed(HidNpadButton_A);
    const bool xPressed = input::button_pressed(HidNpadButton_X);
    const bool yPressed = input::button_pressed(HidNpadButton_Y);

    const bool toUserOptions = xPressed && selected < sm_userCount;

    if (aPressed) { MainMenuState::push_target_state(); }
    else if (toUserOptions) { MainMenuState::create_user_options(); }
    else if (yPressed) { MainMenuState::backup_all_for_all(); }
    else if (m_updateFound.load())
    {
        m_updateFound.store(false);
        MainMenuState::confirm_update();
    }

    m_mainMenu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    for (auto &state : sm_states) { state->sub_update(); }
}

void MainMenuState::sub_update() { m_controlGuide->sub_update(); }

void MainMenuState::render()
{
    const bool hasFocus = BaseState::has_focus();
    const int selected  = m_mainMenu->get_selected();

    // Rediseno: la columna de usuarios va a toda la altura (su fondo lo pinta JKSV::render_base).
    m_renderTarget->clear(colors::PANEL_SOLID);
    m_mainMenu->render(m_renderTarget, hasFocus);
    m_renderTarget->render(sdl::Texture::Null, 0, 10);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);

    if (hasFocus)
    {
        BaseState *target = sm_states[selected].get();
        if (target == sm_cloudState.get())
        {
            // La pantalla de la nube ocupa todo: al pasar por su boton se muestra una tarjeta con la conexion y
            // los ajustes que mas importan. Los datos se leen de la SD una vez cada vez que se llega al boton.
            const bool conectada = remote::get_remote_storage() != nullptr;
            if (!m_nubeLeida)
            {
                m_nubeLeida = true;
                m_nubeServicio.clear();
                m_nubeServidor.clear();
                m_nubeCarpeta.clear();
                m_nubeUsuario.clear();
                json::Object raiz = json::new_object(json_object_from_file, "sdmc:/config/JKSV/webdav.json");
                if (raiz)
                {
                    auto campo = [&](const char *clave) -> std::string {
                        json_object *o = json::get_object(raiz, clave);
                        return o ? std::string{json_object_get_string(o)} : std::string{};
                    };
                    std::string origen = campo("origin");
                    m_nubeCarpeta      = campo("basepath");
                    m_nubeUsuario      = campo("username");
                    if (origen.rfind("https://", 0) == 0) { origen.erase(0, 8); }
                    else if (origen.rfind("http://", 0) == 0) { origen.erase(0, 7); }
                    m_nubeServidor = origen;
                    m_nubeServicio = origen.find("koofr") != std::string::npos ? "Koofr (WebDAV)" : "WebDAV";
                }
                else if (fslib::file_exists(remote::PATH_GOOGLE_DRIVE_CONFIG)) { m_nubeServicio = "Google Drive"; }

                // Favoritos de todos los usuarios, sin repetir; el respaldo mas nuevo sale del nombre de cada
                // respaldo en la carpeta de trabajo. Nada de red: la nube se mira dentro de su pantalla.
                m_nubeFavoritos.clear();
                m_nubeConRespaldo = 0;
                std::vector<uint64_t> vistos{};
                for (data::User *user : sm_users)
                {
                    const int total = static_cast<int>(user->get_total_data_entries());
                    for (int i = 0; i < total; i++)
                    {
                        const uint64_t id = user->get_application_id_at(i);
                        if (!config::is_favorite(id) || std::find(vistos.begin(), vistos.end(), id) != vistos.end()) { continue; }
                        vistos.push_back(id);
                        data::TitleInfo *info = data::get_title_info_by_id(id);
                        if (!info) { continue; }

                        FavoritoNube fav{};
                        fav.nombre = info->get_title();
                        const fslib::Path dir{config::get_working_directory() / info->get_path_safe_title()};
                        if (fslib::directory_exists(dir))
                        {
                            fslib::Directory listado{dir, false};
                            const int64_t n = listado.is_open() ? listado.get_count() : 0;
                            for (int64_t j = 0; j < n; j++)
                            {
                                std::string_view nombre = listado[j].get_filename();
                                if (nombre.size() > 4 && nombre.substr(nombre.size() - 4) == ".zip") { nombre.remove_suffix(4); }
                                if (nombre.size() < 19) { continue; }
                                const std::string clave{nombre.substr(nombre.size() - 19)};
                                if (clave[4] == '-' && clave[10] == '_' && clave > fav.fecha) { fav.fecha = clave; }
                            }
                        }
                        if (!fav.fecha.empty()) { m_nubeConRespaldo++; }
                        m_nubeFavoritos.push_back(std::move(fav));
                    }
                }
                // Los de respaldo mas nuevo primero; los que no tienen, al final.
                std::stable_sort(m_nubeFavoritos.begin(), m_nubeFavoritos.end(),
                                 [](const FavoritoNube &a, const FavoritoNube &b) { return a.fecha > b.fecha; });
            }

            constexpr int CX = 176, CY = 124, CW = 1068;
            sdl::render_rect_fill(sdl::Texture::Null, CX, CY, CW, 420, colors::DIALOG_DARK);
            sdl::render_rect_fill(sdl::Texture::Null, CX, CY, 5, 420, conectada ? colors::BLUE_GREEN : colors::DIV_COLOR);
            sdl::text::render(sdl::Texture::Null, CX + 34, CY + 24, 30, sdl::text::NO_WRAP, colors::WHITE, strings::tr("Cloud", "Nube"));
            {
                const char *estado = conectada ? strings::tr("Connected", "Conectada") : strings::tr("Not set up", "Sin configurar");
                const int ew       = sdl::text::get_width(17, estado) + 32;
                ui::lista::pildora(sdl::Texture::Null, CX + CW - ew - 24, CY + 26, ew, 32, conectada ? colors::BLUE_GREEN : colors::PANEL_SOLID);
                sdl::text::render(sdl::Texture::Null, CX + CW - ew - 8, CY + 33, 17, sdl::text::NO_WRAP, conectada ? colors::CLEAR_COLOR : colors::WHITE, estado);
            }

            // Izquierda: la conexion y sus ajustes (o como conectarla). Derecha: los favoritos, vista rapida.
            constexpr int COL     = CX + 560;  // columna de favoritos
            constexpr int COL_FIN = CX + CW - 34;
            sdl::render_line(sdl::Texture::Null, COL - 26, CY + 86, COL - 26, CY + 420 - 64, colors::DIV_COLOR);

            int fy     = CY + 86;
            auto fila  = [&](const char *etiqueta, const std::string &valor, sdl::Color color) {
                sdl::text::render(sdl::Texture::Null, CX + 34, fy, 18, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, etiqueta);
                const std::vector<std::string> v = ui::lista::partir(valor, 18, COL - 46 - (CX + 300), 1);
                if (!v.empty()) { sdl::text::render(sdl::Texture::Null, CX + 300, fy, 18, sdl::text::NO_WRAP, color, v[0]); }
                fy += 34;
            };
            if (!m_nubeServicio.empty())
            {
                fila(strings::tr("Service", "Servicio"), m_nubeServicio, colors::WHITE);
                if (!m_nubeServidor.empty()) { fila(strings::tr("Server", "Servidor"), m_nubeServidor, colors::WHITE); }
                if (!m_nubeCarpeta.empty()) { fila(strings::tr("Folder", "Carpeta"), m_nubeCarpeta, colors::WHITE); }
                if (!m_nubeUsuario.empty()) { fila(strings::tr("User", "Usuario"), m_nubeUsuario, colors::WHITE); }

                fy += 4;
                sdl::render_line(sdl::Texture::Null, CX + 34, fy, COL - 52, fy, colors::DIV_COLOR);
                fy += 16;
                // Lo de la seccion Nube: que falta sincronizar y cuanto hay alla (de la ultima consulta, sin red aqui).
                const nube::Resumen r = nube::resumen();
                if (r.listo && r.ok)
                {
                    const int pendientes = r.subir + r.bajar + r.conflictos;
                    fila(strings::tr("Favorites", "Favoritos"),
                         pendientes == 0 ? std::string{strings::tr("All up to date", "Todos al día")}
                                         : std::to_string(r.subir) + strings::tr(" to upload · ", " por subir · ") + std::to_string(r.bajar + r.conflictos) +
                                               strings::tr(" to download", " por bajar"),
                         pendientes == 0 ? colors::WHITE : colors::BLUE_GREEN);
                    fila(strings::tr("In the cloud", "En la nube"),
                         nube::tamano_legible(r.bytes) + strings::tr(" in ", " en ") + std::to_string(r.juegos) + strings::tr(" games", " juegos"), colors::WHITE);
                }
                else
                {
                    fila(strings::tr("Favorites", "Favoritos"),
                         nube::consultando() ? std::string{strings::tr("Checking…", "Consultando…")}
                                             : std::string{strings::tr("Open to see what's pending", "Entra para ver qué falta")},
                         colors::DIALOG_LIGHT);
                }
                const bool alSalir = config::get_by_key(config::keys::SUBIR_AL_SALIR) != 0;
                fila(strings::tr("Upload on exit", "Subir al salir"), alSalir ? strings::tr("On", "Encendido") : strings::tr("Off", "Apagado"),
                     alSalir ? colors::BLUE_GREEN : colors::DIALOG_LIGHT);
            }
            else
            {
                const std::vector<std::string> txt = ui::lista::partir(
                    strings::tr("Keep your saves safe online with Koofr (free 10 GB), Nextcloud, another WebDAV server or Google Drive.",
                                "Ten tus partidas a salvo en internet con Koofr (10 GB gratis), Nextcloud, otro servidor WebDAV o Google Drive."),
                    18, COL - 52 - (CX + 34), 3);
                for (const std::string &l : txt)
                {
                    sdl::text::render(sdl::Texture::Null, CX + 34, fy, 18, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, l);
                    fy += 26;
                }
                fy += 14;
                const char *pasos[] = {
                    strings::tr("\uE0E0 Open and choose Koofr (recommended)", "\uE0E0 Abrir y elegir Koofr (recomendado)"),
                    strings::tr("Type your email and the app password from koofr.net", "Escribe tu correo y la contraseña de aplicación de koofr.net"),
                    strings::tr("Turn on Upload on backup: each backup goes up by itself", "Enciende Subir al respaldar: cada respaldo se sube solo"),
                };
                for (int p = 0; p < 3; p++)
                {
                    ui::lista::pildora(sdl::Texture::Null, CX + 34, fy - 2, 28, 28, colors::PANEL_SOLID);
                    const std::string num = std::to_string(p + 1);
                    sdl::text::render(sdl::Texture::Null, CX + 48 - sdl::text::get_width(16, num) / 2, fy + 2, 16, sdl::text::NO_WRAP, colors::BLUE_GREEN, num);
                    const std::vector<std::string> l = ui::lista::partir(pasos[p], 17, COL - 52 - (CX + 76), 2);
                    for (const std::string &linea : l)
                    {
                        sdl::text::render(sdl::Texture::Null, CX + 76, fy, 17, sdl::text::NO_WRAP, colors::WHITE, linea);
                        fy += 24;
                    }
                    fy += 12;
                }
            }

            // Favoritos: cuantos hay, cuantos tienen respaldo en la consola y los mas recientes.
            {
                int gy = CY + 86;
                sdl::text::render(sdl::Texture::Null, COL, gy, 15, sdl::text::NO_WRAP, colors::BLUE_GREEN, strings::tr("FAVORITES", "FAVORITOS"));
                gy += 28;
                const int total = static_cast<int>(m_nubeFavoritos.size());
                if (total == 0)
                {
                    const std::vector<std::string> l = ui::lista::partir(
                        strings::tr("No favorites yet. Press \uE0E3 on a game to mark it: favorites can be uploaded and downloaded together.",
                                    "Aún no hay favoritos. Pulsa \uE0E3 en un juego para marcarlo: los favoritos se suben y se bajan juntos."),
                        17, COL_FIN - COL, 4);
                    for (const std::string &linea : l)
                    {
                        sdl::text::render(sdl::Texture::Null, COL, gy, 17, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, linea);
                        gy += 25;
                    }
                }
                else
                {
                    const std::string resumen = std::to_string(total) + (total == 1 ? strings::tr(" favorite", " favorito") : strings::tr(" favorites", " favoritos")) +
                                                "  ·  " + std::to_string(m_nubeConRespaldo) + strings::tr(" backed up", " con respaldo");
                    const std::vector<std::string> r = ui::lista::partir(resumen, 18, COL_FIN - COL, 1);
                    if (!r.empty()) { sdl::text::render(sdl::Texture::Null, COL, gy, 18, sdl::text::NO_WRAP, colors::WHITE, r[0]); }
                    gy += 38;

                    constexpr int MAX_FILAS = 6;
                    const int mostrar       = total > MAX_FILAS ? MAX_FILAS - 1 : total;
                    for (int i = 0; i < mostrar; i++)
                    {
                        const FavoritoNube &f = m_nubeFavoritos[i];
                        // dd/mm hh:mm del respaldo mas nuevo, o "sin respaldo".
                        const std::string fecha = f.fecha.empty() ? std::string{strings::tr("no backup", "sin respaldo")}
                                                                  : f.fecha.substr(8, 2) + "/" + f.fecha.substr(5, 2) + "  " +
                                                                        f.fecha.substr(11, 2) + ":" + f.fecha.substr(14, 2);
                        const int fw = sdl::text::get_width(16, fecha);
                        sdl::text::render(sdl::Texture::Null, COL_FIN - fw, gy + 1, 16, sdl::text::NO_WRAP,
                                          colors::DIALOG_LIGHT, fecha);
                        const std::vector<std::string> n = ui::lista::partir(f.nombre, 17, COL_FIN - COL - fw - 20, 1);
                        if (!n.empty()) { sdl::text::render(sdl::Texture::Null, COL, gy, 17, sdl::text::NO_WRAP, colors::WHITE, n[0]); }
                        gy += 31;
                    }
                    if (total > mostrar)
                    {
                        const std::string mas = strings::tr("and ", "y ") + std::to_string(total - mostrar) + strings::tr(" more", " más");
                        sdl::text::render(sdl::Texture::Null, COL, gy, 16, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, mas);
                    }
                }
            }

            sdl::text::render(sdl::Texture::Null, CX + 34, CY + 420 - 44, 18, sdl::text::NO_WRAP, colors::BLUE_GREEN,
                              m_nubeServicio.empty() ? strings::tr("\uE0E0 Open to connect", "\uE0E0 Abrir para conectar")
                                                     : strings::tr("\uE0E0 Open: sync, games in the cloud, test connection",
                                                                   "\uE0E0 Abrir: sincronizar, juegos en la nube, probar conexión"));
        }
        else
        {
            m_nubeLeida = false;
            target->render();
        }

    }
}

void MainMenuState::signal_update_found() { m_updateFound.store(true); }

void MainMenuState::initialize_view_states()
{
    const bool jksmMode = config::get_by_key(config::keys::JKSM_TEXT_MODE);

    sm_states.clear();
    for (data::User *user : sm_users)
    {
        std::shared_ptr<BaseState> state{};

        if (jksmMode) { state = TextTitleSelectState::create(user); }
        else { state = TitleSelectState::create(user); }

        sm_states.push_back(state);
    }
    sm_states.push_back(sm_settingsState);
    sm_states.push_back(sm_cloudState);
    sm_states.push_back(sm_extrasState);
}

void MainMenuState::refresh_view_states()
{
    for (int i = 0; i < sm_userCount; i++)
    {
        TitleSelectCommon *target = static_cast<TitleSelectCommon *>(sm_states.at(i).get());
        target->refresh();
    }
}

//                      ---- Private functions ----

void MainMenuState::initialize_settings_extras()
{
    if (!sm_settingsState || !sm_extrasState || !sm_cloudState)
    {
        sm_settingsState = SettingsState::create();
        sm_extrasState   = ExtrasMenuState::create();
        sm_cloudState    = CloudHubState::create(); // la seccion Nube; Extras sigue abriendo Configurar nube
    }
}

void MainMenuState::initialize_menu()
{
    data::get_users(sm_users);
    sm_userCount = sm_users.size();
    for (data::User *user : sm_users) { m_mainMenu->add_option(user->get_icon(), user->get_nickname()); }
    m_mainMenu->add_option(m_settingsIcon, strings::tr("Settings", "Ajustes"), true);
    m_mainMenu->add_option(m_cloudIcon, strings::tr("Cloud", "Nube"), true);
    m_mainMenu->add_option(m_extrasIcon, strings::tr("Extras", "Extras"), true);

    // Abrir en el perfil con el que se entro, como Checkpoint 5.3: el que se eligio al abrir JKSV desde un
    // juego o, desde el Album, el ultimo que abrio un juego. Antes siempre quedaba el primero.
    AccountUid preferido{};
    bool hayPreferido = R_SUCCEEDED(accountGetPreselectedUser(&preferido)) && accountUidIsValid(&preferido);
    if (!hayPreferido) { hayPreferido = R_SUCCEEDED(accountGetLastOpenedUser(&preferido)) && accountUidIsValid(&preferido); }
    for (int i = 0; hayPreferido && i < sm_userCount; i++)
    {
        const AccountUid uid = sm_users[i]->get_account_id();
        if (uid.uid[0] == preferido.uid[0] && uid.uid[1] == preferido.uid[1])
        {
            m_mainMenu->set_selected(i);
            break;
        }
    }
}

void MainMenuState::initialize_data_struct()
{
    m_dataStruct->userList      = sm_users;
    m_dataStruct->spawningState = this;
}

void MainMenuState::check_for_update()
{
    sys::threadpool::push_job(tasks::update::check_for_update, m_dataStruct);
    // Catalogo de partidas completas de OQB, en segundo plano.
    sys::threadpool::push_job(partidas::cargar, m_dataStruct);
}

void MainMenuState::push_target_state()
{
    const int popTicks          = ui::PopMessageManager::DEFAULT_TICKS;
    const char *popNoSaveFormat = strings::get_by_name(strings::names::MAINMENU_POPS, 0);

    const int selected  = m_mainMenu->get_selected();
    const int userCount = sm_users.size();
    if (selected < userCount)
    {
        const data::User *user = sm_users[selected];
        const int titleCount   = user->get_total_data_entries();
        if (titleCount <= 0)
        {
            const char *nickname = user->get_nickname();
            std::string popError = stringutil::get_formatted_string(popNoSaveFormat, nickname);
            ui::PopMessageManager::push_message(popTicks, popError);
            return;
        }
    }

    // Ajustes, Nube y Extras van despues de los usuarios, en ese orden (como en initialize_menu): se eligen por
    // su posicion y no por el vector de estados, para que A en Nube abra siempre la Nube.
    std::shared_ptr<BaseState> target{};
    if (selected < userCount) { target = sm_states[selected]; }
    else
    {
        switch (selected - userCount)
        {
            case 0:  target = sm_settingsState; break;
            case 1:  target = sm_cloudState; break;
            default: target = sm_extrasState; break;
        }
    }
    if (!target) { return; }
    target->reactivate();
    StateManager::push_state(target);
    m_mainMenu->play_sound();
}

void MainMenuState::create_user_options()
{
    const int selected             = m_mainMenu->get_selected();
    data::User *user               = sm_users[selected];
    TitleSelectCommon *titleSelect = static_cast<TitleSelectCommon *>(sm_states[selected].get());

    UserOptionState::create_and_push(user, titleSelect);
}

void MainMenuState::backup_all_for_all()
{
    remote::Storage *remote = remote::get_remote_storage();
    const bool autoUpload   = config::get_by_key(config::keys::AUTO_UPLOAD);
    const char *query       = strings::get_by_name(strings::names::MAINMENU_CONFS, 0);
    if (remote && autoUpload)
    {
        ConfirmProgress::create_push_fade(query, true, tasks::mainmenu::backup_all_for_all_remote, nullptr, m_dataStruct);
    }
    else { ConfirmProgress::create_push_fade(query, true, tasks::mainmenu::backup_all_for_all_local, nullptr, m_dataStruct); }
}

void MainMenuState::confirm_update()
{
    // Como en OQB Updater: «Actualizar ahora» o «Más tarde». Sale una vez por arranque.
    UpdateNoticeState::create_push_fade();
}
