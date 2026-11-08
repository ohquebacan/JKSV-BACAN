#pragma once
#include "appstates/FileOptionState.hpp"
#include "sys/sys.hpp"

namespace tasks::fileoptions
{
    /// @brief Copies the source to destination passed through taskData.
    void copy_source_to_destination(sys::threadpool::JobData taskData);

    /// @brief Deletes the source path passed through taskData
    void delete_target(sys::threadpool::JobData taskData);

    /// @brief Rediseno: copia o mueve (data->mover) cada fuente a su destino, uno tras otro.
    void copiar_varios(sys::threadpool::JobData taskData);

    /// @brief Rediseno: borra cada fuente.
    void borrar_varios(sys::threadpool::JobData taskData);
}