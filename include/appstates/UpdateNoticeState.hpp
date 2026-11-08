#pragma once
#include "appstates/BaseState.hpp"
#include "sdl.hpp"
#include "ui/DialogBox.hpp"

#include <memory>
#include <string>

/// @brief Aviso de version nueva, como el de OQB Updater: «Actualizar ahora» o «Más tarde».
/// «Más tarde» solo lo cierra: vuelve a salir la proxima vez que se abra JKSV.
class UpdateNoticeState final : public BaseState
{
    public:
        UpdateNoticeState();

        static inline std::shared_ptr<UpdateNoticeState> create() { return std::make_shared<UpdateNoticeState>(); }

        /// @brief Lo empuja con el oscurecido de fondo, como los demas dialogos.
        static void create_push_fade();

        void update() override;
        void render() override;

    private:
        /// @brief 0 = Actualizar ahora, 1 = Mas tarde.
        int m_opcion{};

        /// @brief Para que la A que abrio algo antes no lo dispare en el primer cuadro.
        bool m_guarda{};

        std::string m_texto{}, m_notas{};

        std::shared_ptr<ui::DialogBox> m_caja{};
        sdl::SharedSound m_cursor{}, m_pop{};

        void cerrar(bool actualizar);
};
