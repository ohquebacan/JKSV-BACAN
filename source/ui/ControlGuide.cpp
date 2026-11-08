#include "ui/ControlGuide.hpp"

#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "logging/logger.hpp"

namespace
{
    /// @brief This is the X coordinate used to calculate where the guide should be.
    constexpr int GUIDE_X_OFFSET = 1250; // rediseno: alineado al borde del contenido

    /// @brief This is the font size used for the guides.
    constexpr int GUIDE_TEXT_SIZE = 18; // rediseno: una linea compacta

    /// @brief This is the constant Y of the transition.
    constexpr int TRANS_Y = 662;

    /// @brief The padding for the control guide container.
    constexpr int CONTAINER_PADDING = 24;
}

//                      ---- Construction ----

ui::ControlGuide::ControlGuide(const char *guide)
    : m_guide(guide)
    , m_textWidth(sdl::text::get_width(GUIDE_TEXT_SIZE, m_guide))
    , m_targetX(GUIDE_X_OFFSET - (m_textWidth + CONTAINER_PADDING))
    , m_guideWidth(graphics::SCREEN_WIDTH - m_targetX)
    , m_transition(graphics::SCREEN_WIDTH, TRANS_Y, 0, 0, m_targetX, TRANS_Y, 0, 0, ui::Transition::DEFAULT_THRESHOLD)
    , m_state(State::Hidden)
{
    ui::ControlGuide::initialize_static_members();
}

//                      ---- Public functions ----

void ui::ControlGuide::update(bool hasFocus)
{
    switch (m_state)
    {
        case State::Opening:
        case State::Hiding:  ControlGuide::update_position_state(); break;
        default:             ControlGuide::update_state(hasFocus); break;
    }
}

void ui::ControlGuide::sub_update()
{
    // To do: Maybe this differently?
    ControlGuide::update(false);
}

void ui::ControlGuide::render(sdl::SharedTexture &target, bool hasFocus)
{
    // These are for the rectangle that makes up the rest of the container.
    static constexpr int RECT_OFFSET_X = 16;
    static constexpr int RECT_HEIGHT   = 48;

    // This is where the text is rendered and its size.
    static constexpr int TEXT_OFFSET_X  = 24;
    static constexpr int TEXT_OFFSET_Y  = 10;

    // Grab the X and Y.
    const int guideX = m_transition.get_x();
    const int guideY = m_transition.get_y();

    // Rediseno: sin la capsula oscura; solo el texto, atenuado, alineado a la derecha del pie.
    (void)RECT_OFFSET_X;
    (void)RECT_HEIGHT;
    sdl::text::render(sdl::Texture::Null,
                      guideX + TEXT_OFFSET_X,
                      guideY + TEXT_OFFSET_Y + 4,
                      GUIDE_TEXT_SIZE,
                      sdl::text::NO_WRAP,
                      colors::DIALOG_LIGHT,
                      m_guide);
}

//                      ---- Private functions ----

void ui::ControlGuide::initialize_static_members()
{
    // Name for the texture manager.
    static constexpr std::string_view NAME_CAP = "ControlGuideCap";

    // If it's already loaded, return.
    if (sm_controlCap) { return; }
    sm_controlCap = sdl::TextureManager::load(NAME_CAP, "romfs:/Textures/GuideCap.png");
}

void ui::ControlGuide::reset() noexcept
{
    // Set both to the width of the screen.
    m_transition.set_target_x(graphics::SCREEN_WIDTH);
    m_transition.set_x(graphics::SCREEN_WIDTH);

    // Set the state to Hidden.
    m_state = State::Hidden;
}

void ui::ControlGuide::update_position_state() noexcept
{
    // Update the transition.
    m_transition.update();

    // State conditions.
    const bool opened = m_state == State::Opening && m_transition.in_place_xy();
    const bool hidden = m_state == State::Hiding && m_transition.in_place_xy();
    if (opened) { m_state = State::Opened; }
    else if (hidden) { m_state = State::Hidden; }
}

void ui::ControlGuide::update_state(bool hasFocus) noexcept
{
    // Conditions.
    const bool hide   = (m_state == State::Opening || m_state == State::Opened) && !hasFocus;
    const bool unhide = (m_state == State::Hiding || m_state == State::Hidden) && hasFocus;
    if (hide)
    {
        // Set target to screen width.
        m_transition.set_target_x(graphics::SCREEN_WIDTH);
        m_state = State::Hiding;
    }
    else if (unhide)
    {
        // Set target to open.
        m_transition.set_target_x(m_targetX);
        m_state = State::Opening;
    }
}