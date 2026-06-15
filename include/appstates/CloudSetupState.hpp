#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "sdl.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/Menu.hpp"

#include <memory>

/// @brief In-app cloud setup menu. Opens a WebDAV form (with presets) and a Google Drive guide so the user
/// never has to edit JSON on the SD card by hand.
class CloudSetupState final : public BaseState
{
    public:
        /// @brief Constructor.
        CloudSetupState();

        /// @brief Returns a new CloudSetupState.
        static inline std::shared_ptr<CloudSetupState> create() { return std::make_shared<CloudSetupState>(); }

        /// @brief Creates, pushes, then returns a new CloudSetupState.
        static inline std::shared_ptr<CloudSetupState> create_and_push()
        {
            auto newState = CloudSetupState::create();
            StateManager::push_state(newState);
            return newState;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        /// @brief Menu for the provider options.
        std::shared_ptr<ui::Menu> m_menu{};

        /// @brief Render target for the menu.
        sdl::SharedTexture m_renderTarget{};

        /// @brief Control guide for the bottom right corner.
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};

        /// @brief Fills the menu with the provider options.
        void initialize_menu();

        // WebDAV presets: open the form pre-filled for each provider.
        void configure_koofr();
        void configure_nextcloud();
        void configure_generic();

        /// @brief Shows the Google Drive setup guide.
        void show_google_drive_guide();

        /// @brief Opens the form loaded with the current webdav.json (or a pop if there is none).
        void show_current_config();

        /// @brief Cycles the backup-retention limit (Off -> 5 -> 10 -> 20) and saves it.
        void cycle_retention();

        /// @brief Updates the retention menu row to show the current limit.
        void update_retention_label();
};
