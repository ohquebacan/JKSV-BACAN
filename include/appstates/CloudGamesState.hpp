#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/Menu.hpp"

#include <memory>
#include <string>
#include <vector>

/// @brief Juegos en la nube: todas las carpetas de la nube con cuantos respaldos tienen y cuanto pesan.
class CloudGamesState final : public BaseState
{
    public:
        CloudGamesState();

        static inline std::shared_ptr<CloudGamesState> create_and_push()
        {
            auto estado = std::make_shared<CloudGamesState>();
            StateManager::push_state(estado);
            return estado;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        struct Juego
        {
            std::string carpeta{}, ultima{};
            int respaldos{};
            uint64_t bytes{};
        };
        std::vector<Juego> m_juegos{};
        uint64_t m_total{};
        std::shared_ptr<ui::Menu> m_menu{};
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};
        double m_desplazamiento{};
        bool m_teniaFoco{};

        void cargar();
};

/// @brief Los respaldos de un juego en la nube: bajar uno a la consola, borrarlo o dejar solo los 3 mas nuevos.
class CloudGameBackupsState final : public BaseState
{
    public:
        CloudGameBackupsState(std::string carpeta);

        static inline std::shared_ptr<CloudGameBackupsState> create_and_push(std::string carpeta)
        {
            auto estado = std::make_shared<CloudGameBackupsState>(std::move(carpeta));
            StateManager::push_state(estado);
            return estado;
        }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        struct Respaldo
        {
            std::string nombre{}, clave{};
            uint64_t bytes{};
        };
        std::string m_carpeta{};
        std::vector<Respaldo> m_respaldos{};
        std::shared_ptr<ui::Menu> m_menu{};
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};
        double m_desplazamiento{};
        bool m_teniaFoco{};

        void cargar();
};
