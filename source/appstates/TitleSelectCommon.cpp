#include "appstates/TitleSelectCommon.hpp"

#include "graphics/colors.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"

//                      ---- Construction ----

TitleSelectCommon::TitleSelectCommon() { TitleSelectCommon::initialize_control_guide(); }

//                      ---- Public functions ----

void TitleSelectCommon::sub_update() { sm_controlGuide->sub_update(); }

//                      ---- Private functions ----

void TitleSelectCommon::initialize_control_guide()
{
    if (sm_controlGuide) { return; }

    // Rediseno: textos cortos.
    sm_controlGuide = ui::ControlGuide::create(strings::tr("\uE0E0 Open    \uE0E3 Favorite    \uE0E2 Options    ZL/ZR Filter    \uE0E1 Back",
                                                          "\uE0E0 Abrir    \uE0E3 Favorito    \uE0E2 Opciones    ZL/ZR Filtro    \uE0E1 Atrás"));
}
