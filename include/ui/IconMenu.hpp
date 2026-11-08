#pragma once
#include <string>
#include "ui/Menu.hpp"

#include <memory>

namespace ui
{
    /// @brief This is a hacky derived class to use Menu's code with Icons instead.
    class IconMenu final : public ui::Menu
    {
        public:
            /// @brief Default constructor.
            IconMenu() = default;

            /// @brief This constructor calls initialize.
            /// @param x X coordinate to render the menu to.
            /// @param y Y coordinate to render the menu to.
            /// @param renderTargetHeight Height of the render target to calculate how many options can be displayed at once.
            IconMenu(int x, int y, int renderTargetHeight);

            /// @brief Creates and returns a new IconMenu instance.
            static inline std::shared_ptr<ui::IconMenu> create(int x, int y, int renderTargetHeight)
            {
                return std::make_shared<ui::IconMenu>(x, y, renderTargetHeight);
            }

            /// @brief Initializes the menu.
            /// @param x X coordinate to render the menu to.
            /// @param y Y coordinate to render the menu to.
            /// @param renderTargetHeight Height of the render target to calculate how many options can be displayed at
            /// once.
            void initialize(int x, int y, int renderTargetHeight);

            /// @brief Runs the update routine.
            /// @param hasFocus Whether or not the containing state has focus.
            void update(bool hasFocus) override;

            /// @brief Runs the render routine.
            /// @param target Target to render to.
            /// @param hasFocus Whether or not the containing state has focus.
            void render(sdl::SharedTexture &target, bool hasFocus) override;

            /// @brief Adds a new icon to the menu.
            /// @param newOption Icon to add.
            void add_option(sdl::SharedTexture newOption);

            /// @brief Con el nombre que va debajo del avatar (rediseno).
            void add_option(sdl::SharedTexture newOption, std::string label, bool glyph = false);

        private:
            /// @brief Vector of shared texture pointers to textures used.
            std::vector<sdl::SharedTexture> m_options;

            std::vector<std::string> m_labels;

            std::vector<bool> m_glyphs; // icono de linea sobre un circulo (Ajustes, Extras)

            double m_scrollUsuarios{}; // los usuarios se desplazan solos para que el elegido no quede tras los botones
    };
} // namespace ui
