#pragma once
#include "sys/sys.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Partidas completas del catalogo de OQB: partidas.json en OQB-HATS-PACK.
//
//   {"partidas": [{"id": "01006A800016E000", "titulo": "Todo desbloqueado",
//                  "descripcion": "...", "url": "https://.../smash.zip"}]}
//
// Cada archivo (zip, 7z o rar) es un respaldo de JKSV (con o sin su meta) o el contenido del save
// tal cual. "raiz" dice que carpeta de adentro es la partida: no se adivina, porque en unos juegos
// una carpeta sola es un envoltorio y en otros (Smash: save_data/) es parte del save. Un juego puede tener varias. "juego" (nombre) es opcional: lo usa
// OQB Updater para decir para que juegos hay. Para probar sin publicar: sdmc:/config/JKSV/partidas.json.
namespace partidas
{
    struct Partida
    {
        uint64_t aplicacion{};
        std::string titulo{};
        std::string url{};
        std::string raiz{};  // carpeta dentro del archivo que es la partida ("" = la raiz del archivo)
        std::string autor{}; // quien la hizo, para el credito al confirmar
    };

    /// @brief Descarga el catalogo una vez por sesion. Tarea del threadpool: se lanza al arrancar.
    void cargar(sys::threadpool::JobData);

    /// @brief Las partidas para ese juego (vacio sin conexion o si no hay).
    std::vector<partidas::Partida> para(uint64_t applicationID);

    /// @brief Si hay alguna para ese juego (sin copiar nada).
    bool hay_para(uint64_t applicationID);

    /// @brief Sube cada vez que se carga el catalogo: la cuadricula se vuelve a marcar al verlo cambiar.
    int generacion() noexcept;
}
