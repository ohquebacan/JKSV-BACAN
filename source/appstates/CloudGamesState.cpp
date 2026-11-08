#include "appstates/CloudGamesState.hpp"

#include "appstates/ConfirmState.hpp"
#include "appstates/ProgressState.hpp"
#include "appstates/TaskState.hpp"
#include "config/config.hpp"
#include "data/data.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "nube.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "ui/ListaSecciones.hpp"

#include <algorithm>

//                      ---- Juegos en la nube ----

CloudGamesState::CloudGamesState()
    : m_menu(ui::Menu::create(32, 10, 1000, 23, 555))
    , m_controlGuide(ui::ControlGuide::create(strings::tr(" See backups     Back", " Ver respaldos     Atrás")))
{
}

void CloudGamesState::cargar()
{
    m_juegos.clear();
    m_total = 0;
    remote::Storage *remote = remote::get_remote_storage();
    if (!remote) { return; }

    remote->return_to_root();
    remote::Storage::DirectoryListing raiz{};
    remote->get_directory_listing(raiz);
    std::vector<std::string> carpetas{};
    for (remote::Item *item : raiz)
    {
        if (item->is_directory()) { carpetas.emplace_back(item->get_name()); }
    }
    for (const std::string &carpeta : carpetas)
    {
        remote->return_to_root();
        remote::Item *dir = remote->get_directory_by_name(carpeta);
        if (!dir) { continue; }
        remote->change_directory(dir);
        remote::Storage::DirectoryListing archivos{};
        remote->get_directory_listing(archivos);

        Juego j{};
        j.carpeta = carpeta;
        for (remote::Item *a : archivos)
        {
            if (a->is_directory()) { continue; }
            j.respaldos++;
            j.bytes += a->get_size();
            const std::string k = nube::clave(a->get_name());
            if (k > j.ultima) { j.ultima = k; }
        }
        m_total += j.bytes;
        if (j.respaldos > 0) { m_juegos.push_back(std::move(j)); }
    }
    remote->return_to_root();

    // Lo usado hace poco, arriba.
    std::sort(m_juegos.begin(), m_juegos.end(), [](const Juego &a, const Juego &b) { return a.ultima > b.ultima; });

    const int elegida = m_menu->get_selected();
    m_menu->reset();
    for (const Juego &j : m_juegos) { m_menu->add_option(j.carpeta); }
    if (elegida > 0 && elegida < static_cast<int>(m_juegos.size())) { m_menu->set_selected(elegida); }
}

void CloudGamesState::update()
{
    const bool hasFocus = BaseState::has_focus();
    if (hasFocus && !m_teniaFoco) { CloudGamesState::cargar(); } // al entrar y al volver de un juego
    m_teniaFoco = hasFocus;

    if (!m_juegos.empty()) { m_menu->update(hasFocus); }
    m_controlGuide->update(hasFocus);

    const int fila = m_menu->get_selected();
    if (input::button_pressed(HidNpadButton_A) && fila >= 0 && fila < static_cast<int>(m_juegos.size()))
    {
        CloudGameBackupsState::create_and_push(m_juegos[fila].carpeta);
    }
    else if (input::button_pressed(HidNpadButton_B)) { BaseState::deactivate(); }
}

void CloudGamesState::sub_update() { m_controlGuide->sub_update(); }

void CloudGamesState::render()
{
    const bool hasFocus = BaseState::has_focus();
    const std::string estado =
        std::to_string(m_juegos.size()) + strings::tr(" games · ", " juegos · ") + nube::tamano_legible(m_total);
    ui::lista::marco(strings::tr("Games in the cloud", "Juegos en la nube"), estado);

    if (m_juegos.empty())
    {
        sdl::text::render(sdl::Texture::Null, 176, 140, 20, 640, colors::DIALOG_LIGHT,
                          strings::tr("There's nothing in your cloud yet. Sync or upload your favorites from the Cloud section.",
                                      "Todavía no hay nada en tu nube. Sincroniza o sube tus favoritos desde la sección Nube."));
    }
    else
    {
        std::vector<ui::lista::Fila> filas{};
        for (const Juego &j : m_juegos)
        {
            ui::lista::Fila f{};
            f.titulo      = j.carpeta;
            f.valor       = std::to_string(j.respaldos) + "  ·  " + nube::tamano_legible(j.bytes);
            f.descripcion = (j.ultima.empty() ? std::string{}
                                              : strings::tr("Newest backup: ", "Respaldo más nuevo: ") + nube::fecha_legible(j.ultima) + "\n\n") +
                            std::to_string(j.respaldos) + strings::tr(" backups, ", " respaldos, ") + nube::tamano_legible(j.bytes) +
                            strings::tr(".\n\nPress  to see them, download one or delete the old ones.",
                                        ".\n\nPulsa  para verlos, bajar uno o borrar los viejos.");
            filas.push_back(std::move(f));
        }
        ui::lista::dibujar(filas, m_menu->get_selected(), m_desplazamiento, hasFocus);
    }
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Respaldos de un juego ----

CloudGameBackupsState::CloudGameBackupsState(std::string carpeta)
    : m_carpeta(std::move(carpeta))
    , m_menu(ui::Menu::create(32, 10, 1000, 23, 555))
    , m_controlGuide(ui::ControlGuide::create(strings::tr(" Download     Delete     Keep 3 newest     Back",
                                                          " Bajar     Borrar     Dejar 3 más nuevos     Atrás")))
{
}

void CloudGameBackupsState::cargar()
{
    m_respaldos.clear();
    remote::Storage *remote = remote::get_remote_storage();
    if (remote)
    {
        remote->return_to_root();
        remote::Item *dir = remote->get_directory_by_name(m_carpeta);
        if (dir)
        {
            remote->change_directory(dir);
            remote::Storage::DirectoryListing archivos{};
            remote->get_directory_listing(archivos);
            for (remote::Item *a : archivos)
            {
                if (a->is_directory()) { continue; }
                m_respaldos.push_back(Respaldo{std::string{a->get_name()}, nube::clave(a->get_name()), a->get_size()});
            }
        }
        remote->return_to_root();
    }
    std::sort(m_respaldos.begin(), m_respaldos.end(), [](const Respaldo &a, const Respaldo &b) { return a.clave > b.clave; });

    const int elegida = m_menu->get_selected();
    m_menu->reset();
    for (const Respaldo &r : m_respaldos) { m_menu->add_option(r.nombre); }
    if (elegida > 0 && elegida < static_cast<int>(m_respaldos.size())) { m_menu->set_selected(elegida); }
}

void CloudGameBackupsState::update()
{
    const bool hasFocus = BaseState::has_focus();
    if (hasFocus && !m_teniaFoco) { CloudGameBackupsState::cargar(); } // al entrar y despues de bajar o borrar
    m_teniaFoco = hasFocus;

    if (!m_respaldos.empty()) { m_menu->update(hasFocus); }
    m_controlGuide->update(hasFocus);

    if (input::button_pressed(HidNpadButton_B))
    {
        BaseState::deactivate();
        return;
    }

    const int fila   = m_menu->get_selected();
    const bool valida = fila >= 0 && fila < static_cast<int>(m_respaldos.size());
    if (!valida) { return; }

    auto datos     = std::make_shared<nube::DatosRespaldo>();
    datos->carpeta = m_carpeta;
    datos->archivo = m_respaldos[fila].nombre;

    if (input::button_pressed(HidNpadButton_A))
    {
        // A la carpeta local de ese juego, para que salga entre sus respaldos (si esta instalado).
        remote::Storage *remote = remote::get_remote_storage();
        datos->destino          = config::get_working_directory() / m_carpeta;
        data::UserList usuarios{};
        data::get_users(usuarios);
        for (data::User *user : usuarios)
        {
            const int64_t total = user->get_total_data_entries();
            for (int64_t i = 0; remote && i < total; i++)
            {
                data::TitleInfo *info = data::get_title_info_by_id(user->get_application_id_at(i));
                if (info && nube::carpeta_de(remote, info) == m_carpeta)
                {
                    datos->destino = config::get_working_directory() / info->get_path_safe_title();
                    break;
                }
            }
        }
        ConfirmProgress::create_push_fade(strings::tr("Download this backup to the console? It goes with this game's backups; your save doesn't change.",
                                                      "¿Bajar este respaldo a la consola? Queda junto a los respaldos de este juego; tu partida no cambia."),
                                          false, nube::bajar_respaldo, nullptr, datos);
    }
    else if (input::button_pressed(HidNpadButton_X))
    {
        ConfirmTask::create_push_fade(strings::tr("Delete this backup from the cloud? This can't be undone.",
                                                  "¿Borrar este respaldo de la nube? No se puede deshacer."),
                                      true, nube::borrar_respaldo, nullptr, datos);
    }
    else if (input::button_pressed(HidNpadButton_Y) && m_respaldos.size() > 3)
    {
        ConfirmTask::create_push_fade(strings::tr("Keep only the 3 newest backups of this game in the cloud? The rest are deleted.",
                                                  "¿Dejar solo los 3 respaldos más nuevos de este juego en la nube? Los demás se borran."),
                                      true, nube::conservar_nuevos, nullptr, datos);
    }
}

void CloudGameBackupsState::sub_update() { m_controlGuide->sub_update(); }

void CloudGameBackupsState::render()
{
    const bool hasFocus = BaseState::has_focus();
    ui::lista::marco(m_carpeta, std::to_string(m_respaldos.size()) + strings::tr(" backups", " respaldos"));

    if (m_respaldos.empty())
    {
        sdl::text::render(sdl::Texture::Null, 176, 140, 20, 640, colors::DIALOG_LIGHT,
                          strings::tr("This game has no backups in the cloud anymore.", "Este juego ya no tiene respaldos en la nube."));
    }
    else
    {
        std::vector<ui::lista::Fila> filas{};
        for (size_t i = 0; i < m_respaldos.size(); i++)
        {
            const Respaldo &r = m_respaldos[i];
            ui::lista::Fila f{};
            f.titulo      = r.clave.empty() ? r.nombre : nube::fecha_legible(r.clave);
            f.valor       = nube::tamano_legible(r.bytes);
            f.valorActivo = i == 0;
            f.descripcion = r.nombre + "\n\n" +
                            strings::tr(" downloads it to the console without touching your save.\n deletes it from the cloud.\n keeps only the 3 newest.",
                                        " lo baja a la consola sin tocar tu partida.\n lo borra de la nube.\n deja solo los 3 más nuevos.");
            filas.push_back(std::move(f));
        }
        ui::lista::dibujar(filas, m_menu->get_selected(), m_desplazamiento, hasFocus);
    }
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}
