#pragma once
#include "curl/DownloadStruct.hpp"
#include "curl/UploadStruct.hpp"
#include "fslib.hpp"

#include <curl/curl.h>
#include <memory>
#include <string>
#include <vector>

/// @brief This namespace contains various wrapped libcurl functions and data.
namespace curl
{
    /// @brief JKSV's user agent string.
    static constexpr const char *STRING_USER_AGENT = "JKSV";

    /// @brief Self cleaning curl handle.
    using Handle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

    /// @brief Self cleaning curl header/slist.
    using HeaderList = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

    /// @brief Definition for a vector containing headers received from libcurl.
    using HeaderArray = std::vector<std::string>;

    /// @brief Initializes lib curl.
    /// @return True on success. False on failure.
    bool initialize();

    /// @brief Exits libcurl
    void exit();

    /// @brief Inline templated function to wrap curl_easy_setopt and make using curl::Handle slightly easier.
    /// @tparam Option Templated type of the option. This is a headache so let the compiler figure it out.
    /// @tparam Value Templated type of the value to set the option too. See above.
    /// @param handle curl::Handle the option is being set for.
    /// @param option CURLOPT to set.
    /// @param value Value to set the CURLOPT to.
    /// @return CURLcode returned from curl_easy_setopt.
    template <typename Option, typename Value>
    static inline CURLcode set_option(curl::Handle &handle, Option option, Value value)
    {
        return curl_easy_setopt(handle.get(), option, value);
    }

    /// @brief Inline function that returns a self cleaning curl handle.
    /// @return Curl handle.
    static inline curl::Handle new_handle() { return curl::Handle(curl_easy_init(), curl_easy_cleanup); }

    /// @brief Inline function that returns a nullptr'd self cleaning curl_list.
    /// @return Self cleaning curl_slist.
    static inline curl::HeaderList new_header_list() { return curl::HeaderList(nullptr, curl_slist_free_all); }

    /// @brief Inline wrapper function for curl_easy_reset.
    /// @param curl curl::Handle to reset.
    static inline void reset_handle(curl::Handle &curl)
    {
        curl_easy_reset(curl.get());
        curl::set_option(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl::set_option(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl::set_option(curl, CURLOPT_USERAGENT, curl::STRING_USER_AGENT);
        curl::set_option(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        // Stall guard: abort if a transfer moves < 30 B/s for 30s. Prevents a dead/stalled connection from
        // hanging the whole app forever (the calls run synchronously on the main thread in places).
        curl::set_option(curl, CURLOPT_LOW_SPEED_LIMIT, 30L);
        curl::set_option(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    }

    /// @brief Logged inline wrapper function for curl_easy_perform.
    /// @param handle Handle to perform.
    bool perform(curl::Handle &handle);

    /// @brief Inline wrapper function to make adding to HeaderList simpler.
    /// @param headerList Header list to append to.
    /// @param header Header to append.
    void append_header(curl::HeaderList &list, std::string_view header);

    /// @brief Curl callback function for reading data from a file.
    /// @param buffer Incoming buffer from curl to read to.
    /// @param size Element size.
    /// @param count Element count.
    /// @param target Target file to read from.
    /// @return Number of bytes read so curl thinks everything went OK.
    size_t read_data_from_file(char *buffer, size_t size, size_t count, curl::UploadStruct *upload);

    /// @brief Curl callback function that writes incoming headers to a vector/array.
    /// @param buffer Incoming buffer from curl.
    /// @param size Element size.
    /// @param count Element count.
    /// @param array Array to write the header to.
    /// @return size * count so curl thinks everything is fine, because it usually is.
    size_t write_header_array(const char *buffer, size_t size, size_t count, curl::HeaderArray *array);

    /// @brief Curl callback function that writes the response data to a C++ string.
    /// @param buffer Incoming buffer from curl.
    /// @param size Element size.
    /// @param count Element count.
    /// @param string String to write the response to.
    /// @return size * count so curl thinks everything is fine.
    size_t write_response_string(const char *buffer, size_t size, size_t count, std::string *string);

    /// @brief Curl callback function that writes data directly to an fslib::File pointer.
    /// @param buffer Incoming buffer from CURL.
    /// @param size Element size.
    /// @param count Element count.
    /// @param target File to write the data to.
    /// @return Number of bytes written to the file.
    size_t write_data_to_file(const char *buffer, size_t size, size_t count, fslib::File *target);

    /// @brief Threaded version of the above.
    /// @param buffer Incoming data from curl
    /// @param size size
    /// @param count count
    /// @param download Struct containing data for threaded download.
    /// @return size * count
    size_t download_file_threaded(const char *buffer, size_t size, size_t count, curl::DownloadStruct *download);

    /// @brief Function used to download files threaded.
    /// @param download Struct shared by both threads.
    void download_write_thread_function(sys::threadpool::JobData jobData);

    /// @brief Gets the value of a header from an array of headers.
    /// @param array Array of headers to search.
    /// @param header Header to search for.
    /// @param valueOut String to write the value to.
    bool get_header_value(const curl::HeaderArray &array, std::string_view header, std::string &valueOut);

    /// @brief Gets the response code from the handle passed.
    /// @param handle Handle to get the response code from.
    long get_response_code(curl::Handle &handle);

    /// @brief Calls curl_easy_escape using the passed curl::Handle.
    /// @param handle Handle to use.
    /// @param in String to escape.
    /// @param out String to write the escaped text to.
    bool escape_string(curl::Handle &handle, std::string_view in, std::string &out);

    /// @brief Calls curl_easy_unescape using the passed curl::Handle.
    /// @param handle Handle to use.
    /// @param in String to unescape.
    /// @param out String to write the escaped text to.
    bool unescape_string(curl::Handle &handle, std::string_view in, std::string &out);

    /// @brief Prepares the curl handle passed for a get request.
    /// @param curl Handle to prepare for a get request.
    void prepare_get(curl::Handle &curl);

    /// @brief Prepares the curl handle for a post request.
    /// @param curl Handle prepare for a post request.
    void prepare_post(curl::Handle &curl);

    /// @brief Prepares the curl handle for an upload.
    /// @param curl Handle to reset and prepare for an upload.
    void prepare_upload(curl::Handle &curl);
} // namespace curl
