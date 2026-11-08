#include "nube.hpp"

#include "config/config.hpp"
#include "error.hpp"
#include "fs/fs.hpp"
#include "fslib.hpp"
#include "json.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"
#include "sys/ProgressTask.hpp"
#include "ui/PopMessageManager.hpp"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <map>
#include <mutex>
#include <vector>

namespace
{
    constexpr const char *PATH_REGISTRO = "sdmc:/config/JKSV/nube_sync.json";

    struct Registro
    {
        std::string respaldo{};
        uint64_t marca{};
    };

    std::mutex s_registroLock{};
    std::map<std::string, Registro> s_registro{};
    bool s_registroLeido{};

    std::mutex s_resumenLock{};
    nube::Resumen s_resumen{};
    std::atomic<bool> s_consultando{};
    std::atomic<bool> s_vigente{};

    struct DatosConsulta : sys::threadpool::DataStruct
    {
        data::UserList usuarios{};
        bool avisar{};
    };

    std::string llave(data::User *user, uint64_t aplicacion)
    {
        const AccountUid uid = user->get_account_id();
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%016lX-%016lX%016lX", aplicacion, uid.uid[0], uid.uid[1]);
        return buffer;
    }

    // Con el lock tomado.
    void leer_registro()
    {
        if (s_registroLeido) { return; }
        s_registroLeido = true;
        if (!fslib::file_exists(fslib::Path{PATH_REGISTRO})) { return; }

        json::Object raiz = json::new_object(json_object_from_file, PATH_REGISTRO);
        if (!raiz) { return; }
        json_object_object_foreach(raiz.get(), clave, valor)
        {
            json_object *n = json_object_object_get(valor, "n");
            json_object *t = json_object_object_get(valor, "t");
            if (!n || !t) { continue; }
            s_registro[clave] = Registro{json_object_get_string(n), json_object_get_uint64(t)};
        }
    }

    // Con el lock tomado.
    void guardar_registro()
    {
        json::Object raiz = json::new_object(json_object_new_object);
        for (const auto &[clave, r] : s_registro)
        {
            json_object *o = json_object_new_object();
            json_object_object_add(o, "n", json_object_new_string(r.respaldo.c_str()));
            json_object_object_add(o, "t", json_object_new_uint64(r.marca));
            json_object_object_add(raiz.get(), clave.c_str(), o);
        }
        const std::string texto = json_object_to_json_string_ext(raiz.get(), JSON_C_TO_STRING_PLAIN);

        const fslib::Path path{PATH_REGISTRO};
        if (fslib::file_exists(path)) { fslib::delete_file(path); }
        fslib::File archivo{path, FsOpenMode_Create | FsOpenMode_Write, static_cast<int64_t>(texto.length())};
        if (archivo.is_open()) { archivo << texto; }
    }

    // Las partidas favoritas que se pueden sincronizar: usuarios de cuenta, juegos con corazon.
    template <typename Funcion>
    void por_cada_favorito(const data::UserList &usuarios, Funcion funcion)
    {
        for (data::User *user : usuarios)
        {
            if (user->get_account_save_type() == FsSaveDataType_System) { continue; }
            const int64_t total = user->get_total_data_entries();
            for (int64_t i = 0; i < total; i++)
            {
                const FsSaveDataInfo *saveInfo = user->get_save_info_at(i);
                if (!saveInfo || !config::is_favorite(saveInfo->application_id)) { continue; }
                data::TitleInfo *info = data::get_title_info_by_id(saveInfo->application_id);
                if (!info) { continue; }
                funcion(user, saveInfo, info);
            }
        }
    }

    void terminar_consulta(nube::Resumen &r, bool avisar)
    {
        r.listo = true;
        {
            std::lock_guard<std::mutex> guard{s_resumenLock};
            s_resumen = r;
        }
        s_vigente.store(r.ok);
        s_consultando.store(false);
        if (!avisar) { return; }

        const int ticks = ui::PopMessageManager::DEFAULT_TICKS * 2;
        if (!r.ok) { ui::PopMessageManager::push_message(ticks, r.error); }
        else
        {
            const std::string texto = strings::tr("The cloud answers: ", "La nube responde: ") + std::to_string(r.juegos) +
                                      strings::tr(" games, ", " juegos, ") + nube::tamano_legible(r.bytes) + ".";
            ui::PopMessageManager::push_message(ticks, texto);
        }
    }

    void consulta(sys::threadpool::JobData jobData)
    {
        auto datos = std::static_pointer_cast<DatosConsulta>(jobData);
        nube::Resumen r{};

        if (!remote::has_internet_connection())
        {
            r.error = strings::tr("The console isn't online. Most likely Wi-Fi is off or out of range; rule it out if other apps can go online.",
                                  "La consola no tiene internet. Lo más probable es que el Wi-Fi esté apagado o lejos; descártalo si otras apps sí se conectan.");
            terminar_consulta(r, datos->avisar);
            return;
        }

        // Configurada pero sin conectar (contrasena cambiada, servidor caido al arrancar): se reintenta.
        if (!remote::get_remote_storage() && nube::hay_configuracion()) { remote::initialize(nullptr); }
        remote::Storage *remote = remote::get_remote_storage();
        if (!remote)
        {
            r.error = nube::hay_configuracion()
                          ? strings::tr("Couldn't sign in to your cloud. Most likely the app password or the address changed; check them in Connect or change cloud.",
                                        "No se pudo entrar a tu nube. Lo más probable es que la contraseña de aplicación o la dirección hayan cambiado; revísalas en Conectar o cambiar de nube.")
                          : strings::tr("No cloud set up yet.", "Todavía no hay una nube configurada.");
            terminar_consulta(r, datos->avisar);
            return;
        }

        if (!remote->reload())
        {
            r.error = strings::tr("The cloud didn't answer. Most likely the service is down or the connection is slow; rule it out if the site opens on another device.",
                                  "La nube no respondió. Lo más probable es que el servicio esté caído o la conexión lenta; descártalo si la página abre en otro equipo.");
            terminar_consulta(r, datos->avisar);
            return;
        }

        // Todo lo que hay en la nube: juegos, tamano y el respaldo mas nuevo.
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
            bool tiene = false;
            for (remote::Item *a : archivos)
            {
                if (a->is_directory()) { continue; }
                tiene = true;
                r.bytes += a->get_size();
                const std::string k = nube::clave(a->get_name());
                if (k > r.ultimaClave)
                {
                    r.ultimaClave = k;
                    r.ultimoJuego = carpeta;
                }
            }
            r.juegos += tiene ? 1 : 0;
        }
        remote->return_to_root();

        por_cada_favorito(datos->usuarios, [&](data::User *user, const FsSaveDataInfo *saveInfo, data::TitleInfo *info) {
            r.favoritos++;
            switch (nube::decidir(remote, user, saveInfo, info))
            {
                case nube::Accion::Subir: r.subir++; break;
                case nube::Accion::Bajar: r.bajar++; break;
                case nube::Accion::AlDia: r.alDia++; break;
                case nube::Accion::Conflicto: r.conflictos++; break;
            }
        });

        r.ok = true;
        terminar_consulta(r, datos->avisar);
    }

    // Busca la carpeta y el archivo; deja la nube dentro de la carpeta.
    remote::Item *buscar(remote::Storage *remote, std::string_view carpeta, std::string_view archivo)
    {
        remote->return_to_root();
        remote::Item *dir = remote->get_directory_by_name(carpeta);
        if (!dir) { return nullptr; }
        remote->change_directory(dir);
        return remote->get_file_by_name(archivo);
    }
}

bool nube::hay_configuracion()
{
    return fslib::file_exists(fslib::Path{remote::PATH_WEBDAV_CONFIG}) || fslib::file_exists(fslib::Path{remote::PATH_GOOGLE_DRIVE_CONFIG});
}

void nube::consultar(bool avisar)
{
    if (s_consultando.exchange(true)) { return; }
    auto datos    = std::make_shared<DatosConsulta>();
    datos->avisar = avisar;
    data::get_users(datos->usuarios);
    sys::threadpool::push_job(consulta, datos);
}

bool nube::consultando() noexcept { return s_consultando.load(); }

nube::Resumen nube::resumen()
{
    std::lock_guard<std::mutex> guard{s_resumenLock};
    return s_resumen;
}

void nube::invalidar() noexcept { s_vigente.store(false); }

bool nube::vigente() noexcept { return s_vigente.load(); }

std::string nube::carpeta_de(remote::Storage *remote, data::TitleInfo *info)
{
    return remote->supports_utf8() ? info->get_title() : info->get_path_safe_title();
}

remote::Item *nube::mas_nuevo(remote::Storage *remote, std::string_view carpeta)
{
    remote->return_to_root();
    remote::Item *dir = remote->get_directory_by_name(carpeta);
    if (!dir) { return nullptr; }
    remote->change_directory(dir);
    remote::Storage::DirectoryListing archivos{};
    remote->get_directory_listing(archivos);
    remote->return_to_root();

    remote::Item *nuevo = nullptr;
    std::string mejor{};
    for (remote::Item *a : archivos)
    {
        if (a->is_directory()) { continue; }
        const std::string k = nube::clave(a->get_name());
        if (!nuevo || k > mejor)
        {
            nuevo = a;
            mejor = k;
        }
    }
    return nuevo;
}

nube::Accion nube::decidir(remote::Storage *remote, data::User *user, const FsSaveDataInfo *saveInfo, data::TitleInfo *info)
{
    remote::Item *nuevo = nube::mas_nuevo(remote, nube::carpeta_de(remote, info));
    if (!nuevo) { return Accion::Subir; }

    Registro r{};
    bool hay = false;
    {
        std::lock_guard<std::mutex> guard{s_registroLock};
        leer_registro();
        auto it = s_registro.find(llave(user, saveInfo->application_id));
        if (it != s_registro.end())
        {
            r   = it->second;
            hay = true;
        }
    }
    const bool cambioAqui = hay && nube::marca_partida(saveInfo) > r.marca;
    const bool cambioAlla = !hay || r.respaldo != nuevo->get_name();
    if (cambioAqui && cambioAlla) { return Accion::Conflicto; }
    if (cambioAlla) { return Accion::Bajar; }
    return cambioAqui ? Accion::Subir : Accion::AlDia;
}

void nube::registrar(data::User *user, uint64_t aplicacion, std::string_view respaldo, uint64_t marcaPartida)
{
    std::lock_guard<std::mutex> guard{s_registroLock};
    leer_registro();
    s_registro[llave(user, aplicacion)] = Registro{std::string{respaldo}, marcaPartida};
    guardar_registro();
}

uint64_t nube::marca_partida(const FsSaveDataInfo *saveInfo)
{
    FsSaveDataExtraData extra{};
    if (!saveInfo || !fs::read_save_extra_data(saveInfo, extra)) { return 0; }
    return extra.timestamp;
}

std::string nube::clave(std::string_view nombre)
{
    if (nombre.size() > 4 && nombre.substr(nombre.size() - 4) == ".zip") { nombre.remove_suffix(4); }
    if (nombre.size() < 19) { return {}; }
    const std::string_view k = nombre.substr(nombre.size() - 19);
    if (k[4] != '-' || k[7] != '-' || k[10] != '_') { return {}; }
    return std::string{k};
}

std::string nube::fecha_legible(const std::string &k)
{
    if (k.size() < 19) { return {}; }
    return k.substr(8, 2) + "/" + k.substr(5, 2) + "/" + k.substr(0, 4) + "  ·  " + k.substr(11, 2) + ":" + k.substr(14, 2);
}

std::string nube::tamano_legible(uint64_t bytes)
{
    char buffer[32]{};
    if (bytes >= 1024ULL * 1024 * 1024) { std::snprintf(buffer, sizeof(buffer), "%.1f GB", bytes / (1024.0 * 1024 * 1024)); }
    else if (bytes >= 1024ULL * 1024) { std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024)); }
    else { std::snprintf(buffer, sizeof(buffer), "%llu KB", static_cast<unsigned long long>((bytes + 1023) / 1024)); }
    return buffer;
}

void nube::bajar_respaldo(sys::threadpool::JobData jobData)
{
    auto datos              = std::static_pointer_cast<nube::DatosRespaldo>(jobData);
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(datos->task);
    if (!task) { return; }

    remote::Storage *remote = remote::get_remote_storage();
    remote::Item *item      = remote ? buscar(remote, datos->carpeta, datos->archivo) : nullptr;
    if (!item)
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                            strings::tr("That backup is no longer in the cloud.", "Ese respaldo ya no está en la nube."));
        if (remote) { remote->return_to_root(); }
        TASK_FINISH_RETURN(task);
    }

    task->set_status(strings::tr("Downloading ", "Bajando ") + datos->archivo);
    if (!fslib::directory_exists(datos->destino)) { fslib::create_directories_recursively(datos->destino); }
    const fslib::Path destino{datos->destino / datos->archivo};
    const bool bien = remote->download_file(item, destino, task);
    remote->return_to_root();

    ui::PopMessageManager::push_message(
        ui::PopMessageManager::DEFAULT_TICKS * 2,
        bien ? strings::tr("Downloaded to the console: it's in this game's backups.", "Bajado a la consola: está en los respaldos de ese juego.")
             : strings::tr("Couldn't download it. Most likely the connection dropped; try again.",
                           "No se pudo bajar. Lo más probable es que se haya cortado la conexión; vuelve a intentarlo."));
    if (!bien && fslib::file_exists(destino)) { fslib::delete_file(destino); }
    task->complete();
}

void nube::borrar_respaldo(sys::threadpool::JobData jobData)
{
    auto datos      = std::static_pointer_cast<nube::DatosRespaldo>(jobData);
    sys::Task *task = datos->task;
    if (!task) { return; }

    remote::Storage *remote = remote::get_remote_storage();
    remote::Item *item      = remote ? buscar(remote, datos->carpeta, datos->archivo) : nullptr;
    task->set_status(strings::tr("Deleting ", "Borrando ") + datos->archivo);
    const bool bien = item && remote->delete_item(item);
    if (remote) { remote->return_to_root(); }
    if (!bien)
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS * 2,
                                            strings::tr("Couldn't delete it. Most likely the connection dropped; try again.",
                                                        "No se pudo borrar. Lo más probable es que se haya cortado la conexión; vuelve a intentarlo."));
    }
    nube::invalidar();
    task->complete();
}

void nube::conservar_nuevos(sys::threadpool::JobData jobData)
{
    static constexpr int CONSERVAR = 3;

    auto datos      = std::static_pointer_cast<nube::DatosRespaldo>(jobData);
    sys::Task *task = datos->task;
    if (!task) { return; }

    remote::Storage *remote = remote::get_remote_storage();
    if (!remote) { TASK_FINISH_RETURN(task); }

    // Los nombres primero: cada borrado cambia la lista de la nube.
    std::vector<std::string> nombres{};
    {
        remote->return_to_root();
        remote::Item *dir = remote->get_directory_by_name(datos->carpeta);
        if (dir)
        {
            remote->change_directory(dir);
            remote::Storage::DirectoryListing archivos{};
            remote->get_directory_listing(archivos);
            for (remote::Item *a : archivos)
            {
                if (!a->is_directory()) { nombres.emplace_back(a->get_name()); }
            }
        }
        remote->return_to_root();
    }
    std::sort(nombres.begin(), nombres.end(), [](const std::string &a, const std::string &b) { return nube::clave(a) > nube::clave(b); });

    int borrados = 0;
    for (size_t i = CONSERVAR; i < nombres.size(); i++)
    {
        task->set_status(strings::tr("Deleting ", "Borrando ") + nombres[i]);
        remote::Item *item = buscar(remote, datos->carpeta, nombres[i]);
        if (item && remote->delete_item(item)) { borrados++; }
        remote->return_to_root();
    }

    const std::string texto = strings::tr("Deleted ", "Se borraron ") + std::to_string(borrados) +
                              strings::tr(" old backups; the 3 newest stay.", " respaldos viejos; quedan los 3 más nuevos.");
    ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS * 2, texto);
    nube::invalidar();
    task->complete();
}
