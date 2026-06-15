#pragma once
#include "json.hpp"
#include "remote/Storage.hpp"

#include <ctime>

namespace remote
{
    class GoogleDrive final : public remote::Storage
    {
        public:
            /// @brief Loads the config from SD.
            GoogleDrive();

            /// @brief Re-fetches the Drive listing into the in-memory list.
            bool reload() override;

            /// @brief Creates a directory on Google Drive.
            /// @param name Name of the directory to create.
            bool create_directory(std::string_view name) override;

            /// @brief Uploads the file from source. File name is used to name the file.
            /// @param source Path to upload the file from.
            bool upload_file(const fslib::Path &source, std::string_view name, sys::ProgressTask *task = nullptr) override;

            /// @brief Patches or updates the file on Google Drive.
            /// @param file Pointer to the item containing the data needed to update the file.
            /// @param source Source path to update from.
            bool patch_file(remote::Item *file, const fslib::Path &source, sys::ProgressTask *task = nullptr) override;

            /// @brief Downloads a file from Google Drive.
            /// @param file Pointer to the item containing data to download the file.
            /// @param destination Location to write the downloaded file to.
            bool download_file(const remote::Item *file,
                               const fslib::Path &destination,
                               sys::ProgressTask *task = nullptr) override;

            /// @brief Deletes an item from Google Drive.
            /// @param item Pointer to item containing data to delete the item.
            bool delete_item(const remote::Item *item) override;

            /// @brief Renames an item on Google Drive.
            /// @param item Item to rename.
            /// @param newName New name of the item.
            bool rename_item(remote::Item *item, std::string_view newName) override;

            /// @brief Returns whether or not a sign in is required to use drive. AKA the refresh token is missing.
            bool sign_in_required() const;

            /// @brief Requests the the necessary data from Google to login.
            /// @param message String to store the message to.
            /// @param code String to store the device code from Google.
            /// @param expiration time_t to store when the sign in window closes.
            /// @param wait Int to store the time in seconds between server pings.
            bool get_sign_in_data(std::string &message, std::string &code, std::time_t &expiration, int &wait);

            /// @brief This is the function that pings the server to see if the user entered the code yet.
            /// @param code The code Google reponded with for verification.
            /// @return If the user signs in, true. If not, false;
            bool poll_sign_in(std::string_view code);

        private:
            /// @brief Google client ID.
            std::string m_clientId{};

            /// @brief Google client secret.
            std::string m_clientSecret{};

            /// @brief Authentication token.
            std::string m_token{};

            /// @brief Token used for refreshing token when it expires.
            std::string m_refreshToken{};

            /// @brief This is to save the authentication header string instead of recreating it over and over.
            std::string m_authHeader{};

            /// @brief This is the calculate time when the auth token expires.
            std::time_t m_tokenExpires{};

            /// @brief Uses V2 of Drive's API to get the root directory ID from Google.
            bool get_root_id();

            /// @brief Returns whether or not the auth token is still valid for use or needs to be refreshed.
            bool token_is_valid() const noexcept;

            /// @brief Attempts to refresh the auth token if needed.
            bool refresh_token();

            /// @brief Requests and processes the entire listing for JKSV.
            bool request_listing();

            /// @brief Processes and listing
            /// @param json Json object to use for parsing.
            bool process_listing(json::Object &json);

            /// @brief Performs a quick check on the json object passed for errors.
            /// @param json Json object to check.
            /// @param log Whether or not to log the error.
            /// @note This doesn't catch every error. Google's errors aren't consistent.
            bool error_occurred(json::Object &json, bool log = true) noexcept;
    };
} // namespace remote
