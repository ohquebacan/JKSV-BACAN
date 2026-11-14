#pragma once

// JKSV BACÁN: la luz de los mandos (y la de la placa en la Lite) acompaña a las
// tareas largas (respaldar, restaurar, la nube), para estar pendiente sin mirar
// la pantalla:
//   - trabajando: late suave (con doble parpadeo si la batería está baja);
//   - terminó bien: dos destellos y se apaga;
//   - algo falló: parpadea sin parar hasta que se toque un botón.
namespace luz
{
    void empezarTarea();
    void terminarTarea(bool fallo);
    void alTocar();   // hubo un botón: el aviso de error ya se vio
    void alSalir();
}
