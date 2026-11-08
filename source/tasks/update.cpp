#include "tasks/update.hpp"

#include "JKSV.hpp"
#include "appstates/ConfirmState.hpp"
#include "appstates/MainMenuState.hpp"
#include "cmdargs.hpp"
#include "curl/curl.hpp"
#include "error.hpp"
#include "json.hpp"
#include "logging/logger.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"

#include <string_view>

#include "fslib.hpp"
#include "ui/PopMessageManager.hpp"

#include <mutex>
#include <vector>

#ifndef BACAN_VERSION
#define BACAN_VERSION "0.0"
#endif

namespace
{
    // Lo que trajo la consulta al arrancar: la descarga usa lo mismo, sin volver a preguntar a GitHub.
    std::mutex s_lock{};
    std::string s_version{}, s_notas{}, s_url{};
    uint64_t s_size{};

    // "v2.7-bacan" -> {2, 7}. Solo los numeros del principio.
    std::vector<int> numeros(std::string_view tag)
    {
        std::vector<int> out{};
        size_t i = 0;
        while (i < tag.size() && (tag[i] < '0' || tag[i] > '9')) { i++; }
        while (i < tag.size() && tag[i] >= '0' && tag[i] <= '9')
        {
            int n = 0;
            while (i < tag.size() && tag[i] >= '0' && tag[i] <= '9') { n = n * 10 + (tag[i++] - '0'); }
            out.push_back(n);
            if (i < tag.size() && tag[i] == '.') { i++; }
            else { break; }
        }
        return out;
    }

    bool es_mas_nueva(std::string_view remota, std::string_view local)
    {
        std::vector<int> r = numeros(remota), l = numeros(local);
        if (r.empty() || l.empty()) { return false; }
        while (r.size() < l.size()) { r.push_back(0); }
        while (l.size() < r.size()) { l.push_back(0); }
        return r > l;
    }

    std::string texto_de(std::string_view tag)
    {
        std::string out{};
        for (const int n : numeros(tag)) { out += (out.empty() ? "" : ".") + std::to_string(n); }
        return out;
    }
}

const char *tasks::update::version_actual() noexcept { return BACAN_VERSION; }

std::string tasks::update::version_nueva()
{
    std::lock_guard<std::mutex> guard{s_lock};
    return s_version;
}

std::string tasks::update::notas_nueva()
{
    std::lock_guard<std::mutex> guard{s_lock};
    return s_notas;
}

static std::string get_git_json(curl::Handle &handle);

void tasks::update::check_for_update(sys::threadpool::JobData jobData)
{
    auto castData                = std::static_pointer_cast<MainMenuState::DataStruct>(jobData);
    MainMenuState *spawningState = castData->spawningState;

    // Una sola consulta por arranque.
    if (!remote::has_internet_connection()) { return; }

    curl::Handle curlHandle   = curl::new_handle();
    const std::string gitJson = get_git_json(curlHandle);
    json::Object parser       = json::new_object(json_tokener_parse, gitJson.c_str());
    if (!parser) { return; }

    json_object *tagName = json::get_object(parser, "tag_name");
    if (!tagName) { return; }
    const char *tag = json_object_get_string(tagName);
    if (!tag || !es_mas_nueva(tag, BACAN_VERSION)) { return; }

    // El asset tiene que llamarse JKSV.nro (es el mismo que baja OQB Updater).
    json_object *assets = json::get_object(parser, "assets");
    const size_t total  = assets ? json_object_array_length(assets) : 0;
    std::string url{};
    uint64_t size{};
    for (size_t i = 0; i < total; i++)
    {
        json_object *asset = json_object_array_get_idx(assets, i);
        json_object *name  = asset ? json_object_object_get(asset, "name") : nullptr;
        if (!name || std::string_view{json_object_get_string(name)} != "JKSV.nro") { continue; }
        json_object *link   = json_object_object_get(asset, "browser_download_url");
        json_object *length = json_object_object_get(asset, "size");
        if (link && length)
        {
            url  = json_object_get_string(link);
            size = json_object_get_uint64(length);
        }
        break;
    }
    if (url.empty() || size == 0) { return; }

    json_object *body = json::get_object(parser, "body");
    {
        std::lock_guard<std::mutex> guard{s_lock};
        s_version = texto_de(tag);
        s_notas   = body ? json_object_get_string(body) : "";
        s_url     = std::move(url);
        s_size    = size;
    }
    spawningState->signal_update_found();
}

void tasks::update::download_update(sys::threadpool::JobData jobData)
{
    auto castData           = std::static_pointer_cast<sys::Task::DataStruct>(jobData);
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);
    if (error::is_null(task)) { return; }

    std::string url{};
    uint64_t size{};
    {
        std::lock_guard<std::mutex> guard{s_lock};
        url  = s_url;
        size = s_size;
    }
    auto fallo = [&](const char *es, const char *en) {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS * 2, strings::tr(en, es));
    };
    if (url.empty() || size == 0) { TASK_FINISH_RETURN(task); }

    task->set_status(strings::tr("Downloading JKSV BACAN ", "Bajando JKSV BACÁN ") + tasks::update::version_nueva() + "…");
    task->reset(static_cast<double>(size));

    // Primero a un .tmp al lado: si se corta a la mitad, el JKSV que ya tienes sigue intacto.
    const char *argumento = cmdargs::get(0);
    // Sin argumento (raro): donde lo deja OQB Updater, sdmc:/switch/JKSV.nro, o en su carpeta.
    std::string destino{};
    if (argumento && std::string_view{argumento}.rfind("sdmc:/", 0) == 0) { destino = argumento; }
    else if (fslib::file_exists(fslib::Path{"sdmc:/switch/JKSV/JKSV.nro"})) { destino = "sdmc:/switch/JKSV/JKSV.nro"; }
    else { destino = "sdmc:/switch/JKSV.nro"; }
    const fslib::Path pathNro{destino};
    const fslib::Path pathTmp{destino + ".tmp"};
    if (fslib::file_exists(pathTmp)) { fslib::delete_file(pathTmp); }

    bool completo = false;
    {
        fslib::File tmp{pathTmp, FsOpenMode_Create | FsOpenMode_Write, static_cast<int64_t>(size)};
        if (!tmp.is_open())
        {
            fallo("No se pudo crear el archivo de la actualización. Lo más probable es que la SD esté llena o protegida; tu JKSV sigue igual.",
                  "Couldn't create the update file. Most likely the SD card is full or locked; your JKSV is unchanged.");
            TASK_FINISH_RETURN(task);
        }

        curl::Handle downloadCurl = curl::new_handle();
        auto download             = curl::create_download_struct(tmp, task, size);
        curl::set_option(downloadCurl, CURLOPT_URL, url.c_str());
        curl::set_option(downloadCurl, CURLOPT_WRITEFUNCTION, curl::download_file_threaded);
        curl::set_option(downloadCurl, CURLOPT_WRITEDATA, download.get());
        curl::set_option(downloadCurl, CURLOPT_FOLLOWLOCATION, 1L);

        sys::threadpool::push_job(curl::download_write_thread_function, download);
        const bool bajado = curl::perform(downloadCurl);
        download->writeComplete.acquire();
        // El .tmp se crea ya con su tamano final: lo que dice si llego todo es lo que conto curl y el codigo HTTP.
        curl_off_t recibidos{};
        long codigo{};
        curl_easy_getinfo(downloadCurl.get(), CURLINFO_SIZE_DOWNLOAD_T, &recibidos);
        curl_easy_getinfo(downloadCurl.get(), CURLINFO_RESPONSE_CODE, &codigo);
        completo = bajado && codigo == 200 && static_cast<uint64_t>(recibidos) == size;
    }

    if (!completo)
    {
        fslib::delete_file(pathTmp);
        fallo("La actualización no se bajó completa. Lo más probable es que se haya cortado la conexión; tu JKSV sigue igual y puedes reintentar al volver a abrirlo.",
              "The update didn't download completely. Most likely the connection dropped; your JKSV is unchanged and you can retry next time you open it.");
        TASK_FINISH_RETURN(task);
    }

    // Llego entera: ahora si se reemplaza. romfs se cierra para poder borrar el .nro que esta en uso.
    error::libnx(romfsExit());
    fslib::delete_file(pathNro);
    if (!fslib::rename_file(pathTmp, pathNro))
    {
        // Sin el nombre final no abriria desde el menu; queda el .tmp para no perder lo bajado.
        fallo("Se bajó la actualización pero no se pudo poner en su lugar. Probablemente la SD está protegida; queda como JKSV.nro.tmp.",
              "The update downloaded but couldn't be moved into place. The SD card is probably locked; it was left as JKSV.nro.tmp.");
    }

    task->set_status(strings::tr("Done. JKSV will close: open it again to use the new version.",
                                 "Listo. JKSV se va a cerrar: ábrelo de nuevo para usar la versión nueva."));
    svcSleepThread(2'000'000'000LL);
    task->complete();

    JKSV::request_quit();
}

static std::string get_git_json(curl::Handle &handle)
{
    // El repo de JKSV BACAN, no el original: el original reemplazaria esta version por la de J-D-K.
    static constexpr const char *URL_GIT_API = "https://api.github.com/repos/ohquebacan/JKSV-BACAN/releases/latest";

    std::string response{};
    curl::prepare_get(handle);
    curl::set_option(handle, CURLOPT_URL, URL_GIT_API);
    curl::set_option(handle, CURLOPT_WRITEFUNCTION, curl::write_response_string);
    curl::set_option(handle, CURLOPT_WRITEDATA, &response);
    curl::set_option(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl::perform(handle);

    return response;
}
