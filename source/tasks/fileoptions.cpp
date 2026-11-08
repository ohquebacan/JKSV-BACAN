#include "tasks/fileoptions.hpp"

#include <algorithm>
#include <string_view>
#include "appstates/FileOptionState.hpp"
#include "ui/PopMessageManager.hpp"

#include "appstates/MessageState.hpp"
#include "error.hpp"
#include "fs/fs.hpp"
#include "fslib.hpp"
#include "logging/logger.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"

void tasks::fileoptions::copy_source_to_destination(sys::threadpool::JobData taskData)
{

    auto castData = std::static_pointer_cast<FileOptionState::DataStruct>(taskData);

    const int popTicks             = ui::PopMessageManager::DEFAULT_TICKS;
    sys::ProgressTask *task        = static_cast<sys::ProgressTask *>(castData->task);
    const fslib::Path &source      = castData->sourcePath;
    const fslib::Path &dest        = castData->destPath;
    const int64_t journalSpace     = castData->journalSize;
    FileOptionState *spawningState = castData->spawningState;

    if (error::is_null(task)) { return; }
    else if (error::is_null(spawningState)) { TASK_FINISH_RETURN(task); }

    const bool sourceIsDir = fslib::directory_exists(source);
    bool destError         = false;
    if (sourceIsDir) { destError = error::fslib(fslib::create_directories_recursively(dest)); }
    else
    {
        const size_t subDest = dest.find_last_of('/');
        if (subDest != dest.NOT_FOUND && subDest > 1)
        {
            fslib::Path subDestPath{dest.sub_path(subDest)};
            destError = error::fslib(fslib::create_directories_recursively(subDestPath));
        }
    }

    if (destError)
    {
        const char *errorFormat = strings::get_by_name(strings::names::FILEOPTION_POPS, 0);
        std::string pop         = stringutil::get_formatted_string(errorFormat, source.get_filename());
        ui::PopMessageManager::push_message(popTicks, pop);
        TASK_FINISH_RETURN(task);
    }

    const bool needsCommit = journalSpace > 0;

    if (sourceIsDir && needsCommit) { fs::copy_directory_commit(source, dest, journalSpace, task); }
    else if (!sourceIsDir && needsCommit) { fs::copy_file_commit(source, dest, journalSpace, task); }
    else if (sourceIsDir && !needsCommit) { fs::copy_directory(source, dest, task); }
    else if (!sourceIsDir && !needsCommit) { fs::copy_file(source, dest, task); }

    spawningState->update_destination();
    task->complete();
}

void tasks::fileoptions::delete_target(sys::threadpool::JobData taskData)
{
    auto castData = std::static_pointer_cast<FileOptionState::DataStruct>(taskData);

    sys::Task *task                = castData->task;
    const fslib::Path &target      = castData->sourcePath;
    const int64_t journalSpace     = castData->journalSize;
    FileOptionState *spawningState = castData->spawningState;

    if (error::is_null(task)) { return; }
    else if (error::is_null(task)) { TASK_FINISH_RETURN(task); }

    // Gonna borrow this. No point in duplicating it.
    const int popTicks         = ui::PopMessageManager::DEFAULT_TICKS;
    const char *deletingFormat = strings::get_by_name(strings::names::IO_STATUSES, 3);
    const char *errorFormat    = strings::get_by_name(strings::names::FILEOPTION_POPS, 1);

    {
        const char *filename = target.get_filename();
        std::string status   = stringutil::get_formatted_string(deletingFormat, filename);
        task->set_status(status);
    }

    const bool isDir = fslib::directory_exists(target);
    bool needsCommit = journalSpace > 0;

    bool deleteError{};
    if (isDir) { deleteError = error::fslib(fslib::delete_directory_recursively(target)); }
    else { deleteError = error::fslib(fslib::delete_file(target)); }

    if (deleteError)
    {
        const char *filename = target.get_filename();
        std::string pop      = stringutil::get_formatted_string(errorFormat, filename);
        ui::PopMessageManager::push_message(popTicks, pop);
    }

    const bool commitError = needsCommit && error::fslib(fslib::commit_data_to_file_system(target.get_device_name()));
    if (commitError)
    {
        const char *filename = target.get_filename();
        std::string pop      = stringutil::get_formatted_string(errorFormat, filename);
        ui::PopMessageManager::push_message(popTicks, pop);
    }

    spawningState->update_source();
    task->complete();
}

// ---- Rediseno: varios a la vez y mover ----

static bool copiar_uno(const fslib::Path &source, const fslib::Path &dest, int64_t journalSpace, sys::ProgressTask *task)
{
    const bool esCarpeta = fslib::directory_exists(source);
    bool error{};
    if (esCarpeta) { error = error::fslib(fslib::create_directories_recursively(dest)); }
    else
    {
        const size_t barra = dest.find_last_of('/');
        if (barra != dest.NOT_FOUND && barra > 1) { error = error::fslib(fslib::create_directories_recursively(fslib::Path{dest.sub_path(barra)})); }
    }
    if (error) { return false; }

    const bool commit = journalSpace > 0;
    if (esCarpeta && commit) { fs::copy_directory_commit(source, dest, journalSpace, task); }
    else if (!esCarpeta && commit) { fs::copy_file_commit(source, dest, journalSpace, task); }
    else if (esCarpeta) { fs::copy_directory(source, dest, task); }
    else { fs::copy_file(source, dest, task); }
    return esCarpeta ? fslib::directory_exists(dest) : fslib::file_exists(dest);
}

static bool borrar_uno(const fslib::Path &target, int64_t journalSpace)
{
    const bool esCarpeta = fslib::directory_exists(target);
    const bool error     = esCarpeta ? error::fslib(fslib::delete_directory_recursively(target)) : error::fslib(fslib::delete_file(target));
    if (journalSpace > 0) { error::fslib(fslib::commit_data_to_file_system(target.get_device_name())); }
    return !error;
}

void tasks::fileoptions::copiar_varios(sys::threadpool::JobData taskData)
{
    auto castData                  = std::static_pointer_cast<FileOptionState::DataStruct>(taskData);
    sys::ProgressTask *task        = static_cast<sys::ProgressTask *>(castData->task);
    FileOptionState *spawningState = castData->spawningState;
    if (error::is_null(task)) { return; }
    else if (error::is_null(spawningState)) { TASK_FINISH_RETURN(task); }

    const size_t total = std::min(castData->fuentes.size(), castData->destinos.size());
    int fallas         = 0;
    for (size_t i = 0; i < total; i++)
    {
        const fslib::Path &source = castData->fuentes[i];
        const fslib::Path &dest   = castData->destinos[i];
        task->set_status(stringutil::get_formatted_string(castData->mover ? strings::tr("Moving %s (%d of %d)", "Moviendo %s (%d de %d)")
                                                                          : strings::tr("Copying %s (%d of %d)", "Copiando %s (%d de %d)"),
                                                          source.get_filename(),
                                                          static_cast<int>(i + 1),
                                                          static_cast<int>(total)));

        // Mover en la misma unidad: renombrar, que es instantaneo y no ocupa espacio extra.
        if (castData->mover && std::string_view{source.get_device_name()} == std::string_view{dest.get_device_name()})
        {
            const bool esCarpeta = fslib::directory_exists(source);
            const bool movido    = esCarpeta ? fslib::rename_directory(source, dest) : fslib::rename_file(source, dest);
            if (movido)
            {
                if (castData->journalSize > 0) { error::fslib(fslib::commit_data_to_file_system(dest.get_device_name())); }
                continue;
            }
        }

        // Si no: copiar y, al mover, borrar el original solo si la copia salio bien.
        const bool copiado = copiar_uno(source, dest, castData->journalSize, task);
        if (!copiado) { ++fallas; continue; }
        if (castData->mover && !borrar_uno(source, castData->journalSize)) { ++fallas; }
    }

    if (fallas > 0)
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                            stringutil::get_formatted_string(strings::tr("%d item(s) couldn't be copied or moved.", "%d elemento(s) no se pudieron copiar o mover."), fallas));
    }
    spawningState->update_destination();
    spawningState->update_source();
    task->complete();
}

void tasks::fileoptions::borrar_varios(sys::threadpool::JobData taskData)
{
    auto castData                  = std::static_pointer_cast<FileOptionState::DataStruct>(taskData);
    sys::Task *task                = castData->task;
    FileOptionState *spawningState = castData->spawningState;
    if (error::is_null(task)) { return; }
    else if (error::is_null(spawningState)) { TASK_FINISH_RETURN(task); }

    const size_t total = castData->fuentes.size();
    int fallas         = 0;
    for (size_t i = 0; i < total; i++)
    {
        const fslib::Path &target = castData->fuentes[i];
        task->set_status(stringutil::get_formatted_string(strings::tr("Deleting %s (%d of %d)", "Borrando %s (%d de %d)"),
                                                          target.get_filename(), static_cast<int>(i + 1), static_cast<int>(total)));
        if (!borrar_uno(target, castData->journalSize)) { ++fallas; }
    }
    if (fallas > 0)
    {
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                            stringutil::get_formatted_string(strings::tr("%d item(s) couldn't be deleted.", "%d elemento(s) no se pudieron borrar."), fallas));
    }
    spawningState->update_source();
    task->complete();
}
