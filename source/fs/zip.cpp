#include "fs/zip.hpp"

#include <zlib.h>

#include "BufferQueue.hpp"
#include "config/config.hpp"
#include "error.hpp"
#include "fs/SaveMetaData.hpp"
#include "logging/logger.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"
#include "sys/sys.hpp"
#include "ui/PopMessageManager.hpp"

#include <cstring>
#include <ctime>

namespace
{
    /// @brief Buffer limit for queues.
    constexpr int SIZE_BUFFER_LIMIT = 128;

    /// @brief Buffer size used for writing files to ZIP.
    constexpr size_t SIZE_ZIP_BUFFER = 0x10000;

    /// @brief Buffer size used for decompressing files from ZIP.
    constexpr size_t SIZE_UNZIP_BUFFER = 0x80000;

    // Shared struct for Zip/File IO
    // clang-format off
    struct ZipReadStruct : sys::threadpool::DataStruct
    {
        fslib::File *source{};
        BufferQueue bufferQueue{SIZE_BUFFER_LIMIT};
    };

    struct UnzipReadStruct : sys::threadpool::DataStruct
    {
        fs::MiniUnzip *unzip{};
        BufferQueue bufferQueue{SIZE_BUFFER_LIMIT};
    };
    // clang-format on
} // namespace

// Function for reading files for Zipping.
static void zip_read_thread_function(sys::threadpool::JobData jobData)
{
    auto castData = std::static_pointer_cast<ZipReadStruct>(jobData);

    auto &bufferQueue      = castData->bufferQueue;
    fslib::File &source    = *castData->source;
    const int64_t fileSize = source.get_size();

    for (int64_t i = 0; i < fileSize;)
    {
        auto chunkBuffer = std::make_unique<sys::Byte[]>(SIZE_ZIP_BUFFER);
        ssize_t readSize = source.read(chunkBuffer.get(), SIZE_ZIP_BUFFER);

        while (!bufferQueue.try_push(chunkBuffer, readSize)) { BufferQueue::default_delay(); }
        i += readSize;
    }
}

// Function for reading data from Zip to buffer.
static void unzip_read_thread_function(sys::threadpool::JobData jobData)
{
    auto castData = std::static_pointer_cast<UnzipReadStruct>(jobData);

    auto &bufferQueue      = castData->bufferQueue;
    fs::MiniUnzip &unzip   = *castData->unzip;
    const int64_t fileSize = unzip.get_uncompressed_size();

    for (int64_t i = 0; i < fileSize;)
    {
        auto chunkBuffer = std::make_unique<sys::Byte[]>(SIZE_UNZIP_BUFFER);
        ssize_t readSize = unzip.read(chunkBuffer.get(), SIZE_UNZIP_BUFFER);

        while (!bufferQueue.try_push(chunkBuffer, readSize)) { BufferQueue::default_delay(); }
        i += readSize;
    }
}

void fs::copy_directory_to_zip(const fslib::Path &source, fs::MiniZip &dest, sys::ProgressTask *task)
{
    const char *ioStatus = strings::get_by_name(strings::names::IO_STATUSES, 1);

    fslib::Directory sourceDir{source};
    if (error::fslib(sourceDir.is_open())) { return; }

    for (const fslib::DirectoryEntry &entry : sourceDir)
    {
        const fslib::Path fullSource{source / entry};
        if (entry.is_directory())
        {
            dest.add_directory(fullSource.string());
            fs::copy_directory_to_zip(fullSource, dest, task);
        }
        else
        {
            fslib::File sourceFile{fullSource, FsOpenMode_Read};
            const std::string sourceString = fullSource.string();
            const bool newZipFile          = dest.open_new_file(sourceString);
            if (error::fslib(sourceFile.is_open()) || !newZipFile) { continue; }

            const int64_t fileSize = sourceFile.get_size();
            auto sharedData        = std::make_shared<ZipReadStruct>();
            sharedData->source     = &sourceFile;
            auto &bufferQueue      = sharedData->bufferQueue;

            if (task)
            {
                std::string status = stringutil::get_formatted_string(ioStatus, sourceString.c_str());
                task->set_status(status);
                task->reset(static_cast<double>(fileSize));
            }

            sys::threadpool::push_job(zip_read_thread_function, sharedData);
            for (int64_t i = 0; i < fileSize;)
            {
                BufferQueue::QueuePair queuePair{};
                while (!bufferQueue.get_front(queuePair)) { BufferQueue::default_delay(); }

                auto &[buffer, bufferSize] = queuePair;
                dest.write(buffer.get(), bufferSize);

                i += bufferSize;

                if (task) { task->update_current(static_cast<double>(i)); }
            }
            dest.close_current_file();
        }
    }
}

void fs::copy_zip_to_directory(fs::MiniUnzip &unzip, const fslib::Path &dest, int64_t journalSize, sys::ProgressTask *task)
{
    if (!unzip.reset()) { return; }
    const int popTicks          = ui::PopMessageManager::DEFAULT_TICKS;
    const char *popCommitFailed = strings::get_by_name(strings::names::IO_POPS, 0);
    const char *statusTemplate  = strings::get_by_name(strings::names::IO_STATUSES, 2);
    const bool needCommits      = journalSize > 0;

    do {
        if (unzip.get_filename() == fs::NAME_SAVE_META) { continue; }
        else if (unzip.is_directory())
        {
            // Just do this and continue.
            const fslib::Path dirPath{dest / unzip.get_filename()};
            error::fslib(fslib::create_directories_recursively(dirPath));
            continue;
        }

        fslib::Path fullDest{dest / unzip.get_filename()};
        const size_t lastDir = fullDest.find_last_of('/');
        if (lastDir == fullDest.NOT_FOUND) { continue; }

        if (lastDir > 0)
        {
            const fslib::Path dirPath{fullDest.sub_path(lastDir)};
            const bool isValid     = dirPath.is_valid();
            const bool exists      = isValid && fslib::directory_exists(dirPath);
            const bool createError = isValid && !exists && error::fslib(fslib::create_directories_recursively(dirPath));
            bool commitError       = !createError && error::fslib(fslib::commit_data_to_file_system(dirPath.get_device_name()));
            if (isValid && !exists && (createError || commitError)) { continue; }
        }

        const int64_t fileSize = unzip.get_uncompressed_size();
        fslib::File destFile{fullDest, FsOpenMode_Create | FsOpenMode_Write, fileSize};
        if (error::fslib(destFile.is_open())) { continue; }

        if (task)
        {
            std::string status = stringutil::get_formatted_string(statusTemplate, fullDest.get_filename());
            task->set_status(status);
            task->reset(static_cast<double>(fileSize));
        }

        auto sharedData   = std::make_shared<UnzipReadStruct>();
        sharedData->unzip = &unzip;
        auto &bufferQueue = sharedData->bufferQueue;

        int64_t journalCount{};
        sys::threadpool::push_job(unzip_read_thread_function, sharedData);
        for (int64_t i = 0; i < fileSize;)
        {
            BufferQueue::QueuePair queuePair{};
            while (!bufferQueue.get_front(queuePair)) { BufferQueue::default_delay(); }

            auto &[buffer, bufferSize] = queuePair;
            const bool commitNeeded    = needCommits && journalCount + static_cast<int64_t>(bufferSize) >= journalSize;
            if (commitNeeded)
            {
                destFile.close();
                const bool commitError = error::fslib(fslib::commit_data_to_file_system(dest.get_device_name()));
                if (commitError) { ui::PopMessageManager::push_message(popTicks, popCommitFailed); } // To do: How to recover?

                destFile.open(fullDest, FsOpenMode_Write);
                destFile.seek(i, destFile.BEGINNING);
                journalCount = 0;
            }

            destFile.write(buffer.get(), bufferSize);

            i += bufferSize;
            journalCount += bufferSize;

            if (task) { task->update_current(static_cast<double>(i)); }
        }
        destFile.close();

        const bool commitError = needCommits && error::fslib(fslib::commit_data_to_file_system(dest.get_device_name()));
        if (commitError) { ui::PopMessageManager::push_message(popTicks, popCommitFailed); }
    } while (unzip.next_file());
}

bool fs::zip_has_contents(const fslib::Path &zipPath)
{
    fs::MiniUnzip unzip{zipPath};
    if (!unzip.is_open()) { return false; }

    do {
        if (unzip.get_filename() != fs::NAME_SAVE_META) { return true; }
    } while (unzip.next_file());
    return false;
}

int fs::verify_zip_against_directory(fs::MiniUnzip &source, const fslib::Path &dest)
{
    if (!source.reset()) { return 0; }

    int distintos = 0;
    std::vector<unsigned char> bufer(0x40000);
    do {
        if (source.get_filename() == fs::NAME_SAVE_META || source.is_directory()) { continue; }

        const fslib::Path ruta{dest / source.get_filename()};
        fslib::File archivo{ruta, FsOpenMode_Read};
        if (!archivo.is_open())
        {
            ++distintos;
            continue;
        }

        uLong crc        = crc32(0L, Z_NULL, 0);
        uint64_t tamano  = 0;
        ssize_t leidos{};
        while ((leidos = archivo.read(bufer.data(), bufer.size())) > 0)
        {
            crc = crc32(crc, bufer.data(), static_cast<uInt>(leidos));
            tamano += static_cast<uint64_t>(leidos);
        }
        if (leidos < 0 || tamano != source.get_uncompressed_size() || static_cast<uint32_t>(crc) != source.get_crc()) { ++distintos; }
    } while (source.next_file());

    return distintos;
}
