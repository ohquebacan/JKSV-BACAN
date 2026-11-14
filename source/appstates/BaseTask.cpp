#include "appstates/BaseTask.hpp"
#include "error.hpp"
#include "luz.hpp"

#include "graphics/colors.hpp"
#include "input.hpp"
#include "strings/strings.hpp"
#include "ui/PopMessageManager.hpp"

namespace
{
    /// @brief This is the time in milliseconds between changing glyphs.
    constexpr uint64_t TICKS_GLYPH_TRIGGER = 50;
} // namespace

//                      ---- Construction ----
BaseTask::BaseTask()
    : BaseState(false)
    , m_frameTimer(TICKS_GLYPH_TRIGGER)
    , m_popUnableExit(strings::get_by_name(strings::names::GENERAL_POPS, 0))
{
    m_errorsAtStart = error::count();
}

void BaseTask::follow_light()
{
    // Solo las tareas que lo llaman (respaldar, restaurar, la nube) encienden la
    // luz: la carga de partidas al abrir JKSV también es una tarea, pero no avisa.
    if (!m_lightStarted)
    {
        m_lightStarted = true;
        luz::empezarTarea();
    }
    if (m_lightFinished || !m_task || m_task->is_running()) { return; }
    m_lightFinished = true;
    luz::terminarTarea(error::count() > m_errorsAtStart);
}

//                      ---- Public functions ----

void BaseTask::update_loading_glyph()
{
    m_colorMod.update();

    if (!m_frameTimer.is_triggered()) { return; }
    else if (++m_currentFrame % 8 == 0) { m_currentFrame = 0; }
}

void BaseTask::pop_on_plus()
{
    const bool plusPressed = input::button_pressed(HidNpadButton_Plus);

    if (plusPressed) { ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, m_popUnableExit); }
}

void BaseTask::render_loading_glyph()
{
    // Render coords.
    static constexpr int RENDER_X    = 56;
    static constexpr int RENDER_Y    = 673;
    static constexpr int RENDER_SIZE = 32;

    sdl::text::render(sdl::Texture::Null,
                      RENDER_X,
                      RENDER_Y,
                      RENDER_SIZE,
                      sdl::text::NO_WRAP,
                      m_colorMod,
                      sm_glyphArray[m_currentFrame]);
}
