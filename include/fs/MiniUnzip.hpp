#pragma once
#include "fslib.hpp"

#include <minizip/unzip.h>
#include <string_view>

namespace fs
{
    class MiniUnzip final
    {
        public:
            MiniUnzip() = default;

            /// @brief Constructor that c
            MiniUnzip(const fslib::Path &path);

            /// @brief Closes the unzFile.
            ~MiniUnzip();

            /// @brief Returns whether or not the unzFile was successfully opened.
            bool is_open() const noexcept;

            /// @brief Attempts to open the path passed as a ZIP file.
            bool open(const fslib::Path &path);

            /// @brief Closes the unzfile.
            void close();

            /// @brief Attempts to go to the next file. Returns false at the end.
            bool next_file();

            /// @brief Closes the currently open file.
            bool close_current_file();

            /// @brief Attempts to locate a file with filename in the ZIP.
            bool locate_file(std::string_view filename);

            /// @brief Resets to the beginning file.
            bool reset();

            /// @brief Reads from the currently open file to the buffer passed.
            ssize_t read(void *buffer, size_t bufferSize);

            /// @brief Returns if the entry is just a directory.
            bool is_directory() const noexcept;

            /// @brief Returns the name of the current file.
            const char *get_filename() const noexcept;

            /// @brief Returns the compressed size of the currently open file.
            uint64_t get_compressed_size() const noexcept;

            /// @brief Returns the uncompressed size of the the currently open file.
            uint64_t get_uncompressed_size() const noexcept;

            /// @brief CRC32 que el zip guarda para la entrada actual.
            uint32_t get_crc() const noexcept;

        private:
            /// @brief Underlying unzFile.
            unzFile m_unz{};

            /// @brief Saves whether or not opening the file was successful.
            bool m_isOpen{};

            /// @brief Info for the current file.
            unz_file_info64 m_fileInfo{};

            /// @brief Buffer containing the current file's name.
            char m_filename[FS_MAX_PATH]{};
    };
}
