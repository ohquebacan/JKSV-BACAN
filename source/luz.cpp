#include "luz.hpp"

#include "led.hpp"

namespace
{
    bool s_errorPendiente = false;
}

void luz::empezarTarea()
{
    s_errorPendiente = false;
    led::pulso();
}

void luz::terminarTarea(bool fallo)
{
    if (fallo)
    {
        s_errorPendiente = true;
        led::error();
    }
    else { led::exito(); }
}

void luz::alTocar()
{
    if (!s_errorPendiente) { return; }
    s_errorPendiente = false;
    led::apagar();
}

void luz::alSalir() { led::alSalir(); }
