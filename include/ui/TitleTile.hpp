#pragma once
#include "sdl.hpp"
#include "ui/Transition.hpp"

#include <string>
#include <vector>

namespace ui
{
    /// @brief Tile for Titleview.
    class TitleTile final
    {
        public:
            /// @brief Constructor.
            /// @param isFavorite Whether the title is a favorite and should have the little heart rendered.
            /// @param icon Shared texture pointer to the icon.
            /// @param hasCloudBackup Whether the title has a backup on the remote, for the cloud badge.
            /// @param hasLocalBackup Whether the title has a backup on the SD card, for the local badge.
            TitleTile(bool isFavorite,
                      int index,
                      sdl::SharedTexture icon,
                      bool hasCloudBackup = false,
                      bool hasLocalBackup = false,
                      bool hasPartida     = false,
                      std::string name    = {});

            /// @brief Medida del juego en la cuadricula del rediseno.
            static constexpr int SIZE = 150;

            /// @brief Runs the update routine.
            /// @param isSelected Whether or not the tile is selected and needs to expand.
            void update(int selected);

            /// @brief Runs the render routine.
            /// @param target Target to render to.
            /// @param x X coordinate to render to.
            /// @param y Y coordinate to render to.
            void render(sdl::SharedTexture &target, int x, int y);

            /// @brief Resets the width and height of the tile.
            void reset() noexcept;

            /// @brief Returns the render width in pixels.
            /// @return Render width.
            int get_width() const noexcept;

            /// @brief Returns the render height in pixels.
            /// @return Render height.
            int get_height() const noexcept;

            /// @brief Indice del juego en la lista del usuario (con filtro, no coincide con la casilla).
            int get_index() const noexcept { return m_index; }

        private:
            /// @brief Transition for the tile select/deselect.
            ui::Transition m_transition{};

            /// @brief Whether or not the title is a favorite.
            bool m_isFavorite{};

            /// @brief Whether the title has a backup on the remote storage.
            bool m_hasCloudBackup{};

            /// @brief Whether the title has a backup on the SD card (local working directory).
            bool m_hasLocalBackup{};

            bool m_hasPartida{}; // hay partida completa en el catalogo de OQB

            bool m_isSelected{};

            std::string m_name{}; // nombre que se muestra debajo, recortado a dos lineas al dibujarlo

            bool m_nameReady{};

            std::vector<std::string> m_lineas{};

            int m_index{};

            /// @brief Title's icon texture.
            sdl::SharedTexture m_icon{};

            /// @brief Shared cloud-badge texture, loaded once and reused by every tile.
            static inline sdl::SharedTexture sm_cloudIcon{};
            static inline sdl::SharedTexture sm_badgeBase{};
            static inline sdl::SharedTexture sm_badgeCheck{};
            static inline sdl::SharedTexture sm_partida{};
            static inline sdl::SharedTexture sm_mask{};
    };
} // namespace ui
