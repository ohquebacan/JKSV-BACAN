#include "appstates/CloudFolderPickerState.hpp"

#include "appstates/BackupMenuState.hpp"
#include "config/config.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"
#include "ui/PopMessageManager.hpp"

#include <string>

namespace
{
    constexpr std::string_view PICKER_TARGET = "CloudFolderPickerTarget";
}

//                      ---- Construction ----

CloudFolderPickerState::CloudFolderPickerState(data::TitleInfo *titleInfo, BackupMenuState *spawning)
    : m_titleInfo(titleInfo)
    , m_spawning(spawning)
    , m_renderTarget(sdl::TextureManager::load(PICKER_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(strings::get_by_name(strings::names::CONTROL_GUIDES, 3)))
{
    m_menu = ui::Menu::create(16, 10, 1040, 23, 555);
    CloudFolderPickerState::build_list();
}

//                      ---- Public functions ----

void CloudFolderPickerState::update()
{
    const bool hasFocus = BaseState::has_focus();

    m_menu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    if (input::button_pressed(HidNpadButton_A) && !m_folderNames.empty())
    {
        CloudFolderPickerState::associate(m_menu->get_selected());
    }
    else if (input::button_pressed(HidNpadButton_B)) { BaseState::deactivate(); }
}

void CloudFolderPickerState::sub_update() { m_controlGuide->sub_update(); }

void CloudFolderPickerState::render()
{
    const bool hasFocus = BaseState::has_focus();

    m_renderTarget->clear(colors::TRANSPARENT);
    m_menu->render(m_renderTarget, hasFocus);

    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, graphics::SCREEN_WIDTH, graphics::SCREEN_HEIGHT, colors::CLEAR_COLOR);
    sdl::text::render(sdl::Texture::Null, 24, 48, 22, sdl::text::NO_WRAP, colors::WHITE, "Elige la carpeta de la nube de este juego:");
    m_renderTarget->render(sdl::Texture::Null, 201, 91);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void CloudFolderPickerState::build_list()
{
    remote::Storage *remote = remote::get_remote_storage();
    if (!remote)
    {
        m_menu->add_option("No hay nube configurada.");
        return;
    }

    // In-memory listing of the cloud's root-level folders (no network on the main thread).
    remote->return_to_root();
    remote::Storage::DirectoryListing rootItems;
    remote->get_directory_listing(rootItems);

    for (remote::Item *item : rootItems)
    {
        if (!item->is_directory()) { continue; }
        m_folderNames.emplace_back(item->get_name());
        m_menu->add_option(std::string(item->get_name()));
    }

    if (m_folderNames.empty()) { m_menu->add_option("No hay carpetas de juegos en la nube todavia."); }
}

void CloudFolderPickerState::associate(int index)
{
    if (index < 0 || index >= static_cast<int>(m_folderNames.size())) { return; }

    const std::string &folderName = m_folderNames[index];

    // Point this game at the chosen cloud folder (also used for the local folder).
    m_titleInfo->set_path_safe_title(folderName.c_str());
    config::add_custom_path(m_titleInfo->get_application_id(), folderName);
    config::save();

    ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                        stringutil::get_formatted_string("Asociado a: %s", folderName.c_str()));

    if (m_spawning) { m_spawning->reinitialize_remote(); }
    BaseState::deactivate();
}
