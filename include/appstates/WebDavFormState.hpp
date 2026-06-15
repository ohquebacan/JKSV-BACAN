#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "sdl.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/Menu.hpp"

#include <memory>
#include <string>
#include <string_view>

/// @brief A form that shows all WebDAV fields at once (origin, basepath, user, password), lets the user edit
/// any of them, reveal the password, and save. Used both for the provider presets and for viewing/editing
/// the current configuration.
class WebDavFormState final : public BaseState
{
    public:
        /// @brief Constructor. Pre-fills the fields with the given values.
        WebDavFormState(std::string_view origin,
                        std::string_view basepath,
                        std::string_view username,
                        std::string_view password);

        /// @brief Creates, pushes, then returns a new form.
        static inline std::shared_ptr<WebDavFormState> create_and_push(std::string_view origin,
                                                                       std::string_view basepath,
                                                                       std::string_view username,
                                                                       std::string_view password)
        {
            auto newState = std::make_shared<WebDavFormState>(origin, basepath, username, password);
            StateManager::push_state(newState);
            return newState;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        /// @brief Field/action menu.
        std::shared_ptr<ui::Menu> m_menu{};

        /// @brief Render target.
        sdl::SharedTexture m_renderTarget{};

        /// @brief Control guide.
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};

        // Current field values.
        std::string m_origin{};
        std::string m_basepath{};
        std::string m_username{};
        std::string m_password{};

        /// @brief Whether the password is shown in clear text.
        bool m_showPassword{};

        /// @brief Re-writes every menu row from the current field values.
        void update_options();

        /// @brief Opens the keyboard to edit the given field (0=origin,1=basepath,2=user,3=password).
        void edit_field(int field);

        /// @brief Writes webdav.json from the current fields and reconnects.
        void save_and_connect();
};
