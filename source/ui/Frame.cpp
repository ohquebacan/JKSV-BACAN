#include "ui/Frame.hpp"

#include "graphics/colors.hpp"

//                      ---- Construction ----

ui::Frame::Frame(int x, int y, int width, int height)
    : m_x(x)
    , m_y(y)
    , m_width(width)
    , m_height(height)
{
    Frame::initialize_static_members();
}

//                      ---- Public functions ----

void ui::Frame::render(sdl::SharedTexture &target, bool hasFocus)
{
    // Rediseno: panel del tema con un borde fino, en lugar del marco de imagen con bordes blancos.
    if (m_width <= 0 || m_height <= 0) { return; }
    sdl::render_rect_fill(target, m_x, m_y, m_width, m_height, colors::PANEL_SOLID);
    sdl::render_line(target, m_x, m_y, m_x + m_width - 1, m_y, colors::DIV_COLOR);
    sdl::render_line(target, m_x, m_y + m_height - 1, m_x + m_width - 1, m_y + m_height - 1, colors::DIV_COLOR);
    sdl::render_line(target, m_x, m_y, m_x, m_y + m_height - 1, colors::DIV_COLOR);
    sdl::render_line(target, m_x + m_width - 1, m_y, m_x + m_width - 1, m_y + m_height - 1, colors::DIV_COLOR);
}

void ui::Frame::set_x(int x) noexcept { m_x = x; }

void ui::Frame::set_y(int y) noexcept { m_y = y; }

void ui::Frame::set_width(int width) noexcept { m_width = width; }

void ui::Frame::set_height(int height) noexcept { m_height = height; }

void ui::Frame::set_from_transition(const ui::Transition &transition, bool centered) noexcept
{
    const int x      = centered ? transition.get_centered_x() : transition.get_x();
    const int y      = centered ? transition.get_centered_y() : transition.get_y();
    const int width  = transition.get_width();
    const int height = transition.get_height();

    m_x      = x;
    m_y      = y;
    m_width  = width;
    m_height = height;
}

//                      ---- Private functions ----

void ui::Frame::initialize_static_members()
{
    static constexpr std::string_view FRAME_NAME = "FrameCorners";
    static constexpr const char *FRAME_PATH      = "romfs:/Textures/Frame.png";

    if (sm_frameCorners) { return; }
    sm_frameCorners = sdl::TextureManager::load(FRAME_NAME, FRAME_PATH);
}
