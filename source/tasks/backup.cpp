#include "tasks/backup.hpp"

#include "curl/curl.hpp"

#include <archive.h>
#include <archive_entry.h>

#include "appstates/MainMenuState.hpp"
#include "config/config.hpp"
#include "data/data.hpp"
#include "error.hpp"
#include "fs/fs.hpp"
#include "logging/logger.hpp"
#include "JKSV.hpp"
#include "nube.hpp"
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

        // Releer lo escrito y compararlo con el zip: un save a medias se descubre aqui, no al abrir el juego.
        task->set_status(strings::tr("Verifying the restored save...", "Verificando la partida restaurada..."));
        const int distintos = fs::verify_zip_against_directory(unzip, fs::DEFAULT_SAVE_ROOT);
        if (distintos > 0)
        {
            std::string aviso = stringutil::get_formatted_string(
                strings::tr("%d file(s) don't match the backup: the restore is probably incomplete. Your previous save was backed up first.",
                            "%d archivo(s) no coinciden con el respaldo: es probable que la restauracion haya quedado incompleta. Tu partida anterior quedo respaldada antes."),
                distintos);
            ui::PopMessageManager::push_message(POP_TICKS * 2, aviso);
        }
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

// Pasa una partida bajada (zip, 7z o rar, en memoria) a un zip normal de JKSV: solo lo que esta dentro de
// `raiz`, sin esa carpeta. Asi la restauracion es la de cualquier respaldo, meta incluido si lo trae.
// Devuelve cuantos archivos quedaron (0 = no se pudo leer o no habia nada en esa carpeta).
static int partida_a_zip(const std::string &datos, const std::string &raiz, const fslib::Path &zipPath, int64_t &bytes, bool &conMeta)
{
    // Solo los formatos de las partidas (zip, 7z, rar). format_all tambien activa xar, mtree, cab, warc y
    // otros que no hacen falta y que pueden pedir servicios que en el Album no estan.
    struct archive *lector = archive_read_new();
    archive_read_support_format_zip(lector);
    archive_read_support_format_7zip(lector);
    archive_read_support_format_rar(lector);
    archive_read_support_format_rar5(lector);
    if (archive_read_open_memory(lector, datos.data(), datos.size()) != ARCHIVE_OK)
    {
        logger::log("partida: libarchive no lo abre: %s", archive_error_string(lector) ? archive_error_string(lector) : "?");
        archive_read_free(lector);
        return 0;
    }

    fs::MiniZip zip{zipPath};
    if (!zip.is_open())
    {
        archive_read_free(lector);
        return 0;
    }

    int archivos = 0;
    bool fallo   = false;
    bytes        = 0;
    conMeta      = false;
    std::vector<char> bufer(0x10000);
    struct archive_entry *entrada{};
    while (!fallo && archive_read_next_header(lector, &entrada) == ARCHIVE_OK)
    {
        if (archive_entry_filetype(entrada) != AE_IFREG) { continue; }

        const char *nombre = archive_entry_pathname(entrada);
        if (!nombre) { continue; }
        std::string ruta{nombre};
        std::replace(ruta.begin(), ruta.end(), '\\', '/');
        while (ruta.rfind("./", 0) == 0) { ruta.erase(0, 2); }
        if (!raiz.empty())
        {
            if (ruta.rfind(raiz, 0) != 0) { continue; }
            ruta.erase(0, raiz.size());
        }
        // Nada que salga de la carpeta del save.
        if (ruta.empty() || ruta.front() == '/' || ruta.find("..") != ruta.npos) { continue; }

        if (ruta == fs::NAME_SAVE_META) { conMeta = true; }
        else { bytes += archive_entry_size(entrada); }
        if (!zip.open_new_file(ruta)) { fallo = true; break; }
        la_ssize_t leidos{};
        while ((leidos = archive_read_data(lector, bufer.data(), bufer.size())) > 0)
        {
            if (!zip.write(bufer.data(), static_cast<size_t>(leidos))) { fallo = true; break; }
        }
        if (leidos < 0) { fallo = true; }
        zip.close_current_file();
        if (!fallo) { ++archivos; }
    }

    archive_read_free(lector);
    zip.close();
    return fallo ? 0 : archivos;
}

void tasks::backup::restore_partida(sys::threadpool::JobData taskData)
{
    auto castData = std::static_pointer_cast<BackupMenuState::DataStruct>(taskData);

    sys::ProgressTask *task        = static_cast<sys::ProgressTask *>(castData->task);
    data::User *user               = castData->user;
    data::TitleInfo *titleInfo     = castData->titleInfo;
    const FsSaveDataInfo *saveInfo = castData->saveInfo;

    if (error::is_null(task)) { return; }
    else if (error::is_null({user, titleInfo, saveInfo, castData->basePath}) || castData->url.empty()) { TASK_FINISH_RETURN(task); }

    // 1) Bajar el archivo a memoria: una partida pesa pocos MB. Asi un corte no deja un zip a medias en la SD.
    task->set_status(strings::tr("Downloading full save...", "Descargando la partida completa..."));
    logger::log("partida: bajando %s", castData->url.c_str());
    std::string contenido{};
    {
        curl::Handle handle = curl::new_handle();
        curl::prepare_get(handle);
        curl::set_option(handle, CURLOPT_URL, castData->url.c_str());
        curl::set_option(handle, CURLOPT_WRITEFUNCTION, curl::write_response_string);
        curl::set_option(handle, CURLOPT_WRITEDATA, &contenido);
        curl::set_option(handle, CURLOPT_FOLLOWLOCATION, 1L);
        const bool bajo = curl::perform(handle) && curl::get_response_code(handle) == 200 && !contenido.empty();
        logger::log("partida: http %li, %zu bytes", curl::get_response_code(handle), contenido.size());
        if (!bajo)
        {
            ui::PopMessageManager::push_message(POP_TICKS,
                                                strings::tr("Couldn't download the save. Probably no connection; try again.",
                                                            "No se pudo bajar la partida. Es probable que no haya conexion; intenta de nuevo."));
            TASK_FINISH_RETURN(task);
        }
    }

    // 2) Pasarla a un zip de JKSV junto a los respaldos del juego: queda en la lista y se puede volver a
    //    restaurar. Un archivo que no se puede leer (una pagina de error en vez del archivo) o sin nada en su
    //    carpeta no debe borrar la partida actual.
    const fslib::Path &basePath = *castData->basePath;
    if (!fslib::directory_exists(basePath)) { error::fslib(fslib::create_directories_recursively(basePath)); }
    const std::string fecha   = stringutil::get_date_string();
    const fslib::Path zipPath = basePath / stringutil::get_formatted_string("PARTIDA OQB - %s.zip", fecha.c_str());
    int64_t bytes{};
    bool conMeta{};
    logger::log("partida: abriendo el archivo (raiz \"%s\")", castData->raiz.c_str());
    const int archivos        = partida_a_zip(contenido, castData->raiz, zipPath, bytes, conMeta);
    logger::log("partida: %d archivos, %lld bytes, meta %d", archivos, static_cast<long long>(bytes), conMeta ? 1 : 0);
    contenido.clear();
    contenido.shrink_to_fit();
    if (archivos == 0)
    {
        error::fslib(fslib::delete_file(zipPath));
        ui::PopMessageManager::push_message(POP_TICKS,
                                            strings::tr("The downloaded file is probably not a valid save.",
                                                        "Es probable que lo descargado no sea una partida valida."));
        TASK_FINISH_RETURN(task);
    }

    // 2b) Sin meta, nada agranda el save: si la partida no cabe en el de esta consola, la copia falla a mitad
    //     (Mario Kart 8, Zelda). Se agranda solo lo necesario, como Checkpoint 5.3: el contenido mas un margen
    //     para la estructura del save, redondeado a 1 MB y sin pasar del maximo del juego.
    if (!conMeta)
    {
        FsSaveDataExtraData extra{};
        if (fs::read_save_extra_data(saveInfo, extra))
        {
            constexpr int64_t MB    = 0x100000;
            const uint8_t tipo      = saveInfo->save_data_type;
            const int64_t maximo    = titleInfo->get_save_data_size_max(tipo);
            int64_t necesario       = bytes + bytes / 8 + MB;
            necesario               = (necesario + MB - 1) / MB * MB;
            if (maximo > 0 && necesario > maximo) { necesario = maximo; }
            if (extra.data_size < necesario && !fs::extend_save_data(saveInfo, necesario, extra.journal_size))
            {
                ui::PopMessageManager::push_message(POP_TICKS,
                                                    strings::tr("Couldn't enlarge the save: the restore will probably fail for lack of space.",
                                                                "No se pudo agrandar el save: es probable que la restauracion falle por falta de espacio."));
            }
        }
    }

    logger::log("partida: respaldando la actual");
    // 3) Respaldo de la partida actual siempre, aunque el respaldo automatico al restaurar este apagado: es lo
    //    unico que permite volver atras. restore_backup_local lo hace solo si esa opcion esta encendida.
    if (!config::get_by_key(config::keys::AUTO_BACKUP_ON_RESTORE))
    {
        castData->path = zipPath;
        auto_backup(task, castData);
    }

    logger::log("partida: restaurando");
    // 4) Restaurar como cualquier respaldo local: borra el save, aplica el meta si lo trae y extrae.
    castData->path     = zipPath;
    castData->killTask = true;
    tasks::backup::restore_backup_local(castData);
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

// Smart sync: returns true if the cloud backup should NOT overwrite the local save, because the local save is
// genuinely newer (by the save's real timestamp stored in the backup meta, not the backup's file date). This
// protects real progress from being clobbered by a fresh backup of an older save. Off (config) => never skips.
static bool smart_skip_restore(const FsSaveDataInfo *saveInfo, fs::MiniUnzip &backupZip)
{
    if (config::get_by_key(config::keys::SMART_SYNC) == 0) { return false; }
    if (saveInfo == nullptr) { return false; }

    // Live save's last-commit (gameplay) timestamp.
    FsSaveDataExtraData liveExtra{};
    if (!fs::read_save_extra_data(saveInfo, liveExtra) || liveExtra.timestamp == 0) { return false; }

    // Cloud backup's stored save timestamp (read from its meta; locate/read won't disturb the later restore,
    // since copy_zip_to_directory resets to the first file).
    if (!backupZip.locate_file(fs::NAME_SAVE_META)) { return false; }
    fs::SaveMetaData meta{};
    backupZip.read(&meta, fs::SIZE_SAVE_META);
    if (meta.timestamp == 0) { return false; }

    // Skip the overwrite only when the cloud save is NOT newer than the live one.
    return meta.timestamp <= liveExtra.timestamp;
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

            const fslib::Path localDir{config::get_working_directory() / titleInfo->get_path_safe_title()};

            data->user      = user;
            data->titleInfo = titleInfo;
            data->saveInfo  = saveInfo;

            // 1) Download the newest cloud backup first (we need its meta to decide whether to restore).
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

            // 2) Smart sync: skip if the local save is genuinely newer (protects real progress).
            if (smart_skip_restore(saveInfo, backup))
            {
                backup.close();
                error::fslib(fslib::delete_file(tempPath));
                ++skipped;
                continue;
            }

            // 3) Forced local "PRE-SYNC" safety backup of the current save (recoverable if the restore goes wrong).
            {
                if (!fslib::directory_exists(localDir)) { error::fslib(fslib::create_directories_recursively(localDir)); }
                const std::string dateString = stringutil::get_date_string();
                std::string safetyName       = stringutil::get_formatted_string("PRE-SYNC - %s.zip", dateString.c_str());
                data->path                   = localDir / safetyName;
                tasks::backup::create_new_backup_local(data);
            }

            // 4) Wipe the live save and extract the cloud backup into it.
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

    std::string report = stringutil::get_formatted_string(strings::tr("Favorites downloaded: %d  -  skipped: %d", "Favoritos bajados: %d  -  saltados: %d"), restored, skipped);
    ui::PopMessageManager::push_message(POP_TICKS, report);

    task->complete();
}

void tasks::backup::sync_favorites_remote(sys::threadpool::JobData taskData)
{
    static constexpr const char *DOWNLOAD_PATH = "sdmc:/jksv_download.zip";

    auto datos              = std::static_pointer_cast<nube::DatosSync>(taskData);
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(datos->task);
    if (error::is_null(task)) { return; }

    auto terminar = [&]() {
        nube::invalidar();
        task->complete();
        if (datos->salirAlTerminar) { JKSV::request_quit(); }
    };

    remote::Storage *remote = remote::get_remote_storage();
    if (!remote)
    {
        ui::PopMessageManager::push_message(POP_TICKS, strings::tr("Couldn't reach your cloud: nothing was synced.",
                                                                   "No se pudo llegar a tu nube: no se sincronizó nada."));
        terminar();
        return;
    }

    task->set_status(strings::tr("Checking the cloud…", "Consultando la nube…"));
    if (!remote->reload())
    {
        ui::PopMessageManager::push_message(POP_TICKS, strings::tr("The cloud didn't answer: nothing was synced. Most likely the connection is slow; try again.",
                                                                   "La nube no respondió: no se sincronizó nada. Lo más probable es que la conexión esté lenta; vuelve a intentarlo."));
        terminar();
        return;
    }

    int subidos = 0, bajados = 0, alDia = 0, fallos = 0;

    auto data           = std::make_shared<BackupMenuState::DataStruct>();
    data->task          = task;
    data->killTask      = false;
    data->spawningState = nullptr;

    // Respaldo nuevo de la partida actual, subido a la carpeta del juego.
    auto subir = [&](data::User *user, const FsSaveDataInfo *saveInfo, data::TitleInfo *titleInfo) -> bool {
        {
            fs::ScopedSaveMount montaje{fs::DEFAULT_SAVE_MOUNT, saveInfo};
            if (!montaje.is_open() || !fs::directory_has_contents(fs::DEFAULT_SAVE_ROOT)) { return false; }
        }
        const std::string carpeta = nube::carpeta_de(remote, titleInfo);
        remote->return_to_root();
        if (!remote->directory_exists(carpeta) && !remote->create_directory(carpeta)) { return false; }
        remote->change_directory(remote->get_directory_by_name(carpeta));

        const std::string fecha = stringutil::get_date_string();
        data->user              = user;
        data->saveInfo          = saveInfo;
        data->titleInfo         = titleInfo;
        data->remoteName        = stringutil::get_formatted_string("%s - %s.zip", user->get_path_safe_nickname(), fecha.c_str());
        tasks::backup::create_new_backup_remote(data);

        const bool subio = remote->file_exists(data->remoteName);
        remote->return_to_root();
        if (subio) { nube::registrar(user, saveInfo->application_id, data->remoteName, nube::marca_partida(saveInfo)); }
        return subio;
    };

    data::UserList &usuarios = datos->usuarios;
    for (data::User *user : usuarios)
    {
        if (user->get_account_save_type() == FsSaveDataType_System) { continue; }
        const int64_t total = user->get_total_data_entries();
        for (int64_t i = 0; i < total; i++)
        {
            const FsSaveDataInfo *saveInfo = user->get_save_info_at(i);
            if (error::is_null(saveInfo) || !config::is_favorite(saveInfo->application_id)) { continue; }
            data::TitleInfo *titleInfo = data::get_title_info_by_id(saveInfo->application_id);
            if (error::is_null(titleInfo)) { continue; }

            task->set_status(std::string{titleInfo->get_title()});
            const nube::Accion accion = nube::decidir(remote, user, saveInfo, titleInfo);

            if (accion == nube::Accion::AlDia) { ++alDia; continue; }
            if (accion == nube::Accion::Subir)
            {
                subir(user, saveInfo, titleInfo) ? ++subidos : ++fallos;
                continue;
            }
            if (datos->soloSubir) { continue; } // al salir no se baja nada

            // Bajar o conflicto: se trae el respaldo y se compara la marca de tiempo guardada en el con la de aqui.
            remote::Item *nuevo = nube::mas_nuevo(remote, nube::carpeta_de(remote, titleInfo));
            if (!nuevo) { ++fallos; continue; }
            const std::string nombreNube{nuevo->get_name()};

            const fslib::Path temporal{DOWNLOAD_PATH};
            if (!remote->download_file(nuevo, temporal, task))
            {
                ++fallos;
                continue;
            }
            fs::MiniUnzip respaldo{temporal};
            if (!respaldo.is_open())
            {
                fslib::delete_file(temporal);
                ++fallos;
                continue;
            }

            uint64_t marcaNube = 0;
            if (respaldo.locate_file(fs::NAME_SAVE_META))
            {
                fs::SaveMetaData meta{};
                respaldo.read(&meta, fs::SIZE_SAVE_META);
                marcaNube = meta.timestamp;
            }
            const uint64_t marcaAqui = nube::marca_partida(saveInfo);

            if (marcaNube != 0 && marcaNube == marcaAqui)
            {
                // Es la misma partida (p. ej. la subio esta consola antes de existir el registro).
                respaldo.close();
                fslib::delete_file(temporal);
                nube::registrar(user, saveInfo->application_id, nombreNube, marcaAqui);
                ++alDia;
                continue;
            }
            if (marcaNube != 0 && marcaAqui > marcaNube)
            {
                // La de aqui es mas nueva: se sube en vez de bajar.
                respaldo.close();
                fslib::delete_file(temporal);
                subir(user, saveInfo, titleInfo) ? ++subidos : ++fallos;
                continue;
            }

            // La de la nube es mas nueva (o no dice su fecha): PRE-SYNC de la actual y se reemplaza.
            data->user      = user;
            data->titleInfo = titleInfo;
            data->saveInfo  = saveInfo;
            {
                const fslib::Path carpetaLocal{config::get_working_directory() / titleInfo->get_path_safe_title()};
                if (!fslib::directory_exists(carpetaLocal)) { error::fslib(fslib::create_directories_recursively(carpetaLocal)); }
                const std::string fecha = stringutil::get_date_string();
                data->path              = carpetaLocal / stringutil::get_formatted_string("PRE-SYNC - %s.zip", fecha.c_str());
                tasks::backup::create_new_backup_local(data);
            }
            {
                auto montaje = create_scoped_mount(saveInfo);
                error::fslib(fslib::delete_directory_recursively(fs::DEFAULT_SAVE_ROOT));
                error::fslib(fslib::commit_data_to_file_system(fs::DEFAULT_SAVE_MOUNT));
            }
            read_and_process_meta(respaldo, data, task);
            {
                FsSaveDataExtraData extra{};
                const bool leido          = fs::read_save_extra_data(saveInfo, extra);
                const int64_t journalSize = leido ? extra.journal_size : titleInfo->get_journal_size(user->get_account_save_type());
                auto montaje              = create_scoped_mount(saveInfo);
                fs::copy_zip_to_directory(respaldo, fs::DEFAULT_SAVE_ROOT, journalSize, task);
            }
            respaldo.close();
            fslib::delete_file(temporal);
            nube::registrar(user, saveInfo->application_id, nombreNube, nube::marca_partida(saveInfo));
            ++bajados;
        }
    }
    remote->return_to_root();

    std::string informe = strings::tr("Synced: ", "Sincronizado: ") + std::to_string(subidos) + strings::tr(" up, ", " subidos, ") +
                          std::to_string(bajados) + strings::tr(" down, ", " bajados, ") + std::to_string(alDia) +
                          strings::tr(" up to date", " al día");
    if (fallos > 0) { informe += strings::tr(" · ", " · ") + std::to_string(fallos) + strings::tr(" failed", " fallaron"); }
    ui::PopMessageManager::push_message(POP_TICKS * 2, informe);

    terminar();
}

// Shared restore core for the cloud sweep: download the item, optionally protect a newer local save, make a
// PRE-SYNC safety backup, wipe the live save and extract. data must have a valid user/titleInfo/saveInfo/task
// (no spawning state). applySmartCheck=false forces the restore (e.g. for a save container just created).
static bool restore_cloud_item_into_save(BackupMenuState::TaskData data, remote::Item *latest, bool applySmartCheck)
{
    static constexpr const char *DOWNLOAD_PATH = "sdmc:/jksv_download.zip";

    sys::ProgressTask *task        = static_cast<sys::ProgressTask *>(data->task);
    data::User *user               = data->user;
    data::TitleInfo *titleInfo     = data->titleInfo;
    const FsSaveDataInfo *saveInfo = data->saveInfo;
    remote::Storage *remote        = remote::get_remote_storage();

    if (error::is_null(task) || error::is_null({user, titleInfo, remote, latest}) || saveInfo == nullptr) { return false; }

    const fslib::Path localDir{config::get_working_directory() / titleInfo->get_path_safe_title()};

    // 1) Download the cloud backup first (we need its meta to decide).
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

    // 2) Smart sync: skip if the existing local save is genuinely newer (not for freshly-created containers).
    if (applySmartCheck && smart_skip_restore(saveInfo, backup))
    {
        backup.close();
        error::fslib(fslib::delete_file(tempPath));
        return false;
    }

    // 3) Forced PRE-SYNC local safety backup of the current save (recoverable if the restore goes wrong).
    {
        if (!fslib::directory_exists(localDir)) { error::fslib(fslib::create_directories_recursively(localDir)); }
        const std::string dateString = stringutil::get_date_string();
        std::string safetyName       = stringutil::get_formatted_string("PRE-SYNC - %s.zip", dateString.c_str());
        data->path                   = localDir / safetyName;
        data->killTask               = false;
        tasks::backup::create_new_backup_local(data);
    }

    // 4) Wipe the live save and extract the cloud backup into it.
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
        ui::PopMessageManager::push_message(POP_TICKS, strings::tr("No user account to restore to.", "No hay cuenta de usuario para restaurar."));
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
        bool justCreated         = false;
        FsSaveDataInfo *saveInfo = targetUser->get_save_info_by_id(applicationID);
        if (saveInfo == nullptr)
        {
            if (!fs::create_save_data_for(targetUser, titleInfo, 0)) { ++otherSkips; continue; }
            targetUser->load_user_data();
            saveInfo = targetUser->get_save_info_by_id(applicationID);
            if (saveInfo == nullptr) { ++otherSkips; continue; }
            ++createdSaves;
            justCreated = true;
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
        // A just-created (empty) container has no real progress to protect: force the restore.
        if (restore_cloud_item_into_save(data, latest, !justCreated)) { ++restored; }
        else { ++otherSkips; }
    }

    std::string report = stringutil::get_formatted_string(strings::tr("Restored: %d (saves created: %d)  -  not installed: %d", "Restaurados: %d (saves creados: %d)  -  no instalados: %d"),
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
