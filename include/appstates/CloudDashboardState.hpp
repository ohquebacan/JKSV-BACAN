#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "sdl.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/Menu.hpp"

#include <memory>

/// @brief Read-only sync overview: for each favorited game, shows the date of the local vs cloud latest backup
/// and whether you need to upload, download, or are up to date.
class CloudDashboardState final : public BaseState
{
    public:
        /// @brief Constructor. Reloads the remote listing and builds the rows.
        CloudDashboardState();

        /// @brief Creates, pushes, then returns a new dashboard.
        static inline std::shared_ptr<CloudDashboardState> create_and_push()
        {
            auto newState = std::make_shared<CloudDashboardState>();
            StateManager::push_state(newState);
            return newState;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        /// @brief Scrollable list of per-favorite rows.
        std::shared_ptr<ui::Menu> m_menu{};

        /// @brief Render target.
        sdl::SharedTexture m_renderTarget{};

        /// @brief Control guide.
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};

        /// @brief Builds the rows from the favorites + local/cloud backup dates.
        void build_rows();
};
