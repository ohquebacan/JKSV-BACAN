#include "data/TitleInfo.hpp"

#include "config/config.hpp"
#include "error.hpp"
#include "graphics/colors.hpp"
#include "graphics/gfxutil.hpp"
#include "logging/logger.hpp"
#include "stringutil.hpp"

#include <array>
#include <cstring>
#include <zlib.h>

namespace
{
    // Newer game updates (Breath of the Wild 1.9.0+, Super Mario Party Jamboree, ...) added a 17th+
    // language. To stay backwards-compatible the per-language title/publisher block is now packed into a
    // raw-DEFLATE blob at the very start of control.nacp, while everything past 0x3000 is left intact.
    // libnx doesn't decompress it yet (switchbrew/libnx#714), so nacpGetLanguageEntry() reads the
    // compressed bytes as a garbage title (this is what made games show up under a numeric/broken name and
    // froze the renderer). Detect the format — the exact check HOS itself uses — and restore the classic
    // 16-language block in place so the rest of the code path works unchanged.
    //
    // Compressed layout:
    //   [0x0 .. 0x2)    uint16 little-endian: size of the compressed blob
    //   [0x2 .. 0x2+n)  raw DEFLATE (wbits -15) -> 0x6000 bytes = 32 entries * 0x300
    //   byte 0x3215     == 1 when compressed (0 = classic uncompressed 16-language NACP)
    bool decompress_nacp_language_names(NacpStruct *nacp) noexcept
    {
        if (!nacp) { return false; }
        uint8_t *raw = reinterpret_cast<uint8_t *>(nacp);

        constexpr size_t FLAG_OFFSET = 0x3215;
        if (raw[FLAG_OFFSET] != 1) { return false; } // classic uncompressed NACP, nothing to do

        const uint16_t compressedSize = static_cast<uint16_t>(raw[0]) | (static_cast<uint16_t>(raw[1]) << 8);
        if (compressedSize == 0) { return false; }

        constexpr size_t DECOMPRESSED_SIZE = 0x6000;          // 32 * sizeof(NacpLanguageEntry)
        const size_t classicLangBlock      = sizeof(nacp->lang); // 16 * 0x300 = 0x3000

        std::array<uint8_t, DECOMPRESSED_SIZE> out{};

        z_stream strm{};
        if (inflateInit2(&strm, -15) != Z_OK) { return false; }
        strm.next_in   = raw + 2;
        strm.avail_in  = compressedSize;
        strm.next_out  = out.data();
        strm.avail_out = static_cast<uInt>(out.size());
        const int ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        if (ret != Z_STREAM_END) { return false; } // failed/partial inflate -> leave NACP untouched

        // Overwrite the compressed block with the first 16 (classic) language entries.
        std::memcpy(raw, out.data(), classicLangBlock);
        return true;
    }

    // Some games ship a NACP name with invalid UTF-8 bytes. JKSV's text renderer can hang forever on those,
    // so replace any malformed byte sequence in-place with '?' (result is always valid UTF-8).
    // Returns true if anything was replaced (i.e. the name was broken).
    bool sanitize_utf8(char *str) noexcept
    {
        if (!str) { return false; }

        bool changed     = false;
        unsigned char *s = reinterpret_cast<unsigned char *>(str);
        while (*s)
        {
            const unsigned char lead = *s;
            int length = 0;
            if (lead < 0x80) { length = 1; }
            else if ((lead & 0xE0) == 0xC0) { length = 2; }
            else if ((lead & 0xF0) == 0xE0) { length = 3; }
            else if ((lead & 0xF8) == 0xF0) { length = 4; }
            else
            {
                *s = '?';
                ++s;
                changed = true;
                continue;
            }

            bool valid = true;
            for (int i = 1; i < length; i++)
            {
                if ((s[i] & 0xC0) != 0x80) // includes hitting the NUL terminator early
                {
                    valid = false;
                    break;
                }
            }

            if (!valid)
            {
                *s = '?';
                ++s;
                changed = true;
                continue;
            }
            s += length;
        }
        return changed;
    }
} // namespace

//                      ---- Construction ----

data::TitleInfo::TitleInfo(uint64_t applicationID) noexcept
    : m_applicationID(applicationID)
{
    static constexpr size_t SIZE_CTRL_DATA = sizeof(NsApplicationControlData);

    uint64_t controlSize{};

    // This will filter from even trying to fetch control data for system titles.
    const bool isSystem   = applicationID & 0x8000000000000000;
    const bool getError   = !isSystem && error::libnx(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                                                m_applicationID,
                                                                                &m_data,
                                                                                SIZE_CTRL_DATA,
                                                                                &controlSize));
    // Restore real per-language names if this game uses the new compressed NACP (switchbrew/libnx#714).
    if (!isSystem && !getError) { decompress_nacp_language_names(&m_data.nacp); }
    const bool entryError = !getError && error::libnx(nacpGetLanguageEntry(&m_data.nacp, &m_entry));
    if (isSystem || getError)
    {
        const std::string appIDHex = stringutil::get_formatted_string("%04X", m_applicationID & 0xFFFF);
        m_entry                    = &m_data.nacp.lang[SetLanguage_ENUS]; // I'm hoping this is enough?

        std::snprintf(m_entry->name, TitleInfo::SIZE_PATH_SAFE, "%016lX", m_applicationID);
        m_nameBroken = getError; // a real game whose name couldn't be read (system titles aren't "broken")
        TitleInfo::get_create_path_safe_title();
    }
    else if (!getError && !entryError)
    {
        m_nameBroken = sanitize_utf8(m_entry->name); // guard the renderer + flag malformed NACP names
        m_hasData    = true;
        TitleInfo::get_create_path_safe_title();
    }
}

// To do: Make this safer...
data::TitleInfo::TitleInfo(uint64_t applicationID, NsApplicationControlData &controlData) noexcept
    : m_applicationID(applicationID)
    , m_data(controlData)
    , m_hasData(true)
{
    // Restore real per-language names if this game uses the new compressed NACP (switchbrew/libnx#714).
    decompress_nacp_language_names(&m_data.nacp);
    const bool entryError = error::libnx(nacpGetLanguageEntry(&m_data.nacp, &m_entry));
    if (entryError)
    {
        m_entry      = &m_data.nacp.lang[SetLanguage_ENUS];
        std::snprintf(m_entry->name, TitleInfo::SIZE_PATH_SAFE, "%016lX", m_applicationID);
        m_nameBroken = true;
    }
    else { m_nameBroken = sanitize_utf8(m_entry->name); } // guard the renderer + flag malformed NACP names

    TitleInfo::get_create_path_safe_title();
}

//                      ---- Public functions ----

uint64_t data::TitleInfo::get_application_id() const noexcept { return m_applicationID; }

const NsApplicationControlData *data::TitleInfo::get_control_data() const noexcept { return &m_data; }

bool data::TitleInfo::has_control_data() const noexcept { return m_hasData; }

bool data::TitleInfo::name_is_broken() const noexcept
{
    // Once the user has assigned a custom folder name, the title is no longer "broken" for our purposes.
    return m_nameBroken && !config::has_custom_path(m_applicationID);
}

const char *data::TitleInfo::get_title() const noexcept { return m_entry->name; }

const char *data::TitleInfo::get_path_safe_title() const noexcept { return m_pathSafeTitle; }

const char *data::TitleInfo::get_publisher() const noexcept { return m_entry->author; }

uint64_t data::TitleInfo::get_save_data_owner_id() const noexcept { return m_data.nacp.save_data_owner_id; }

int64_t data::TitleInfo::get_save_data_size(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:   return nacp.user_account_save_data_size;
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return nacp.device_save_data_size;
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:     return nacp.cache_storage_size;
    }

    return 0;
}

int64_t data::TitleInfo::get_save_data_size_max(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:   return std::max(nacp.user_account_save_data_size, nacp.user_account_save_data_size_max);
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return std::max(nacp.device_save_data_size, nacp.device_save_data_size_max);
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:     return std::max(nacp.cache_storage_size, nacp.cache_storage_data_and_journal_size_max);
    }

    return 0;
}

int64_t data::TitleInfo::get_journal_size(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:   return nacp.user_account_save_data_journal_size;
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return nacp.device_save_data_journal_size;
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:     return nacp.cache_storage_journal_size;
    }

    return 0;
}

int64_t data::TitleInfo::get_journal_size_max(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account:
            return std::max(nacp.user_account_save_data_journal_size, nacp.user_account_save_data_journal_size_max);
        case FsSaveDataType_Bcat:      return nacp.bcat_delivery_cache_storage_size;
        case FsSaveDataType_Device:    return std::max(nacp.device_save_data_journal_size, nacp.device_save_data_journal_size_max);
        case FsSaveDataType_Temporary: return nacp.temporary_storage_size;
        case FsSaveDataType_Cache:
            return std::max(nacp.cache_storage_journal_size, nacp.cache_storage_data_and_journal_size_max);
    }

    return 0;
}

bool data::TitleInfo::has_save_data_type(uint8_t saveType) const noexcept
{
    const NacpStruct &nacp = m_data.nacp;
    switch (saveType)
    {
        case FsSaveDataType_Account: return nacp.user_account_save_data_size > 0 || nacp.user_account_save_data_size_max > 0;
        case FsSaveDataType_Bcat:    return nacp.bcat_delivery_cache_storage_size > 0;
        case FsSaveDataType_Device:  return nacp.device_save_data_size > 0 || nacp.device_save_data_size_max > 0;
        case FsSaveDataType_Cache:   return nacp.cache_storage_size > 0 || nacp.cache_storage_data_and_journal_size_max > 0;
    }

    return false;
}

sdl::SharedTexture data::TitleInfo::get_icon() const noexcept { return m_icon; }

void data::TitleInfo::set_path_safe_title(const char *newPathSafe) noexcept
{
    const size_t length = std::char_traits<char>::length(newPathSafe);
    if (length >= TitleInfo::SIZE_PATH_SAFE) { return; }

    std::memset(m_pathSafeTitle, 0x00, TitleInfo::SIZE_PATH_SAFE);
    std::memcpy(m_pathSafeTitle, newPathSafe, length);
}

void data::TitleInfo::refresh_path_safe_title() noexcept { TitleInfo::get_create_path_safe_title(); }

void data::TitleInfo::load_icon()
{
    // This is taken from the NacpStruct.
    static constexpr size_t SIZE_ICON = 0x20000;

    if (m_hasData)
    {
        const std::string textureName = stringutil::get_formatted_string("%016llX", m_applicationID);
        m_icon                        = sdl::TextureManager::load(textureName, m_data.icon, SIZE_ICON);
    }
    else
    {
        const std::string text = stringutil::get_formatted_string("%04X", m_applicationID & 0xFFFF);
        m_icon                 = gfxutil::create_generic_icon(text, 48, colors::DIALOG_DARK, colors::WHITE);
    }
}

//                      ---- Private functions ----

void data::TitleInfo::get_create_path_safe_title() noexcept
{
    // Check if the title has a custom output path instead.
    const bool hasCustom = config::has_custom_path(m_applicationID);
    if (hasCustom)
    {
        // Read it into our local buffer.
        config::get_custom_path(m_applicationID, m_pathSafeTitle, TitleInfo::SIZE_PATH_SAFE);
        return;
    }

    // This is whether or not to use the title ID. It's used to bypass the English check.
    const bool useTitleId = config::get_by_key(config::keys::USE_TITLE_IDS);
    const bool useEnglish = config::get_by_key(config::keys::ENGLISH_SAFE_TITLES);

    // Grab the English title in case it's needed.
    const char *englishTitle{};
    const bool hasEnglish = TitleInfo::get_english_title(&englishTitle);

    // Final condition for override.
    const bool englishSafeTitle = useEnglish && hasEnglish;

    // This is our final string to use.
    const char *safeTarget = englishSafeTitle ? englishTitle : m_entry->name;

    const bool sanitized = !useTitleId && stringutil::sanitize_string_for_path(safeTarget, m_pathSafeTitle, SIZE_PATH_SAFE);
    if (useTitleId || !sanitized) { std::snprintf(m_pathSafeTitle, TitleInfo::SIZE_PATH_SAFE, "%016lX", m_applicationID); }
}

bool data::TitleInfo::get_english_title(const char **titleOut) const noexcept
{
    // These are the indexes for the english titles.
    static constexpr uint8_t ENUS_INDEX = 0;
    static constexpr uint8_t ENGB_INDEX = 1;

    // Grab the pointers to them.
    const char *enUs = m_data.nacp.lang[ENUS_INDEX].name;
    const char *enGb = m_data.nacp.lang[ENGB_INDEX].name;

    // If they're both empty, return false.
    if (enUs[0] == 0x00 && enGb[0] == 0x00) { return false; }

    // Get the length. Assign whichever is longer.
    const size_t enUsLength = std::char_traits<char>::length(enUs);
    const size_t enGbLength = std::char_traits<char>::length(enGb);

    // Assign whichever is longer. EnUS wins on match.
    *titleOut = enUsLength >= enGbLength ? enUs : enGb;

    return true;
}
