#include "appstates/DataLoadingState.hpp"

#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "logging/logger.hpp"

namespace
{
    constexpr int SCREEN_CENTER = 640;
}

//                      ---- Construction ----

DataLoadingState::DataLoadingState(data::DataContext &context,
                                   DestructFunction destructFunction,
                                   sys::threadpool::JobFunction function,
                                   sys::Task::TaskData taskData)
    : BaseTask()
    , m_context(context)
    , m_destructFunction(destructFunction)
{
    DataLoadingState::initialize_static_members();
    m_task = std::make_unique<sys::Task>(function, taskData);
}

//                      ---- Public functions ----

void DataLoadingState::update()
{
    BaseTask::update_loading_glyph();
    if (!m_task->is_running()) { DataLoadingState::deactivate_state(); }
    m_context.process_icon_queue();
}

void DataLoadingState::sub_update() { BaseTask::update_loading_glyph(); }

void DataLoadingState::render()
{
    static constexpr int ICON_X_COORD = SCREEN_CENTER - 128;
    static constexpr int ICON_Y_COORD = 226;
    const std::string status          = m_task->get_status();

    const int statusWidth = sdl::text::get_width(BaseTask::FONT_SIZE, status);
    m_statusX             = SCREEN_CENTER - (statusWidth / 2);

    // Rediseno: la Trifuerza grande del color del tema, el titulo en Upheaval y el estado debajo.
    static sdl::SharedTexture trifuerza = sdl::TextureManager::load("triforceBig", "romfs:/Textures/TriforceBig.png");
    static sdl::SharedTexture titulo    = sdl::TextureManager::load("headerTitle", "romfs:/Textures/Title.png");
    (void)ICON_X_COORD;
    (void)ICON_Y_COORD;
    sdl::render_rect_fill(sdl::Texture::Null, 0, 0, graphics::SCREEN_WIDTH, graphics::SCREEN_HEIGHT, colors::CLEAR_COLOR);
    if (trifuerza)
    {
        trifuerza->set_color_mod(colors::BLUE_GREEN);
        trifuerza->render(sdl::Texture::Null, SCREEN_CENTER - 54, 236);
    }
    if (titulo) { titulo->render(sdl::Texture::Null, SCREEN_CENTER - 175, 360); }
    const int estadoW = sdl::text::get_width(20, status);
    sdl::text::render(sdl::Texture::Null, SCREEN_CENTER - estadoW / 2, 430, 20, sdl::text::NO_WRAP, colors::DIALOG_LIGHT, status);
    (void)m_statusX;
    BaseTask::render_loading_glyph();
}

//                      ---- Private functions ----

void DataLoadingState::initialize_static_members()
{
    if (sm_jksvIcon) { return; }

    sm_jksvIcon = sdl::TextureManager::load("LoadingIcon", "romfs:/Textures/LoadingIcon.png");
}

void DataLoadingState::deactivate_state()
{
    // This is to catch any stragglers.
    m_context.process_icon_queue();
    if (m_destructFunction) { m_destructFunction(); }
    BaseState::deactivate();
}
