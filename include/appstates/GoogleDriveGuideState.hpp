#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"

#include <memory>

/// @brief Full-screen Google Drive setup guide. Renders the steps (white) and the credential-file status
/// (green if detected, red if missing), with full control over layout/colors unlike the fixed message box.
class GoogleDriveGuideState final : public BaseState
{
    public:
        /// @brief Constructor. Detects whether client_secret.json is present.
        GoogleDriveGuideState();

        /// @brief Creates, pushes, then returns a new guide state.
        static inline std::shared_ptr<GoogleDriveGuideState> create_and_push()
        {
            auto newState = std::make_shared<GoogleDriveGuideState>();
            StateManager::push_state(newState);
            return newState;
        }

        void update() override;
        void render() override;

    private:
        /// @brief Whether client_secret.json was found on the SD card.
        bool m_hasConfig{};
};
