#include "partidas.hpp"

#include "curl/curl.hpp"
#include "json.hpp"
#include "remote/remote.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <atomic>
#include <mutex>

namespace
{
    constexpr const char *URL_CATALOGO = "https://raw.githubusercontent.com/ohquebacan/OQB-HATS-PACK/main/partidas.json";
    // Si existe, se usa en lugar del publicado: para probar partidas nuevas antes de subirlas.
    constexpr const char *RUTA_LOCAL = "sdmc:/config/JKSV/partidas.json";

    std::mutex s_candado{};
    std::vector<partidas::Partida> s_catalogo{};
    std::atomic<int> s_generacion{0};
}

void partidas::cargar(sys::threadpool::JobData)
{
    std::string respuesta{};
    std::ifstream local{RUTA_LOCAL};
    if (local.good()) { respuesta.assign(std::istreambuf_iterator<char>(local), std::istreambuf_iterator<char>()); }
    else if (remote::has_internet_connection())
    {
        curl::Handle handle = curl::new_handle();
        curl::prepare_get(handle);
        curl::set_option(handle, CURLOPT_URL, URL_CATALOGO);
        curl::set_option(handle, CURLOPT_WRITEFUNCTION, curl::write_response_string);
        curl::set_option(handle, CURLOPT_WRITEDATA, &respuesta);
        curl::set_option(handle, CURLOPT_FOLLOWLOCATION, 1L);
        if (!curl::perform(handle) || curl::get_response_code(handle) != 200) { return; }
    }
    else { return; }

    json::Object raiz = json::new_object(json_tokener_parse, respuesta.c_str());
    if (!raiz) { return; }
    json_object *lista = json::get_object(raiz, "partidas");
    if (!lista || !json_object_is_type(lista, json_type_array)) { return; }

    std::vector<partidas::Partida> leidas{};
    const size_t total = json_object_array_length(lista);
    for (size_t i = 0; i < total; i++)
    {
        json_object *entrada = json_object_array_get_idx(lista, i);
        json_object *id      = entrada ? json_object_object_get(entrada, "id") : nullptr;
        json_object *titulo  = entrada ? json_object_object_get(entrada, "titulo") : nullptr;
        json_object *url     = entrada ? json_object_object_get(entrada, "url") : nullptr;
        if (!id || !titulo || !url) { continue; }

        partidas::Partida partida{};
        partida.aplicacion = std::strtoull(json_object_get_string(id), nullptr, 16);
        partida.titulo     = json_object_get_string(titulo);
        partida.url        = json_object_get_string(url);
        json_object *raiz  = json_object_object_get(entrada, "raiz");
        json_object *autor = json_object_object_get(entrada, "autor");
        if (raiz) { partida.raiz = json_object_get_string(raiz); }
        if (autor) { partida.autor = json_object_get_string(autor); }
        if (!partida.raiz.empty() && partida.raiz.back() != '/') { partida.raiz += '/'; }
        if (partida.aplicacion == 0 || partida.url.rfind("https://", 0) != 0) { continue; }
        leidas.push_back(std::move(partida));
    }

    {
        std::lock_guard guardia{s_candado};
        s_catalogo = std::move(leidas);
    }
    ++s_generacion;
}

std::vector<partidas::Partida> partidas::para(uint64_t applicationID)
{
    std::vector<partidas::Partida> encontradas{};
    std::lock_guard guardia{s_candado};
    for (const partidas::Partida &partida : s_catalogo)
    {
        if (partida.aplicacion == applicationID) { encontradas.push_back(partida); }
    }
    return encontradas;
}

bool partidas::hay_para(uint64_t applicationID)
{
    std::lock_guard guardia{s_candado};
    for (const partidas::Partida &partida : s_catalogo)
    {
        if (partida.aplicacion == applicationID) { return true; }
    }
    return false;
}

int partidas::generacion() noexcept { return s_generacion.load(); }
