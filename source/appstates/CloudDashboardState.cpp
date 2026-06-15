#include "appstates/CloudDashboardState.hpp"

#include "config/config.hpp"
#include "data/data.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view DASHBOARD_TARGET = "CloudDashboardTarget";

    // Extract the YYYY-MM-DD_HH-MM-SS stamp (last 19 chars before optional .zip) from a backup name.
    std::string date_key(std::string_view name)
    {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".zip") { name = name.substr(0, name.size() - 4); }
        constexpr size_t stamp = 19;
        return name.size() >= stamp ? std::string(name.substr(name.size() - stamp)) : std::string{};
    }

    // "2026-06-15_10-00-00" -> "06-15 10:00"; empty key -> "--".
    std::string pretty_date(const std::string &key)
    {
        if (key.size() < 16) { return "--"; }
        std::string out = key.substr(5, 5) + " " + key.substr(11, 2) + ":" + key.substr(14, 2);
        return out;
    }

    // Newest local backup date for a game's working-directory folder.
    std::string local_latest(const data::TitleInfo *titleInfo)
    {
        const fslib::Path dir{config::get_working_directory() / titleInfo->get_path_safe_title()};
        fslib::Directory listing{dir, false};
        std::string latest;
        if (listing.is_open())
        {
            const int64_t count = listing.get_count();
            for (int64_t i = 0; i < count; i++)
            {
                const std::string key = date_key(listing[i].get_filename());
                if (key > latest) { latest = key; }
            }
        }
        return latest;
    }

    // Newest cloud backup date for a game's remote folder.
    std::string cloud_latest(remote::Storage *remote, const data::TitleInfo *titleInfo)
    {
        if (!remote) { return {}; }

        remote->return_to_root();
        const std::string_view remoteTitle =
            remote->supports_utf8() ? titleInfo->get_title() : titleInfo->get_path_safe_title();
        if (!remote->directory_exists(remoteTitle)) { return {}; }

        remote::Item *folder = remote->get_directory_by_name(remoteTitle);
        if (!folder) { return {}; }

        remote->change_directory(folder);
        remote::Storage::DirectoryListing files;
        remote->get_directory_listing(files);

        std::string latest;
        for (remote::Item *file : files)
        {
            if (file->is_directory()) { continue; }
            const std::string key = date_key(file->get_name());
            if (key > latest) { latest = key; }
        }

        remote->return_to_root();
        return latest;
    }
} // namespace

//                      ---- Construction ----

CloudDashboardState::CloudDashboardState()
    : m_renderTarget(sdl::TextureManager::load(DASHBOARD_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(strings::get_by_name(strings::names::CONTROL_GUIDES, 3)))
{
    m_menu = ui::Menu::create(16, 10, 1040, 23, 555);
    CloudDashboardState::build_rows();
}

//                      ---- Public functions ----

void CloudDashboardState::update()
{
    const bool hasFocus = BaseState::has_focus();

    m_menu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    if (input::button_pressed(HidNpadButton_B)) { BaseState::deactivate(); }
}

void CloudDashboardState::sub_update() { m_controlGuide->sub_update(); }

void CloudDashboardState::render()
{
    const bool hasFocus = BaseState::has_focus();

    m_renderTarget->clear(colors::TRANSPARENT);
    m_menu->render(m_renderTarget, hasFocus);

    // Opaque background so the menu underneath doesn't bleed through.
    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, graphics::SCREEN_WIDTH, graphics::SCREEN_HEIGHT, colors::CLEAR_COLOR);

    sdl::text::render(sdl::Texture::Null, 24, 48, 22, sdl::text::NO_WRAP, colors::WHITE, "Panel de sincronizacion (favoritos)");
    m_renderTarget->render(sdl::Texture::Null, 201, 91);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void CloudDashboardState::build_rows()
{
    // NOTE: do NOT reload() here. This runs on the main/render thread, and a synchronous network call freezes
    // the whole app. Use the already-loaded in-memory listing (refreshed at startup and when opening games).
    remote::Storage *remote = remote::get_remote_storage();

    std::set<uint64_t> seen;
    int favorites = 0;

    data::UserList userList;
    data::get_users(userList);
    for (data::User *user : userList)
    {
        if (user->get_account_save_type() == FsSaveDataType_System) { continue; }

        const int total = static_cast<int>(user->get_total_data_entries());
        for (int i = 0; i < total; i++)
        {
            const uint64_t applicationID = user->get_application_id_at(i);
            if (!config::is_favorite(applicationID)) { continue; }
            if (!seen.insert(applicationID).second) { continue; }

            data::TitleInfo *titleInfo = data::get_title_info_by_id(applicationID);
            if (!titleInfo) { continue; }

            const std::string localKey = local_latest(titleInfo);
            const std::string cloudKey = cloud_latest(remote, titleInfo);

            const char *tag = "[sin copias]";
            if (!localKey.empty() && !cloudKey.empty())
            {
                tag = localKey > cloudKey ? "[SUBIR]" : (cloudKey > localKey ? "[BAJAR]" : "[OK]");
            }
            else if (!localKey.empty()) { tag = "[SUBIR]"; }
            else if (!cloudKey.empty()) { tag = "[BAJAR]"; }

            std::string name{titleInfo->get_title()};
            if (name.size() > 26) { name = name.substr(0, 25) + "~"; }

            std::string row = name;
            row.append(32 - std::min<size_t>(name.size(), 30), ' ');
            row += "local:" + pretty_date(localKey) + "  nube:" + pretty_date(cloudKey) + "  " + tag;

            m_menu->add_option(row);
            ++favorites;
        }
    }

    if (favorites == 0)
    {
        m_menu->add_option("No tienes juegos favoritos. Marca juegos con el corazon (Y) para verlos aqui.");
    }
}
