#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "ui/ControlGuide.hpp"
#include "ui/Menu.hpp"

#include <memory>

/// @brief La seccion Nube del menu principal: estado de la nube arriba a la derecha, sincronizar, los juegos que
/// hay alla, probar la conexion y subir al salir. La conexion (Koofr, WebDAV, Drive) es una opcion mas.
class CloudHubState final : public BaseState
{
    public:
        CloudHubState();

        static inline std::shared_ptr<CloudHubState> create() { return std::make_shared<CloudHubState>(); }

        void update() override;
        void sub_update() override;
        void render() override;

    private:
        std::shared_ptr<ui::Menu> m_menu{};
        std::shared_ptr<ui::ControlGuide> m_controlGuide{};
        double m_desplazamiento{};
        bool m_teniaFoco{};

        void elegir(int accion);
        void dibujar_estado();
};
