#include "ui/DialogBox.hpp"

#include "graphics/colors.hpp"

namespace
{
    constexpr int CORNER_WIDTH  = 16;
    constexpr int CORNER_HEIGHT = 16;
}

//                      ---- Construction ----

ui::DialogBox::DialogBox(int x, int y, int width, int height, ui::DialogBox::Type type)
    : m_x(x)
    , m_y(y)
    , m_width(width)
    , m_height(height)
    , m_type(type)
{
    ui::DialogBox::initialize_static_members();
}

//                      ---- Public functions ----

void ui::DialogBox::render(sdl::SharedTexture &target, bool hasFocus)
{
    const bool darkDialog = m_type == DialogBox::Type::Dark;
    const sdl::Color rectColor = darkDialog ? colors::DIALOG_DARK : colors::DIALOG_LIGHT;
    // Rediseno: esquinas de un circulo blanco suavizado, tenidas del color del cuadro (las grises fijas no
    // combinaban con el tema y dejaban esquinas claras).
    static sdl::SharedTexture redondas = sdl::TextureManager::load("dialogCornersRound", "romfs:/Textures/DialogCornersRound.png");
    sdl::SharedTexture &corners        = redondas ? redondas : (darkDialog ? sm_darkCorners : sm_lightCorners);
    if (redondas) { redondas->set_color_mod(rectColor); }

    // Top
    corners->render_part(target, m_x, m_y, 0, 0, CORNER_WIDTH, CORNER_HEIGHT);
    sdl::render_rect_fill(target, m_x + CORNER_WIDTH, m_y, m_width - (CORNER_WIDTH * 2), CORNER_HEIGHT, rectColor);
    corners->render_part(target, (m_x + m_width) - CORNER_WIDTH, m_y, CORNER_WIDTH, 0, CORNER_WIDTH, CORNER_HEIGHT);

    // Middle
    sdl::render_rect_fill(target, m_x, m_y + CORNER_HEIGHT, m_width, m_height - (CORNER_HEIGHT * 2), rectColor);

    // Bottom
    corners->render_part(target, m_x, (m_y + m_height) - CORNER_HEIGHT, 0, CORNER_HEIGHT, CORNER_WIDTH, CORNER_HEIGHT);
    sdl::render_rect_fill(target,
                          m_x + CORNER_WIDTH,
                          (m_y + m_height) - CORNER_HEIGHT,
                          m_width - (CORNER_WIDTH * 2),
                          CORNER_HEIGHT,
                          rectColor);
    corners->render_part(target,
                         (m_x + m_width) - CORNER_WIDTH,
                         (m_y + m_height) - CORNER_HEIGHT,
                         CORNER_WIDTH,
                         CORNER_HEIGHT,
                         CORNER_WIDTH,
                         CORNER_HEIGHT);
}

void ui::DialogBox::set_x(int x) noexcept { m_x = x; }

void ui::DialogBox::set_y(int y) noexcept { m_y = y; }

void ui::DialogBox::set_width(int width) noexcept { m_width = width; }

void ui::DialogBox::set_height(int height) noexcept { m_height = height; }

void ui::DialogBox::set_from_transition(ui::Transition &transition, bool centered)
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

void ui::DialogBox::initialize_static_members()
{
    if (sm_darkCorners && sm_lightCorners) { return; }

    sm_darkCorners  = sdl::TextureManager::load("darkCorners", "romfs:/Textures/DialogCornersDark.png");
    sm_lightCorners = sdl::TextureManager::load("lightCorners", "romfs:/Textures/DialogCornersLight.png");
}
