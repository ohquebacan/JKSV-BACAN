#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "data/TitleInfo.hpp"
#include "sdl.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/Menu.hpp"

#include <memory>
#include <string>
#include <vector>

class BackupMenuState; // forward decl; full type used in the .cpp to refresh after associating.

/// @brief Lists the cloud's per-game folders and lets the user associate one with a game whose name is broken
/// (so it maps to the right folder across consoles). Picking sets the game's custom folder name.
class CloudFolderPickerState final : public BaseState
{
    public:
        CloudFolderPickerState(data::TitleInfo *titleInfo, BackupMenuState *spawning);

        static inline std::shared_ptr<CloudFolderPickerState> create_and_push(data::TitleInfo *titleInfo,
                                                                              BackupMenuState *spawning)
        {
            auto newState = std::make_shared<CloudFolderPickerState>(titleInfo, spawning);
            StateManager::push_state(newState);
            return newState;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        data::TitleInfo *m_titleInfo{};
        BackupMenuState *m_spawning{};

        std::shared_ptr<ui::Menu> m_menu{};
        sdl::SharedTexture m_renderTarget{};
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};

        /// @brief Cloud folder names, parallel to the menu options.
        std::vector<std::string> m_folderNames{};

        void build_list();
        void associate(int index);
};
