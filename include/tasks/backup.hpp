#pragma once
#include "appstates/BackupMenuState.hpp"
#include "sys/sys.hpp"

#include <string>

namespace tasks::backup
{
    /// @brief Task/thread function executed when a new backup is created.
    void create_new_backup_local(sys::threadpool::JobData taskData);
    void create_new_backup_remote(sys::threadpool::JobData taskData);

    /// @brief Overwrites a pre-existing backup.
    void overwrite_backup_local(sys::threadpool::JobData taskData);
    void overwrite_backup_remote(sys::threadpool::JobData taskData);

    /// @brief Restores a backup
    void restore_backup_local(sys::threadpool::JobData taskData);
    void restore_backup_remote(sys::threadpool::JobData taskData);

    /// @brief Deletes a backup
    void delete_backup_local(sys::threadpool::JobData taskData);
    void delete_backup_remote(sys::threadpool::JobData taskData);

    /// @brief Uploads a backup
    void upload_backup(sys::threadpool::JobData taskData);

    /// @brief Patches a pre-existing backup on the remote storage.
    void patch_backup(sys::threadpool::JobData taskData);

    /// @brief Re-fetches the remote listing so backups made on other devices appear without restarting.
    void reload_remote(sys::threadpool::JobData taskData);
}
