#pragma once
#include "appstates/BaseState.hpp"
#include "sdl.hpp"
#include "ui/DialogBox.hpp"

#include <chrono>
#include <memory>
#include <vector>

/// @brief Invitacion a donar, como la de OQB Updater: el texto, el QR del enlace y «En otra ocasión», que se habilita
/// tras una cuenta regresiva corta. Sale la 3.a vez que se abre JKSV y despues como mucho una vez cada 3 dias.
class DonarState final : public BaseState
{
    public:
        DonarState();

        static inline std::shared_ptr<DonarState> create() { return std::make_shared<DonarState>(); }

        /// @brief Cuenta una apertura de JKSV y dice si toca mostrarla.
        static bool toca();

        /// @brief Lo empuja con el oscurecido de fondo, como los demas dialogos, y anota que salio.
        static void create_push_fade();

        void update() override;
        void render() override;

    private:
        std::shared_ptr<ui::DialogBox> m_caja{};
        std::vector<uint8_t> m_qr{};
        int m_lado{};
        bool m_guarda{};
        std::chrono::steady_clock::time_point m_inicio{std::chrono::steady_clock::now()};

        int restantes() const;
        void cerrar();
};
