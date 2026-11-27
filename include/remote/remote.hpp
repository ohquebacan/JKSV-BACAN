#pragma once
#include <string>
#include "remote/Storage.hpp"
#include "sys/threadpool.hpp"

#include <memory>

namespace remote
{
    /// @brief Lo que respondio el servidor WebDAV al conectar: codigo HTTP, 0 si no hubo respuesta, -1 si no se intento.
    long ultimo_codigo_webdav() noexcept;
    void anotar_codigo_webdav(long codigo) noexcept;

    /// @brief El motivo probable de que no conecte, para mostrarlo (vacio si no hay dato).
    std::string motivo_webdav();

    // Both of these are needed in two different places.
    static constexpr std::string_view PATH_GOOGLE_DRIVE_CONFIG = "sdmc:/config/JKSV/client_secret.json";
    static constexpr std::string_view PATH_WEBDAV_CONFIG       = "sdmc:/config/JKSV/webdav.json";

    /// @brief Returns whether or not the console has an active internet connection.
    bool has_internet_connection() noexcept;

    /// @brief Initializes the remote service according to the config on the sdmc.
    void initialize(sys::threadpool::JobData jobData);

    /// @brief Returns the pointer to the Storage instance.
    remote::Storage *get_remote_storage() noexcept;
} // namespace remote
