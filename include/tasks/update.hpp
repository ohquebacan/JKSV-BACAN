#pragma once
#include "sys/threadpool.hpp"

#include <string>

namespace tasks::update
{
    /// @brief Busca en el ultimo release de ohquebacan/JKSV-BACAN. Si es mas nuevo que esta version, avisa al menu.
    void check_for_update(sys::threadpool::JobData jobData);

    /// @brief Baja el JKSV.nro nuevo a un .tmp y, si llego completo, reemplaza al actual y cierra JKSV.
    void download_update(sys::threadpool::JobData jobData);

    /// @brief Version que corre ("2.8").
    const char *version_actual() noexcept;

    /// @brief Version del release encontrado ("2.9") y sus notas (pueden venir vacias).
    std::string version_nueva();
    std::string notas_nueva();
}
