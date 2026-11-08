#include "appstates/CloudHubState.hpp"

#include "appstates/CloudDashboardState.hpp"
#include "appstates/CloudGamesState.hpp"
#include "appstates/CloudSetupState.hpp"
#include "appstates/ConfirmState.hpp"
#include "appstates/MainMenuState.hpp"
#include "appstates/ProgressState.hpp"
#include "config/config.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "nube.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "tasks/backup.hpp"
#include "tasks/mainmenu.hpp"
#include "ui/ListaSecciones.hpp"
#include "ui/PopMessageManager.hpp"

#include <string>
#include <vector>

namespace
{
    enum Accion
    {
        SINCRONIZAR,
        SUBIR_FAV,
        BAJAR_FAV,
        JUEGOS,
        ESTADO_FAV,
        PROBAR,
        CONECTAR,
        SUBIR_SALIR,
        RESTAURAR
    };

    struct Opcion
    {
        int accion;
        int seccion;
        const char *en, *es, *descEn, *descEs;
    };

    constexpr Opcion ORDEN[] = {
        {SINCRONIZAR, 0, "Sync favorites", "Sincronizar favoritos",
         "Uploads the favorites you played here and brings down the ones you played on another console. If both changed, the newest save wins; a PRE-SYNC copy is kept before anything is replaced.",
         "Sube los favoritos que jugaste aquí y baja los que jugaste en otra consola. Si cambiaron en los dos lados, gana la partida más nueva; antes de reemplazar algo se guarda una copia PRE-SYNC."},
        {SUBIR_FAV, 0, "Upload favorites", "Subir favoritos", "Makes a new backup of every favorite and uploads it, changed or not.",
         "Hace un respaldo nuevo de cada favorito y lo sube, haya cambiado o no."},
        {BAJAR_FAV, 0, "Download favorites", "Bajar favoritos",
         "Brings the newest cloud backup of every favorite. Skips the ones whose save here is newer. A PRE-SYNC copy is kept first.",
         "Trae el respaldo más nuevo de la nube de cada favorito. Se salta los que aquí tienen la partida más nueva. Antes se guarda una copia PRE-SYNC."},
        {JUEGOS, 1, "Games in the cloud", "Juegos en la nube",
         "Everything in your cloud, not only favorites: backups per game and their size. Download one to the console, delete it, or keep only the newest.",
         "Todo lo que hay en tu nube, no solo favoritos: respaldos por juego y cuánto pesan. Baja uno a la consola, bórralo o deja solo los más nuevos."},
        {ESTADO_FAV, 1, "Favorites, game by game", "Favoritos, juego por juego", "For each favorite: the newest backup on the console and in the cloud.",
         "Por cada favorito: el respaldo más nuevo en la consola y en la nube."},
        {PROBAR, 2, "Test connection", "Probar conexión", "Checks the connection to your cloud and says what's wrong if it fails.",
         "Comprueba la conexión con tu nube y, si falla, dice qué puede estar pasando."},
        {CONECTAR, 2, "Connect or change cloud", "Conectar o cambiar de nube", "Koofr, Nextcloud, another WebDAV server or Google Drive, and the settings for each.",
         "Koofr, Nextcloud, otro servidor WebDAV o Google Drive, y los ajustes de cada uno."},
        {SUBIR_SALIR, 2, "Upload when closing JKSV", "Subir al salir de JKSV",
         "When you close JKSV with +, it uploads the favorites you played (only the ones that changed) and then closes. Closing with HOME skips it.",
         "Al cerrar JKSV con +, sube los favoritos que jugaste (solo los que cambiaron) y luego se cierra. Si cierras con HOME no se sube."},
        {RESTAURAR, 3, "Restore everything from the cloud", "Restaurar todo desde la nube",
         "For a new or formatted console: restores the newest backup of every installed game, creating its save if needed.",
         "Para una consola nueva o formateada: restaura el respaldo más nuevo de cada juego instalado y crea su partida si hace falta."},
    };
    constexpr int TOTAL = sizeof(ORDEN) / sizeof(ORDEN[0]);

    constexpr int RESERVA = 236; // alto del estado arriba del panel de la derecha

    bool sin_nube()
    {
        if (remote::get_remote_storage()) { return false; }
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS * 2,
                                            nube::hay_configuracion()
                                                ? strings::tr("Your cloud isn't connected. Try Test connection.", "Tu nube no está conectada. Prueba con Probar conexión.")
                                                : strings::tr("Connect a cloud first, in Connect or change cloud.",
                                                              "Primero conecta una nube, en Conectar o cambiar de nube."));
        return true;
    }

    bool esperando()
    {
        if (!nube::consultando()) { return false; }
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                            strings::tr("Checking the cloud, one moment…", "Consultando la nube, un momento…"));
        return true;
    }
}

CloudHubState::CloudHubState()
    : m_menu(ui::Menu::create(32, 10, 1000, 23, 555))
    , m_controlGuide(ui::ControlGuide::create(strings::tr(" Choose     Refresh     Back",
                                                          " Elegir     Actualizar     Atrás")))
{
    for (const Opcion &o : ORDEN) { m_menu->add_option(strings::tr(o.en, o.es)); }
}

void CloudHubState::update()
{
    const bool hasFocus = BaseState::has_focus();

    // Al entrar (o al volver de otra pantalla) se consulta la nube si lo que hay quedo viejo.
    if (hasFocus && !m_teniaFoco && remote::get_remote_storage() && !nube::consultando() && !nube::vigente()) { nube::consultar(false); }
    m_teniaFoco = hasFocus;

    m_menu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    const int fila = m_menu->get_selected();
    if (input::button_pressed(HidNpadButton_A) && fila >= 0 && fila < TOTAL) { CloudHubState::elegir(ORDEN[fila].accion); }
    else if (input::button_pressed(HidNpadButton_Y))
    {
        if (!esperando() && !sin_nube()) { nube::consultar(false); }
    }
    else if (input::button_pressed(HidNpadButton_B)) { BaseState::deactivate(); }
}

void CloudHubState::sub_update() { m_controlGuide->sub_update(); }

void CloudHubState::render()
{
    const bool hasFocus        = BaseState::has_focus();
    const bool conectada       = remote::get_remote_storage() != nullptr;
    const nube::Resumen r      = nube::resumen();
    const bool hayConfig       = nube::hay_configuracion();
    const char *estado         = conectada ? strings::tr("Connected", "Conectada")
                                 : hayConfig ? strings::tr("Not connecting", "Sin conexión")
                                             : strings::tr("Not set up", "Sin configurar");
    ui::lista::marco(strings::tr("Cloud", "Nube"), estado, conectada);

    std::vector<ui::lista::Fila> filas{};
    for (const Opcion &o : ORDEN)
    {
        ui::lista::Fila f{};
        f.seccion     = o.seccion == 0   ? strings::tr("SYNC", "SINCRONIZAR")
                        : o.seccion == 1 ? strings::tr("YOUR CLOUD", "TU NUBE")
                        : o.seccion == 2 ? strings::tr("CONNECTION", "CONEXIÓN")
                                         : strings::tr("ADVANCED", "AVANZADO");
        f.titulo      = strings::tr(o.en, o.es);
        f.descripcion = strings::tr(o.descEn, o.descEs);
        const bool hayDatos = r.listo && r.ok;
        switch (o.accion)
        {
            case SINCRONIZAR:
                if (hayDatos)
                {
                    const int pendientes = r.subir + r.bajar + r.conflictos;
                    f.valor              = pendientes == 0 ? std::string{strings::tr("Up to date", "Al día")}
                                                           : std::to_string(pendientes) + strings::tr(" pending", " pendientes");
                    f.valorActivo        = pendientes > 0;
                }
                break;
            case JUEGOS:
                if (hayDatos) { f.valor = std::to_string(r.juegos) + strings::tr(" games", " juegos"); }
                break;
            case PROBAR:
                f.valor       = conectada ? strings::tr("Connected", "Conectada") : strings::tr("Not connected", "Sin conexión");
                f.valorActivo = conectada;
                break;
            case SUBIR_SALIR:
            {
                const bool on = config::get_by_key(config::keys::SUBIR_AL_SALIR) != 0;
                f.valor       = on ? strings::tr("On", "Encendido") : strings::tr("Off", "Apagado");
                f.valorActivo = on;
            }
            break;
            default: break;
        }
        filas.push_back(std::move(f));
    }
    ui::lista::dibujar(filas, m_menu->get_selected(), m_desplazamiento, hasFocus, RESERVA);
    CloudHubState::dibujar_estado();
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

void CloudHubState::dibujar_estado()
{
    using ui::lista::PANEL_ANCHO;
    using ui::lista::PANEL_X;
    using ui::lista::PANEL_Y;
    constexpr int X     = PANEL_X + 28;
    constexpr int ANCHO = PANEL_ANCHO - 56;

    sdl::render_rect_fill(sdl::Texture::Null, PANEL_X, PANEL_Y, PANEL_ANCHO, RESERVA, colors::SLIDE_PANEL_CLEAR);
    sdl::render_line(sdl::Texture::Null, PANEL_X, PANEL_Y, PANEL_X, PANEL_Y + RESERVA, colors::DIV_COLOR);
    sdl::render_line(sdl::Texture::Null, X, PANEL_Y + RESERVA - 4, X + ANCHO, PANEL_Y + RESERVA - 4, colors::DIV_COLOR);

    int y = PANEL_Y + 22;
    sdl::text::render(sdl::Texture::Null, X, y, 15, sdl::text::NO_WRAP, colors::BLUE_GREEN, strings::tr("YOUR CLOUD NOW", "TU NUBE AHORA"));
    y += 30;

    auto parrafo = [&](std::string_view texto, int size, sdl::Color color, int lineas) {
        for (const std::string &l : ui::lista::partir(texto, size, ANCHO, lineas))
        {
            sdl::text::render(sdl::Texture::Null, X, y, size, sdl::text::NO_WRAP, color, l);
            y += size + 8;
        }
    };

    const nube::Resumen r = nube::resumen();
    if (!remote::get_remote_storage() && !nube::hay_configuracion())
    {
        parrafo(strings::tr("Not set up", "Sin configurar"), 22, colors::WHITE, 1);
        y += 4;
        parrafo(strings::tr("Choose Connect or change cloud to start. Koofr gives 10 GB free.",
                            "Elige Conectar o cambiar de nube para empezar. Koofr da 10 GB gratis."),
                17, colors::DIALOG_LIGHT, 4);
        return;
    }
    if (nube::consultando() && !(r.listo && r.ok))
    {
        parrafo(strings::tr("Checking the cloud…", "Consultando la nube…"), 22, colors::WHITE, 1);
        y += 4;
        parrafo(strings::tr("It reads the list of your backups once; it takes a few seconds.",
                            "Se lee la lista de tus respaldos una vez; tarda unos segundos."),
                17, colors::DIALOG_LIGHT, 3);
        return;
    }
    if (r.listo && !r.ok)
    {
        parrafo(strings::tr("Couldn't check", "No se pudo consultar"), 22, colors::WHITE, 1);
        y += 4;
        parrafo(r.error, 16, colors::DIALOG_LIGHT, 5);
        return;
    }
    if (!r.listo)
    {
        parrafo(strings::tr("Press  to check your cloud.", "Pulsa  para consultar tu nube."), 18, colors::DIALOG_LIGHT, 2);
        return;
    }

    // Tres cifras: por subir, por bajar, al dia (de los favoritos).
    const int cifras[3]       = {r.subir, r.bajar + r.conflictos, r.alDia};
    const char *etiquetas[3]  = {strings::tr("to upload", "por subir"), strings::tr("to download", "por bajar"), strings::tr("up to date", "al día")};
    constexpr int COLUMNA     = ANCHO / 3;
    for (int i = 0; i < 3; i++)
    {
        const std::string n = std::to_string(cifras[i]);
        const bool resalta  = i < 2 && cifras[i] > 0;
        sdl::text::render(sdl::Texture::Null, X + i * COLUMNA, y, 38, sdl::text::NO_WRAP, resalta ? colors::BLUE_GREEN : colors::WHITE, n);
        sdl::text::render(sdl::Texture::Null, X + i * COLUMNA, y + 46, 15, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, etiquetas[i]);
    }
    y += 80;

    if (r.conflictos > 0)
    {
        const std::string t = std::to_string(r.conflictos) +
                              strings::tr(" changed on both sides: the newest wins, with a PRE-SYNC copy.",
                                          " cambiaron en los dos lados: gana la más nueva, con copia PRE-SYNC.");
        parrafo(t, 15, colors::WHITE, 2);
    }
    else if (r.favoritos == 0)
    {
        parrafo(strings::tr("No favorites yet: press  on a game to mark it.", "Aún no hay favoritos: pulsa  en un juego para marcarlo."), 15,
                colors::DIALOG_LIGHT, 2);
    }
    if (!r.ultimaClave.empty())
    {
        const std::string fecha = nube::fecha_legible(r.ultimaClave);
        parrafo(strings::tr("Latest: ", "Lo último: ") + r.ultimoJuego + "  ·  " + fecha.substr(0, 5), 15, colors::DIALOG_LIGHT, 1);
    }
    parrafo(nube::tamano_legible(r.bytes) + strings::tr(" in ", " en ") + std::to_string(r.juegos) + strings::tr(" games", " juegos") +
                (nube::consultando() ? strings::tr("  ·  updating…", "  ·  actualizando…") : ""),
            15, colors::DIALOG_LIGHT, 1);
}

void CloudHubState::elegir(int accion)
{
    switch (accion)
    {
        case SINCRONIZAR:
        case SUBIR_FAV:
        case BAJAR_FAV:
        case RESTAURAR:
        {
            if (esperando() || sin_nube()) { return; }
            if (accion == SINCRONIZAR)
            {
                auto datos = std::make_shared<nube::DatosSync>();
                data::get_users(datos->usuarios);
                ConfirmProgress::create_push_fade(
                    strings::tr("Sync your favorites? What you played here goes up and what you played on another console comes down. A PRE-SYNC copy is kept before replacing a save.",
                                "¿Sincronizar tus favoritos? Lo que jugaste aquí se sube y lo que jugaste en otra consola se baja. Antes de reemplazar una partida se guarda una copia PRE-SYNC."),
                    true, tasks::backup::sync_favorites_remote, nullptr, datos);
                return;
            }
            auto datos = std::make_shared<MainMenuState::DataStruct>();
            data::get_users(datos->userList);
            nube::invalidar();
            if (accion == SUBIR_FAV)
            {
                ConfirmProgress::create_push_fade(strings::tr("Upload a new backup of every favorite?", "¿Subir un respaldo nuevo de cada favorito?"), false,
                                                  tasks::mainmenu::upload_favorites_remote, nullptr, datos);
            }
            else if (accion == BAJAR_FAV)
            {
                ConfirmProgress::create_push_fade(
                    strings::tr("Replace your favorites with the newest cloud backup? A PRE-SYNC copy is kept first; the ones newer here are skipped.",
                                "¿Reemplazar tus favoritos con el respaldo más nuevo de la nube? Antes se guarda una copia PRE-SYNC; se saltan los que aquí son más nuevos."),
                    true, tasks::backup::download_favorites_remote, nullptr, datos);
            }
            else
            {
                ConfirmProgress::create_push_fade(
                    strings::tr("Restore your ENTIRE cloud on this console? Creates saves for installed games you never opened and REPLACES existing ones (with a PRE-SYNC copy).",
                                "¿Restaurar TODA tu nube en esta consola? Crea las partidas de juegos instalados que nunca abriste y REEMPLAZA las que existen (con copia PRE-SYNC)."),
                    true, tasks::backup::restore_all_from_cloud, nullptr, datos);
            }
            return;
        }
        case JUEGOS:
            if (esperando() || sin_nube()) { return; }
            if (!nube::resumen().ok)
            {
                nube::consultar(false);
                ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                                    strings::tr("Checking the cloud, try again in a moment.", "Consultando la nube, vuelve a intentarlo en un momento."));
                return;
            }
            CloudGamesState::create_and_push();
            return;
        case ESTADO_FAV:
            if (esperando() || sin_nube()) { return; }
            CloudDashboardState::create_and_push();
            return;
        case PROBAR:
            if (esperando()) { return; }
            ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, strings::tr("Testing the connection…", "Probando la conexión…"));
            nube::consultar(true);
            return;
        case CONECTAR:
            nube::invalidar();
            CloudSetupState::create_and_push();
            return;
        case SUBIR_SALIR:
        {
            const uint8_t valor = config::get_by_key(config::keys::SUBIR_AL_SALIR) ? 0 : 1;
            config::set_by_key(config::keys::SUBIR_AL_SALIR, valor);
            config::save();
            return;
        }
    }
}
