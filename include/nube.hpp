#pragma once
#include "data/data.hpp"
#include "remote/remote.hpp"
#include "sys/sys.hpp"

#include <cstdint>
#include <string>

// La seccion Nube: estado de la nube, que hace falta sincronizar y los juegos que hay alla.
//
// Para no bajar de mas, se recuerda por partida (juego + usuario) que respaldo de la nube quedo sincronizado
// y la marca de tiempo de la partida en ese momento (sdmc:/config/JKSV/nube_sync.json):
//   - la nube tiene el mismo respaldo y la partida no cambio  -> al dia
//   - la nube tiene el mismo respaldo y la partida si cambio  -> para subir
//   - la nube tiene otro respaldo (de otra consola, o sin registro) -> para bajar (al bajar se compara la marca
//     de tiempo guardada en el respaldo y, si la de la consola es mas nueva, se sube en vez de bajar)
//   - cambiaron las dos (otra consola subio y aqui tambien se jugo) -> conflicto
//   - la nube no tiene nada de ese juego -> para subir
// Es la comparacion de tres puntos de Cloudpoint (dwalker109/cloudpoint, MIT): local, nube y lo ultimo sincronizado.
namespace nube
{
    enum class Accion
    {
        Subir,
        Bajar,
        AlDia,
        Conflicto // cambiaron las dos (como Cloudpoint): al sincronizar se queda la mas nueva por fecha, con PRE-SYNC
    };

    /// @brief Lo que muestra el panel de la seccion Nube.
    struct Resumen
    {
        bool listo{};       // ya termino una consulta
        bool ok{};          // y salio bien
        std::string error{}; // por que no (si !ok)
        int favoritos{}, subir{}, bajar{}, alDia{}, conflictos{};
        int juegos{};       // carpetas en la nube
        uint64_t bytes{};   // lo que ocupan
        std::string ultimoJuego{}, ultimaClave{}; // el respaldo mas nuevo de toda la nube
    };

    /// @brief Datos para las tareas de sincronizar.
    struct DatosSync : sys::Task::DataStruct
    {
        data::UserList usuarios{};
        bool soloSubir{};      // al salir de JKSV: solo sube lo que cambio, no baja nada
        bool salirAlTerminar{};
    };

    /// @brief Datos para las tareas de un respaldo en la nube.
    struct DatosRespaldo : sys::Task::DataStruct
    {
        std::string carpeta{}, archivo{};
        fslib::Path destino{}; // carpeta local para bajarlo
    };

    /// @brief Hay un webdav.json o credenciales de Google Drive (aunque no conecte).
    bool hay_configuracion();

    /// @brief Consulta la nube en segundo plano (una lista completa) y calcula el resumen. Si ya hay una en
    /// curso no hace nada. Con avisar, al terminar sale un mensaje con el resultado (Probar conexion).
    void consultar(bool avisar);

    bool consultando() noexcept;
    Resumen resumen();

    /// @brief Marca el resumen como viejo (tras sincronizar, borrar...): se vuelve a consultar al volver a la Nube.
    void invalidar() noexcept;
    bool vigente() noexcept;

    /// @brief Nombre de la carpeta de ese juego en la nube.
    std::string carpeta_de(remote::Storage *remote, data::TitleInfo *info);

    /// @brief El respaldo mas nuevo de una carpeta (por la fecha del nombre). Deja la nube en la raiz.
    remote::Item *mas_nuevo(remote::Storage *remote, std::string_view carpeta);

    /// @brief Que hacer con esa partida. Necesita la lista de la nube ya cargada.
    Accion decidir(remote::Storage *remote, data::User *user, const FsSaveDataInfo *saveInfo, data::TitleInfo *info);

    /// @brief Anota que esa partida quedo sincronizada con ese respaldo de la nube.
    void registrar(data::User *user, uint64_t aplicacion, std::string_view respaldo, uint64_t marcaPartida);

    /// @brief Marca de tiempo de la ultima vez que se guardo la partida (0 si no se puede leer).
    uint64_t marca_partida(const FsSaveDataInfo *saveInfo);

    /// @brief "AAAA-MM-DD_HH-MM-SS" del nombre de un respaldo ("" si no tiene).
    std::string clave(std::string_view nombre);

    /// @brief "dd/mm/aaaa · hh:mm" de una clave.
    std::string fecha_legible(const std::string &clave);

    /// @brief "340 MB", "1.2 GB"...
    std::string tamano_legible(uint64_t bytes);

    // Tareas de Juegos en la nube.
    void bajar_respaldo(sys::threadpool::JobData datos);   // ProgressTask
    void borrar_respaldo(sys::threadpool::JobData datos);  // Task
    void conservar_nuevos(sys::threadpool::JobData datos); // Task: deja los 3 mas nuevos de la carpeta
}
