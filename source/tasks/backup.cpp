#include "tasks/backup.hpp"

#include "appstates/MainMenuState.hpp"
#include "config/config.hpp"
#include "data/data.hpp"
#include "error.hpp"
#include "fs/fs.hpp"
#include "logging/logger.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"
#include "ui/PopMessageManager.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // This is used in various places for appending and checking.
    constexpr const char *STRING_ZIP_EXT = ".zip";

    // I got tired of typing out the DEFAULT_TICKS.
    constexpr int POP_TICKS = ui::PopMessageManager::DEFAULT_TICKS;

    // Backups are named "[prefix] - YYYY-MM-DD_HH-MM-SS[.zip]"; that 19-char stamp sorts chronologically as text.
    std::string retention_sort_key(std::string_view name)
    {
        if (name.size() > 4 && name.substr(name.size() - 4) == STRING_ZIP_EXT) { name = name.substr(0, name.size() - 4); }
        constexpr size_t stamp = 19; // YYYY-MM-DD_HH-MM-SS
        return name.size() >= stamp ? std::string(name.substr(name.size() - stamp)) : std::string(name);
    }

    // Keeps only the newest `keep` local backups in `directory`; older ones go to the trash bin (or are deleted).
    void prune_local_backups(const fslib::Path &directory, int keep)
    {
        if (keep <= 0) { return; }

        fslib::Directory listing{directory, false};
        if (!listing.is_open()) { return; }

        const int count = static_cast<int>(listing.get_count());
        if (count <= keep) { return; }

        std::vector<int> order;
        order.reserve(count);
        for (int i = 0; i < count; i++) { order.push_back(i); }
        std::sort(order.begin(),
                  order.end(),
                  [&](int a, int b)
                  { return retention_sort_key(listing[a].get_filename()) > retention_sort_key(listing[b].get_filename()); });

        const bool trash = config::get_by_key(config::keys::ENABLE_TRASH_BIN);
        for (int i = keep; i < count; i++)
        {
            const fslib::Path target{directory / listing[order[i]].get_filename()};
            const bool isDir = fslib::directory_exists(target);
            if (trash)
            {
                const fslib::Path trashPath{config::get_working_directory() / "_TRASH_" / listing[order[i]].get_filename()};
                if (isDir) { error::fslib(fslib::rename_directory(target, trashPath)); }
                else { error::fslib(fslib::rename_file(target, trashPath)); }
            }
            else if (isDir) { error::fslib(fslib::delete_directory_recursively(target)); }
            else { error::fslib(fslib::delete_file(target)); }
        }
    }

    // Keeps only the newest `keep` files in the remote's current folder; older ones are deleted from the server.
    void prune_remote_backups(remote::Storage *remote, int keep)
    {
        if (keep <= 0 || !remote) { return; }

        remote::Storage::DirectoryListing listing;
        remote->get_directory_listing(listing);

        std::vector<remote::Item *> files;
        for (remote::Item *item : listing)
        {
            if (!item->is_directory()) { files.push_back(item); }
        }
        if (static_cast<int>(files.size()) <= keep) { return; }

        std::sort(files.begin(),
                  files.end(),
                  [](const remote::Item *a, const remote::Item *b)
                  { return retention_sort_key(a->get_name()) > retention_sort_key(b->get_name()); });

        // Collect IDs first: delete_item mutates the list and would invalidate these pointers mid-loop.
        std::vector<std::string> toDelete;
        for (size_t i = static_cast<size_t>(keep); i < files.size(); i++) { toDelete.emplace_back(files[i]->get_id()); }
        for (const std::string &id : toDelete)
        {
            remote::Item *item = remote->get_item_by_id(id);
            if (item) { remote->delete_item(item); }
        }
    }
}

// Definitions at bottom.
static void auto_backup(sys::ProgressTask *task, BackupMenuState::TaskData taskData);
static bool read_and_process_meta(const fslib::Path &targetDir, BackupMenuState::TaskData taskData, sys::ProgressTask *task);
static bool read_and_process_meta(fs::MiniUnzip &unzip, BackupMenuState::TaskData taskData, sys::ProgressTask *task);
static void write_meta_file(const fslib::Path &target, const FsSaveDataInfo *saveInfo);
static void write_meta_zip(fs::MiniZip &zip, const FsSaveDataInfo *saveInfo);
static fs::ScopedSaveMount create_scoped_mount(const FsSaveDataInfo *saveInfo);

void tasks::backup::create_new_backup_local(sys::threadpool::JobData taskData)
{
    // Cast data to what we actually use.
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack to pointers and references for easier access.
    // Task.
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // Data.
    data::User *user               = castData->user;
    data::TitleInfo *titleInfo     = castData->titleInfo;
    const FsSaveDataInfo *saveInfo = castData->saveInfo;

    // Filesystem/path.
    const fslib::Path &path = castData->path;

    // State to update.
    BackupMenuState *spawningState = castData->spawningState;

    // Whether or not to signal completion at the end.
    const bool killTask = castData->killTask;

    // If anything is invalid, bail.
    if (error::is_null(task)) { return; }
    else if (error::is_null({user, titleInfo, saveInfo}) || !path.is_valid()) { TASK_FINISH_RETURN(task); }

    // Check if the path has the zip extension. Scoped so the string doesn't linger. Not the best way to detect this btw.
    bool hasZipExt{};
    {
        const std::string pathString = path.string();
        hasZipExt                    = pathString.find(STRING_ZIP_EXT) != pathString.npos;
    }

    // If it has the zip extension
    if (hasZipExt) // At this point, this should have the zip extension appended if needed.
    {
        fs::MiniZip zip{path};
        if (!zip.is_open()) { TASK_FINISH_RETURN(task); }

        write_meta_zip(zip, saveInfo);
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_directory_to_zip(fs::DEFAULT_SAVE_ROOT, zip, task);
    }
    else
    {
        // Create the directory if needed.
        const bool needsDir    = !fslib::directory_exists(path);
        const bool createError = needsDir && error::fslib(fslib::create_directory(path));
        if (needsDir && createError) { TASK_FINISH_RETURN(task); }

        write_meta_file(path, saveInfo);
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_directory(fs::DEFAULT_SAVE_ROOT, path, task);
    }

    // Prune old local backups for this game if retention is enabled.
    {
        const int keep = config::get_by_key(config::keys::BACKUP_RETENTION);
        if (keep > 0 && castData->basePath && castData->basePath->is_valid()) { prune_local_backups(*castData->basePath, keep); }
    }

    // This is like this so I can reuse this code.
    if (spawningState) { spawningState->refresh(); }
    if (killTask) { task->complete(); }
}

void tasks::backup::create_new_backup_remote(sys::threadpool::JobData taskData)
{
    // This is the temporary name for the backup.
    static constexpr const char *BACKUP_PATH = "sdmc:/jksv_backup.zip";

    // Cast
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack
    // Task.
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // Data.
    data::User *user               = castData->user;
    data::TitleInfo *titleInfo     = castData->titleInfo;
    const FsSaveDataInfo *saveInfo = castData->saveInfo;

    // FS
    const fslib::Path &path = castData->path;

    // Remote
    remote::Storage *remote       = remote::get_remote_storage();
    const std::string &remoteName = castData->remoteName;

    // State.
    BackupMenuState *spawningState = castData->spawningState;

    // Whether or not to signal.
    const bool killTask = castData->killTask;

    // Whether or not to keep and move the backup.
    const bool keepLocal = config::get_by_key(config::keys::KEEP_LOCAL_BACKUPS);

    // Valid check.
    if (error::is_null(task)) { return; }
    else if (error::is_null({user, titleInfo, remote, saveInfo})) { TASK_FINISH_RETURN(task); }

    // This path is conditional and changes depending on whether or not the keep local option is toggled.
    const fslib::Path zipPath{keepLocal ? path : BACKUP_PATH};

    // Attempt to open the ZIP.
    fs::MiniZip zip{zipPath};
    if (!zip.is_open())
    {
        const char *popErrorCreating = strings::get_by_name(strings::names::BACKUPMENU_POPS, 5);
        ui::PopMessageManager::push_message(POP_TICKS, popErrorCreating);
        TASK_FINISH_RETURN(task);
    }

    // Write meta, backup
    write_meta_zip(zip, saveInfo);
    {
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_directory_to_zip(fs::DEFAULT_SAVE_ROOT, zip, task);
    }
    zip.close();

    // Scoped, update status of the task.
    {
        const char *uploadFormat = strings::get_by_name(strings::names::IO_STATUSES, 5);
        std::string status       = stringutil::get_formatted_string(uploadFormat, remoteName.data());
        task->set_status(status);
    }

    // Upload the file.
    const bool uploaded = remote->upload_file(zipPath, remoteName, task);
    // Delete if desired.
    const bool deleteError = uploaded && !keepLocal && error::fslib(fslib::delete_file(zipPath));
    if (!uploaded || deleteError)
    {
        const char *popErrorUploading = strings::get_by_name(strings::names::BACKUPMENU_POPS, 10);
        ui::PopMessageManager::push_message(POP_TICKS, popErrorUploading);
    }

    // Prune old remote backups for this game if retention is enabled (current folder is this game's).
    {
        const int keep = config::get_by_key(config::keys::BACKUP_RETENTION);
        if (uploaded && keep > 0) { prune_remote_backups(remote, keep); }
    }

    if (spawningState) { spawningState->refresh(); }
    if (killTask) { task->complete(); }
}

void tasks::backup::overwrite_backup_local(sys::threadpool::JobData taskData)
{
    // Cast
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack
    // Task.
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // FS
    const fslib::Path &path = castData->path;

    // Bail if invalid.
    if (error::is_null(task) || !path.is_valid()) { return; }

    // If the backup is a directory, try to delete it. If not, delete the zip.
    const bool isDirectory = fslib::directory_exists(path);
    const bool dirFailed   = isDirectory && error::fslib(fslib::delete_directory_recursively(path));
    const bool fileFailed  = !isDirectory && error::fslib(fslib::delete_file(path));
    // If deletion of the target backup failed, pop, finish.
    if (dirFailed && fileFailed)
    {
        const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 4);
        ui::PopMessageManager::push_message(POP_TICKS, popError);
        TASK_FINISH_RETURN(task);
    }

    // Ensure the new backup kills the task.
    castData->killTask = true;

    tasks::backup::create_new_backup_local(castData);
}

void tasks::backup::overwrite_backup_remote(sys::threadpool::JobData taskData)
{
    // This is the temporary path for patch backups.
    static constexpr const char *PATCH_PATH = "sdmc:/jksv_patch.zip";

    // Cast.
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack.
    // Task.
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // Data.
    const FsSaveDataInfo *saveInfo = castData->saveInfo;

    // Remote.
    remote::Storage *remote = remote::get_remote_storage();
    remote::Item *target    = castData->remoteItem;

    // Bail if invalid.
    if (error::is_null(task)) { return; }
    else if (error::is_null({remote, target})) { TASK_FINISH_RETURN(task); }

    // This is our temporary path to work with.
    const fslib::Path tempPath{PATCH_PATH};

    // Create the ZIP.
    fs::MiniZip zip{tempPath};
    if (!zip.is_open()) { TASK_FINISH_RETURN(task); }

    // Backup and close.
    write_meta_zip(zip, saveInfo);
    {
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_directory_to_zip(fs::DEFAULT_SAVE_ROOT, zip, task);
    }
    zip.close();

    // Scoped. Update status to uploading.
    {
        const char *targetName   = target->get_name().data();
        const char *statusFormat = strings::get_by_name(strings::names::IO_STATUSES, 5);
        std::string status       = stringutil::get_formatted_string(statusFormat, targetName);
        task->set_status(status);
    }
    // Patch the backup.
    remote->patch_file(target, tempPath, task);

    // Delete the temporary local backup.
    const bool deleteError = error::fslib(fslib::delete_file(tempPath));
    if (deleteError)
    {
        const char *popErrorDeleting = strings::get_by_name(strings::names::BACKUPMENU_POPS, 4);
        ui::PopMessageManager::push_message(POP_TICKS, popErrorDeleting);
    }

    task->complete();
}

void tasks::backup::restore_backup_local(sys::threadpool::JobData taskData)
{
    // Cast
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack
    // Task
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // Data
    data::User *user               = castData->user;
    data::TitleInfo *titleInfo     = castData->titleInfo;
    const FsSaveDataInfo *saveInfo = castData->saveInfo;

    // FS
    const fslib::Path &path = castData->path;

    // State
    BackupMenuState *spawningState = castData->spawningState;

    // Bail on invalid data.
    if (error::is_null(task)) { return; }
    else if (error::is_null({user, titleInfo, saveInfo, spawningState})) { TASK_FINISH_RETURN(task); }

    // Get the journal size to work with.
    FsSaveDataExtraData extraData{};
    const uint8_t saveType    = saveInfo->save_data_type;
    const bool readExtra      = fs::read_save_extra_data(saveInfo, extraData);
    const int64_t journalSize = readExtra ? extraData.journal_size : titleInfo->get_journal_size(saveType);

    // Whether or not to create an auto-backup.
    const bool autoBackup = config::get_by_key(config::keys::AUTO_BACKUP_ON_RESTORE);

    // Wether or not the backup is a directory.
    const bool isDir = fslib::directory_exists(path);

    // Wether or not the it's a file and has the zip extension.
    bool hasZipExt{};
    {
        const std::string pathString = path.string();
        hasZipExt                    = !isDir && pathString.find(STRING_ZIP_EXT) != pathString.npos;
    }

    // Create the auto-backup.
    if (autoBackup) { auto_backup(task, castData); }

    // Wipe the current save.
    {
        auto scopedMount = create_scoped_mount(saveInfo);
        error::fslib(fslib::delete_directory_recursively(fs::DEFAULT_SAVE_ROOT));
        error::fslib(fslib::commit_data_to_file_system(fs::DEFAULT_SAVE_MOUNT));
    }

    // If it's not a folder and has the zip extension, try to restore as a ZIP.
    if (!isDir && hasZipExt)
    {
        fs::MiniUnzip unzip{path};
        if (!unzip.is_open())
        {
            const char *popErrorOpenZip = strings::get_by_name(strings::names::EXTRASMENU_POPS, 7);
            ui::PopMessageManager::push_message(POP_TICKS, popErrorOpenZip);
            TASK_FINISH_RETURN(task);
        }

        read_and_process_meta(unzip, castData, task);
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_zip_to_directory(unzip, fs::DEFAULT_SAVE_ROOT, journalSize, task);
    }
    else if (isDir)
    {
        // Directory restore.
        read_and_process_meta(path, castData, task);
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_directory_commit(path, fs::DEFAULT_SAVE_ROOT, journalSize, task);
    }
    else
    {
        // Just copy the file.
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_file_commit(path, fs::DEFAULT_SAVE_ROOT, journalSize, task);
    }

    // Signal data was written if it previously wasn't and refresh it.
    if (spawningState)
    {
        spawningState->save_data_written();
        spawningState->refresh();
    }

    task->complete();
}

void tasks::backup::restore_backup_remote(sys::threadpool::JobData taskData)
{
    // Download path.
    static constexpr const char *DOWNLOAD_PATH = "sdmc:/jksv_download.zip";

    // Cast.
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack.
    // Task
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // Data.
    data::User *user               = castData->user;
    data::TitleInfo *titleInfo     = castData->titleInfo;
    const FsSaveDataInfo *saveInfo = castData->saveInfo;

    // State.
    BackupMenuState *spawningState = castData->spawningState;

    // Remote.
    remote::Storage *remote    = remote::get_remote_storage();
    const remote::Item *target = castData->remoteItem;

    // Whether or not an auto backup is needed.
    const bool autoBackup = config::get_by_key(config::keys::AUTO_BACKUP_ON_RESTORE);

    // Invalid, bail.
    if (error::is_null(task)) { return; }
    else if (error::is_null({user, titleInfo, saveInfo, remote, target})) { TASK_FINISH_RETURN(task); }

    // Temporary file path for download.
    const fslib::Path tempPath{DOWNLOAD_PATH};

    // Scoped status update.
    {
        const char *targetName   = target->get_name().data();
        const char *statusFormat = strings::get_by_name(strings::names::IO_STATUSES, 4);
        std::string status       = stringutil::get_formatted_string(statusFormat, targetName);

        task->set_status(status);
    }

    // Download the file first. Only continue if it succeeds.
    const bool downloaded = remote->download_file(target, tempPath, task);
    if (!downloaded)
    {
        const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 9);
        ui::PopMessageManager::push_message(POP_TICKS, popError);
        TASK_FINISH_RETURN(task);
    }

    // Attempt to open the downloaded file. It it fails, don't continue.
    fs::MiniUnzip backup{tempPath};
    if (!backup.is_open())
    {
        const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 3);
        ui::PopMessageManager::push_message(POP_TICKS, popError);
        TASK_FINISH_RETURN(task);
    }

    // Create the autobackup.
    if (autoBackup) { auto_backup(task, castData); }

    // Clear the save container.
    {
        auto scopedMount       = create_scoped_mount(saveInfo);
        const bool deleteError = error::fslib(fslib::delete_directory_recursively(fs::DEFAULT_SAVE_ROOT));
        const bool commitError = error::fslib(fslib::commit_data_to_file_system(fs::DEFAULT_SAVE_MOUNT));
        if (deleteError || commitError)
        {
            const char *popErrorResetting = strings::get_by_name(strings::names::BACKUPMENU_POPS, 2);
            ui::PopMessageManager::push_message(POP_TICKS, popErrorResetting);
            TASK_FINISH_RETURN(task);
        }
    }

    // Read the meta from the backup.
    read_and_process_meta(backup, castData, task);
    {
        // Get journal size.
        FsSaveDataExtraData extraData{};
        const bool readExtra      = fs::read_save_extra_data(saveInfo, extraData);
        const uint8_t saveType    = user->get_account_save_type();
        const int64_t journalSize = readExtra ? extraData.journal_size : titleInfo->get_journal_size(saveType);

        // Temp mount the save, back it up.
        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_zip_to_directory(backup, fs::DEFAULT_SAVE_ROOT, journalSize, task);
    }
    backup.close();

    // Delete the temporary downloaded file.
    const bool deleteError = error::fslib(fslib::delete_file(tempPath));
    if (deleteError)
    {
        const char *popErrorDeleting = strings::get_by_name(strings::names::BACKUPMENU_POPS, 4);
        ui::PopMessageManager::push_message(POP_TICKS, popErrorDeleting);
    }

    spawningState->save_data_written();
    task->complete();
}

void tasks::backup::download_favorites_remote(sys::threadpool::JobData taskData)
{
    static constexpr const char *DOWNLOAD_PATH = "sdmc:/jksv_download.zip";

    auto castData = std::static_pointer_cast<MainMenuState::DataStruct>(taskData);

    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);
    if (error::is_null(task)) { return; }

    remote::Storage *remote = remote::get_remote_storage();
    if (error::is_null(remote)) { TASK_FINISH_RETURN(task); }

    int restored = 0;
    int skipped  = 0;

    // Reused per game. No spawning state on purpose: the helpers we call must not dereference it.
    auto data           = std::make_shared<BackupMenuState::DataStruct>();
    data->task          = task;
    data->killTask      = false;
    data->spawningState = nullptr;

    const data::UserList &userList = castData->userList;
    for (data::User *user : userList)
    {
        if (user->get_account_save_type() == FsSaveDataType_System) { continue; }

        const int64_t titleCount = user->get_total_data_entries();
        for (int64_t i = 0; i < titleCount; i++)
        {
            const FsSaveDataInfo *saveInfo = user->get_save_info_at(i);
            if (error::is_null(saveInfo)) { continue; }

            // Favorites only.
            const uint64_t applicationID = saveInfo->application_id;
            if (!config::is_favorite(applicationID)) { continue; }

            data::TitleInfo *titleInfo = data::get_title_info_by_id(applicationID);
            if (error::is_null(titleInfo)) { continue; }

            // Resolve this game's remote folder with a fresh server query.
            remote->return_to_root();
            const std::string_view remoteTitle =
                remote->supports_utf8() ? titleInfo->get_title() : titleInfo->get_path_safe_title();
            if (!remote->directory_exists(remoteTitle)) { ++skipped; continue; }
            remote->reload_folder(remoteTitle);

            remote::Item *folder = remote->get_directory_by_name(remoteTitle);
            if (!folder) { ++skipped; continue; }
            remote->change_directory(folder);

            // Pick the newest cloud backup by the date in its name.
            remote::Storage::DirectoryListing listing;
            remote->get_directory_listing(listing);
            remote::Item *latest = nullptr;
            std::string latestKey;
            for (remote::Item *item : listing)
            {
                if (item->is_directory()) { continue; }
                const std::string key = retention_sort_key(item->get_name());
                if (!latest || key > latestKey)
                {
                    latest    = item;
                    latestKey = key;
                }
            }
            if (!latest) { ++skipped; continue; } // no cloud backup for this game

            // Safety: don't downgrade. Skip if a local backup is already newer than the cloud one.
            const fslib::Path localDir{config::get_working_directory() / titleInfo->get_path_safe_title()};
            {
                fslib::Directory localList{localDir, false};
                std::string localLatest;
                if (localList.is_open())
                {
                    const int64_t localCount = localList.get_count();
                    for (int64_t j = 0; j < localCount; j++)
                    {
                        const std::string key = retention_sort_key(localList[j].get_filename());
                        if (key > localLatest) { localLatest = key; }
                    }
                }
                if (!localLatest.empty() && localLatest >= latestKey) { ++skipped; continue; }
            }

            data->user      = user;
            data->titleInfo = titleInfo;
            data->saveInfo  = saveInfo;

            // 1) Forced local "PRE-SYNC" safety backup of the current save (recoverable if the restore goes wrong).
            {
                if (!fslib::directory_exists(localDir)) { error::fslib(fslib::create_directories_recursively(localDir)); }
                const std::string dateString = stringutil::get_date_string();
                std::string safetyName       = stringutil::get_formatted_string("PRE-SYNC - %s.zip", dateString.c_str());
                data->path                   = localDir / safetyName;
                tasks::backup::create_new_backup_local(data);
            }

            // 2) Download the newest cloud backup.
            {
                const char *statusFormat = strings::get_by_name(strings::names::IO_STATUSES, 4);
                std::string status       = stringutil::get_formatted_string(statusFormat, titleInfo->get_title());
                task->set_status(status);
            }
            const fslib::Path tempPath{DOWNLOAD_PATH};
            if (!remote->download_file(latest, tempPath, task)) { ++skipped; continue; }

            fs::MiniUnzip backup{tempPath};
            if (!backup.is_open())
            {
                error::fslib(fslib::delete_file(tempPath));
                ++skipped;
                continue;
            }

            // 3) Wipe the live save and extract the cloud backup into it.
            {
                auto scopedMount = create_scoped_mount(saveInfo);
                error::fslib(fslib::delete_directory_recursively(fs::DEFAULT_SAVE_ROOT));
                error::fslib(fslib::commit_data_to_file_system(fs::DEFAULT_SAVE_MOUNT));
            }
            read_and_process_meta(backup, data, task);
            {
                FsSaveDataExtraData extraData{};
                const bool readExtra      = fs::read_save_extra_data(saveInfo, extraData);
                const uint8_t saveType    = user->get_account_save_type();
                const int64_t journalSize = readExtra ? extraData.journal_size : titleInfo->get_journal_size(saveType);

                auto scopedMount = create_scoped_mount(saveInfo);
                fs::copy_zip_to_directory(backup, fs::DEFAULT_SAVE_ROOT, journalSize, task);
            }
            backup.close();
            error::fslib(fslib::delete_file(tempPath));
            ++restored;
        }
    }

    remote->return_to_root();

    std::string report = stringutil::get_formatted_string("Favoritos bajados: %d  -  saltados: %d", restored, skipped);
    ui::PopMessageManager::push_message(POP_TICKS, report);

    task->complete();
}

// Shared restore core for the cloud sweep: PRE-SYNC safety backup, download the item, wipe the live save and
// extract the cloud backup into it. data must have a valid user/titleInfo/saveInfo/task (no spawning state).
static bool restore_cloud_item_into_save(BackupMenuState::TaskData data, remote::Item *latest)
{
    static constexpr const char *DOWNLOAD_PATH = "sdmc:/jksv_download.zip";

    sys::ProgressTask *task        = static_cast<sys::ProgressTask *>(data->task);
    data::User *user               = data->user;
    data::TitleInfo *titleInfo     = data->titleInfo;
    const FsSaveDataInfo *saveInfo = data->saveInfo;
    remote::Storage *remote        = remote::get_remote_storage();

    if (error::is_null(task) || error::is_null({user, titleInfo, remote, latest}) || saveInfo == nullptr) { return false; }

    const fslib::Path localDir{config::get_working_directory() / titleInfo->get_path_safe_title()};

    // 1) Forced PRE-SYNC local safety backup of the current save (recoverable if the restore goes wrong).
    {
        if (!fslib::directory_exists(localDir)) { error::fslib(fslib::create_directories_recursively(localDir)); }
        const std::string dateString = stringutil::get_date_string();
        std::string safetyName       = stringutil::get_formatted_string("PRE-SYNC - %s.zip", dateString.c_str());
        data->path                   = localDir / safetyName;
        data->killTask               = false;
        tasks::backup::create_new_backup_local(data);
    }

    // 2) Download the cloud backup.
    {
        const char *statusFormat = strings::get_by_name(strings::names::IO_STATUSES, 4);
        std::string status       = stringutil::get_formatted_string(statusFormat, titleInfo->get_title());
        task->set_status(status);
    }
    const fslib::Path tempPath{DOWNLOAD_PATH};
    if (!remote->download_file(latest, tempPath, task)) { return false; }

    fs::MiniUnzip backup{tempPath};
    if (!backup.is_open())
    {
        error::fslib(fslib::delete_file(tempPath));
        return false;
    }

    // 3) Wipe the live save and extract the cloud backup into it.
    {
        auto scopedMount = create_scoped_mount(saveInfo);
        error::fslib(fslib::delete_directory_recursively(fs::DEFAULT_SAVE_ROOT));
        error::fslib(fslib::commit_data_to_file_system(fs::DEFAULT_SAVE_MOUNT));
    }
    read_and_process_meta(backup, data, task);
    {
        FsSaveDataExtraData extraData{};
        const bool readExtra      = fs::read_save_extra_data(saveInfo, extraData);
        const uint8_t saveType    = user->get_account_save_type();
        const int64_t journalSize = readExtra ? extraData.journal_size : titleInfo->get_journal_size(saveType);

        auto scopedMount = create_scoped_mount(saveInfo);
        fs::copy_zip_to_directory(backup, fs::DEFAULT_SAVE_ROOT, journalSize, task);
    }
    backup.close();
    error::fslib(fslib::delete_file(tempPath));
    return true;
}

void tasks::backup::restore_all_from_cloud(sys::threadpool::JobData taskData)
{
    auto castData = std::static_pointer_cast<MainMenuState::DataStruct>(taskData);

    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);
    if (error::is_null(task)) { return; }

    remote::Storage *remote = remote::get_remote_storage();
    if (error::is_null(remote)) { TASK_FINISH_RETURN(task); }

    // Fresh full listing so the cloud state is current.
    remote->reload();

    // Target the first real account profile (account saves belong to a user).
    data::User *targetUser = nullptr;
    {
        data::UserList userList;
        data::get_users(userList);
        for (data::User *user : userList)
        {
            if (user->get_account_save_type() == FsSaveDataType_Account)
            {
                targetUser = user;
                break;
            }
        }
    }
    if (error::is_null(targetUser))
    {
        ui::PopMessageManager::push_message(POP_TICKS, "No hay cuenta de usuario para restaurar.");
        TASK_FINISH_RETURN(task);
    }

    const FsSaveDataType saveType = targetUser->get_account_save_type();

    // Installed titles that support this save type (includes games never launched).
    data::TitleInfoList installedTitles;
    data::get_title_info_by_type(saveType, installedTitles);

    // List the cloud's per-game folders at the root.
    remote->return_to_root();
    remote::Storage::DirectoryListing rootItems;
    remote->get_directory_listing(rootItems);

    auto data           = std::make_shared<BackupMenuState::DataStruct>();
    data->task          = task;
    data->user          = targetUser;
    data->killTask      = false;
    data->spawningState = nullptr;

    int restored = 0, createdSaves = 0, notInstalled = 0, otherSkips = 0;

    for (remote::Item *folder : rootItems)
    {
        if (!folder->is_directory()) { continue; }
        const std::string_view folderName = folder->get_name();

        // Match the cloud folder to an installed title by name (either form).
        data::TitleInfo *titleInfo = nullptr;
        for (data::TitleInfo *candidate : installedTitles)
        {
            if (folderName == candidate->get_title() || folderName == candidate->get_path_safe_title())
            {
                titleInfo = candidate;
                break;
            }
        }
        if (error::is_null(titleInfo)) { ++notInstalled; continue; }

        const uint64_t applicationID = titleInfo->get_application_id();

        // Ensure a save container exists for this user; create it if the game was never launched.
        FsSaveDataInfo *saveInfo = targetUser->get_save_info_by_id(applicationID);
        if (saveInfo == nullptr)
        {
            if (!fs::create_save_data_for(targetUser, titleInfo, 0)) { ++otherSkips; continue; }
            targetUser->load_user_data();
            saveInfo = targetUser->get_save_info_by_id(applicationID);
            if (saveInfo == nullptr) { ++otherSkips; continue; }
            ++createdSaves;
        }

        // Newest cloud backup in this folder.
        remote->change_directory(folder);
        remote::Storage::DirectoryListing files;
        remote->get_directory_listing(files);
        remote::Item *latest = nullptr;
        std::string latestKey;
        for (remote::Item *item : files)
        {
            if (item->is_directory()) { continue; }
            const std::string key = retention_sort_key(item->get_name());
            if (!latest || key > latestKey)
            {
                latest    = item;
                latestKey = key;
            }
        }
        remote->return_to_root();
        if (error::is_null(latest)) { ++otherSkips; continue; }

        data->titleInfo = titleInfo;
        data->saveInfo  = saveInfo;
        if (restore_cloud_item_into_save(data, latest)) { ++restored; }
        else { ++otherSkips; }
    }

    std::string report = stringutil::get_formatted_string("Restaurados: %d (saves creados: %d)  -  no instalados: %d",
                                                          restored,
                                                          createdSaves,
                                                          notInstalled);
    ui::PopMessageManager::push_message(POP_TICKS, report);

    task->complete();
}

void tasks::backup::delete_backup_local(sys::threadpool::JobData taskData)
{
    // Cast.
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack
    // Task
    sys::Task *task = castData->task;

    // FS
    const fslib::Path &path = castData->path;

    // State.
    BackupMenuState *spawningState = castData->spawningState;

    // Config
    const bool trashEnabled = config::get_by_key(config::keys::ENABLE_TRASH_BIN);

    // Invalid, bail.
    if (error::is_null(task)) { return; }
    else if (error::is_null(spawningState)) { TASK_FINISH_RETURN(task); }

    // Status. This is basically a flash most of the time.
    {
        const std::string pathString = path.string();
        const char *statusFormat     = strings::get_by_name(strings::names::IO_STATUSES, 3);
        std::string status           = stringutil::get_formatted_string(statusFormat, pathString.c_str());
        task->set_status(status);
    }

    // Wether or not the backup is a folder.
    const bool isDir = fslib::directory_exists(path);

    // These errors are set in the conditions and checked later.
    bool dirError{}, fileError{};
    if (trashEnabled)
    {
        const fslib::Path newPath{config::get_working_directory() / "_TRASH_" / path.get_filename()};
        dirError  = isDir && error::fslib(fslib::rename_directory(path, newPath));
        fileError = !isDir && error::fslib(fslib::rename_file(path, newPath));
    }
    else
    {
        dirError  = isDir && error::fslib(fslib::delete_directory_recursively(path));
        fileError = !isDir && error::fslib(fslib::delete_file(path));
    }

    if (dirError || fileError)
    {
        const char *popFailed = strings::get_by_name(strings::names::BACKUPMENU_POPS, 4);
        ui::PopMessageManager::push_message(POP_TICKS, popFailed);
    }

    spawningState->refresh();
    task->complete();
}

void tasks::backup::delete_backup_remote(sys::threadpool::JobData taskData)
{
    // Cast
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack
    // Task
    sys::Task *task = castData->task;

    // Remote.
    remote::Storage *remote = remote::get_remote_storage();
    remote::Item *target    = castData->remoteItem;

    // State
    BackupMenuState *spawningState = castData->spawningState;

    // Invalid, bail
    if (error::is_null(task)) { return; }
    else if (error::is_null({target, spawningState, remote})) { TASK_FINISH_RETURN(task); }

    {
        const char *targetName     = target->get_name().data();
        const char *statusTemplate = strings::get_by_name(strings::names::IO_STATUSES, 3);
        const std::string status   = stringutil::get_formatted_string(statusTemplate, targetName);
        task->set_status(status);
    }

    const bool deleted = remote->delete_item(target);
    if (!deleted)
    {
        const char *popFailed = strings::get_by_name(strings::names::BACKUPMENU_POPS, 4);
        ui::PopMessageManager::push_message(POP_TICKS, popFailed);
    }

    spawningState->refresh();
    task->complete();
}

void tasks::backup::upload_backup(sys::threadpool::JobData taskData)
{
    // Cast
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Upack.
    // Task
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // FS
    const fslib::Path &path = castData->path;

    // State
    BackupMenuState *spawningState = castData->spawningState;

    // Remote
    remote::Storage *remote = remote::get_remote_storage();

    // Invalid, bail.
    if (error::is_null(task)) { return; }
    else if (error::is_null({spawningState, remote})) { TASK_FINISH_RETURN(task); }

    // Scoped status update.
    {
        const char *filename     = path.get_filename();
        const char *statusFormat = strings::get_by_name(strings::names::IO_STATUSES, 5);
        std::string status       = stringutil::get_formatted_string(statusFormat, filename);
        task->set_status(status);
    }

    // The backup menu should've made sure the remote is pointing to the correct location.
    const bool uploaded = remote->upload_file(path, path.get_filename(), task);
    if (!uploaded)
    {
        // We're going to pop the error, but not return since we're at the end of the function anyway.
        const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 10);
        ui::PopMessageManager::push_message(POP_TICKS, popError);
    }

    spawningState->refresh();
    task->complete();
}

void tasks::backup::patch_backup(sys::threadpool::JobData taskData)
{
    // Cast
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Unpack
    // Task
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);

    // FS
    const fslib::Path &path = castData->path;

    // Remote
    remote::Item *remoteItem = castData->remoteItem;
    remote::Storage *remote  = remote::get_remote_storage();

    // State.
    BackupMenuState *spawningState = castData->spawningState;

    // Invalid, bail.
    if (error::is_null(task)) { return; }
    else if (error::is_null({remoteItem, spawningState, remote})) { TASK_FINISH_RETURN(task); }

    // Status.
    {
        const char *filename     = path.get_filename();
        const char *statusFormat = strings::get_by_name(strings::names::IO_STATUSES, 5);
        std::string status       = stringutil::get_formatted_string(statusFormat, filename);
        task->set_status(status);
    }

    remote->patch_file(remoteItem, path, task);
    task->complete();
}

void tasks::backup::reload_remote(sys::threadpool::JobData taskData)
{
    // Cast.
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    // Task.
    sys::Task *task = castData->task;

    // State.
    BackupMenuState *spawningState = castData->spawningState;

    // Remote.
    remote::Storage *remote = remote::get_remote_storage();

    // Invalid, bail.
    if (error::is_null(task)) { return; }
    else if (error::is_null({spawningState, remote})) { TASK_FINISH_RETURN(task); }

    // Status.
    task->set_status("Reloading cloud listing...");

    // Re-pull the listing from the server, then re-resolve the current title folder and rebuild the menu.
    const bool reloaded = remote->reload();
    if (!reloaded)
    {
        const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 9);
        ui::PopMessageManager::push_message(POP_TICKS, popError);
    }

    spawningState->reinitialize_remote();
    task->complete();
}

static void auto_backup(sys::ProgressTask *task, BackupMenuState::TaskData taskData)
{
    // Unpack
    // Data
    data::User *user               = taskData->user;
    data::TitleInfo *titleInfo     = taskData->titleInfo;
    const FsSaveDataInfo *saveInfo = taskData->saveInfo;

    // FS
    fslib::Path &path           = taskData->path;
    const fslib::Path &basePath = *taskData->basePath;

    // Remote
    remote::Storage *remote = remote::get_remote_storage();

    // Config
    const bool autoUpload = config::get_by_key(config::keys::AUTO_UPLOAD);
    const bool exportZip  = config::get_by_key(config::keys::EXPORT_TO_ZIP);
    const bool zip        = autoUpload || exportZip;

    // Invalid, bail.
    if (error::is_null(task)) { return; }
    else if (error::is_null({user, titleInfo, saveInfo}) || !path.is_valid()) { return; }

    // Check if the save actually has data to backup before continuing.
    {
        auto scopedMount   = create_scoped_mount(saveInfo);
        const bool hasData = fs::directory_has_contents(fs::DEFAULT_SAVE_ROOT);
        if (!scopedMount.is_open() || !hasData) { return; }
    }

    // Generate the backup name.
    const char *safeNickname     = user->get_path_safe_nickname();
    const std::string dateString = stringutil::get_date_string();
    std::string backupName       = stringutil::get_formatted_string("AUTO - %s - %s", safeNickname, dateString.c_str());
    if (zip) { backupName += STRING_ZIP_EXT; }

    // Tell the backup function it shouldn't kill the task.
    taskData->killTask = false;

    // Store and swap the path so we don't lose the original.
    fslib::Path originalPath = std::move(path);
    path                     = basePath / backupName;

    if (autoUpload && remote)
    {
        // Store this for uploading.
        taskData->remoteName = std::move(backupName);

        // Create the auto backup.
        tasks::backup::create_new_backup_remote(taskData);
    }
    else { tasks::backup::create_new_backup_local(taskData); }

    // Restore the original backup path.
    taskData->path = std::move(originalPath);
}

static bool read_and_process_meta(const fslib::Path &targetDir, BackupMenuState::TaskData taskData, sys::ProgressTask *task)
{
    // Unpack.
    // Data
    data::User *user               = taskData->user;
    data::TitleInfo *titleInfo     = taskData->titleInfo;
    const FsSaveDataInfo *saveInfo = taskData->saveInfo;

    // Invalid, bail.
    if (error::is_null(task)) { return false; }
    else if (error::is_null({user, titleInfo, saveInfo})) { return false; }

    // Update status.
    {
        const char *statusProcessing = strings::get_by_name(strings::names::BACKUPMENU_STATUS, 0);
        task->set_status(statusProcessing);
    }

    // Target meta path.
    const fslib::Path metaPath{targetDir / fs::NAME_SAVE_META};

    // Attempt to open file.
    fslib::File metaFile{metaPath, FsOpenMode_Read};
    if (!metaFile.is_open())
    {
        const char *popErrorProcessing = strings::get_by_name(strings::names::BACKUPMENU_POPS, 16);
        ui::PopMessageManager::push_message(POP_TICKS, popErrorProcessing);

        return false;
    }

    // Read meta and try to process it.
    fs::SaveMetaData metaData{};
    const bool metaRead  = metaFile.read(&metaData, fs::SIZE_SAVE_META) <= fs::SIZE_SAVE_META;
    const bool processed = metaRead && fs::process_save_meta_data(saveInfo, metaData);
    if (!metaRead || !processed)
    {
        const char *popErrorProcessing = strings::get_by_name(strings::names::BACKUPMENU_POPS, 11);
        ui::PopMessageManager::push_message(POP_TICKS, popErrorProcessing);

        return false;
    }

    // If we made it here, it probably might've worked.
    return true;
}

static bool read_and_process_meta(fs::MiniUnzip &unzip, BackupMenuState::TaskData taskData, sys::ProgressTask *task)
{
    // Unpack.
    // Data.
    data::User *user               = taskData->user;
    data::TitleInfo *titleInfo     = taskData->titleInfo;
    const FsSaveDataInfo *saveInfo = taskData->saveInfo;

    // Invalid, bail.
    if (error::is_null(task)) { return false; }
    else if (error::is_null({user, titleInfo, saveInfo})) { return false; }

    // Status
    {
        const char *statusProcessing = strings::get_by_name(strings::names::BACKUPMENU_STATUS, 0);
        task->set_status(statusProcessing);
    }

    // Try to open and read the meta file in the zip.
    fs::SaveMetaData saveMeta{};
    const bool metaFound = unzip.locate_file(fs::NAME_SAVE_META);
    if (!metaFound)
    {
        const char *popNotFound = strings::get_by_name(strings::names::BACKUPMENU_POPS, 16);
        ui::PopMessageManager::push_message(POP_TICKS, popNotFound);

        return false;
    }

    // Read & process.
    const bool metaRead      = metaFound && unzip.read(&saveMeta, fs::SIZE_SAVE_META) <= fs::SIZE_SAVE_META;
    const bool metaProcessed = metaRead && fs::process_save_meta_data(saveInfo, saveMeta);
    if (!metaRead || !metaProcessed)
    {
        const char *popErrorProcessing = strings::get_by_name(strings::names::BACKUPMENU_POPS, 11);
        ui::PopMessageManager::push_message(POP_TICKS, popErrorProcessing);

        return false;
    }

    return true;
}

static void write_meta_file(const fslib::Path &target, const FsSaveDataInfo *saveInfo)
{
    // This error is used in two places here.
    const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 8);

    // Get meta for the target save info.
    fs::SaveMetaData saveMeta{};
    const fslib::Path metaPath{target / fs::NAME_SAVE_META};
    const bool hasMeta = fs::fill_save_meta_data(saveInfo, saveMeta);

    // Open for writing.
    fslib::File metaFile{metaPath, FsOpenMode_Create | FsOpenMode_Write, fs::SIZE_SAVE_META};
    if (!metaFile.is_open() || !hasMeta)
    {
        ui::PopMessageManager::push_message(POP_TICKS, popError);
        return;
    }

    // Write the meta.
    const bool metaWritten = metaFile.write(&saveMeta, fs::SIZE_SAVE_META) <= fs::SIZE_SAVE_META;
    if (!metaWritten) { ui::PopMessageManager::push_message(POP_TICKS, popError); }
}

static void write_meta_zip(fs::MiniZip &zip, const FsSaveDataInfo *saveInfo)
{
    // Get the meta
    fs::SaveMetaData saveMeta{};
    const bool hasMeta = fs::fill_save_meta_data(saveInfo, saveMeta);

    // Open it in the zip.
    const bool openMeta = hasMeta && zip.open_new_file(fs::NAME_SAVE_META);

    // Write it.
    const bool writeMeta = openMeta && zip.write(&saveMeta, fs::SIZE_SAVE_META);

    // Close it.
    const bool closeMeta = openMeta && zip.close_current_file();

    if (hasMeta && (!openMeta || !writeMeta || !closeMeta))
    {
        const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 8);
        ui::PopMessageManager::push_message(POP_TICKS, popError);
    }
}

static fs::ScopedSaveMount create_scoped_mount(const FsSaveDataInfo *saveInfo)
{
    // Attempt to open the mount
    fs::ScopedSaveMount saveMount{fs::DEFAULT_SAVE_MOUNT, saveInfo};
    if (!saveMount.is_open())
    {
        const char *popError = strings::get_by_name(strings::names::BACKUPMENU_POPS, 14);
        ui::PopMessageManager::push_message(POP_TICKS, popError);
    }

    // Return it.
    return saveMount;
}
