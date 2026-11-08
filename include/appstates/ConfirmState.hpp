#pragma once
#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "appstates/FadeState.hpp"
#include "appstates/ProgressState.hpp"
#include "appstates/TaskState.hpp"
#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"
#include "sys/sys.hpp"
#include "ui/ui.hpp"

#include <memory>
#include <string>
#include <switch.h>

namespace
{
    // This is the base position on the screen used to center the Yes [A](...) text.
    constexpr int COORD_YES_X = 460;
    // ^ but for no.
    constexpr int COORD_NO_X = 820;

    // Transition starting vars.
    constexpr int TRANS_X                  = 280;
    constexpr int TRANS_Y                  = 229;
    constexpr int TRANS_BEGIN_WIDTH_HEIGHT = 32;
    constexpr int TRANS_END_WIDTH          = 720;
    constexpr int TRANS_END_HEIGHT         = 256;
} // namespace

/// @brief Templated class to create confirmation dialogs.
/// @tparam TaskType The type of task spawned on confirmation. Ex: Task, ProgressTask
/// @tparam StateType The state type spawned on confirmation. Ex: TaskState, ProgressState
/// @tparam StructType The type of struct passed to the state on confirmation.
template <typename TaskType, typename StateType>
class ConfirmState final : public BaseState
{
    public:
        /// @brief Constructor for new ConfirmState.
        /// @param query The string displayed.
        /// @param holdRequired Whether or not confirmation requires holding A for three seconds.
        /// @param function Function executed on confirmation.
        /// @param dataStruct shared_ptr<StructType> that is passed to function. I tried templating this and it was a nightmare.
        ConfirmState(std::string_view query,
                     bool holdRequired,
                     sys::threadpool::JobFunction onConfirm,
                     sys::threadpool::JobFunction onCancel,
                     sys::Task::TaskData taskData)
            : BaseState(false)
            , m_query(query)
            , m_holdRequired(holdRequired)
            , m_transition(TRANS_X,
                           TRANS_Y,
                           TRANS_BEGIN_WIDTH_HEIGHT,
                           TRANS_BEGIN_WIDTH_HEIGHT,
                           TRANS_X,
                           TRANS_Y,
                           TRANS_END_WIDTH,
                           TRANS_END_HEIGHT,
                           ui::Transition::DEFAULT_THRESHOLD)
            , m_state(State::Opening)
            , m_onConfirm(onConfirm)
            , m_onCancel(onCancel)
            , m_taskData(taskData)
        {
            ConfirmState::initialize_static_members();
            ConfirmState::center_no();
            ConfirmState::center_yes();
            sm_dialogPop->play();
        }

        /// @brief Same as above. Difference is that the query string is moved instead of assigned.
        ConfirmState(std::string &query,
                     bool holdRequired,
                     sys::threadpool::JobFunction onConfirm,
                     sys::threadpool::JobFunction onCancel,
                     sys::Task::TaskData taskData)
            : BaseState(false)
            , m_query(std::move(query))
            , m_holdRequired(holdRequired)
            , m_transition(TRANS_X,
                           TRANS_Y,
                           TRANS_BEGIN_WIDTH_HEIGHT,
                           TRANS_BEGIN_WIDTH_HEIGHT,
                           TRANS_X,
                           TRANS_Y,
                           TRANS_END_WIDTH,
                           TRANS_END_HEIGHT,
                           ui::Transition::DEFAULT_THRESHOLD)
            , m_state(State::Opening)
            , m_onConfirm(onConfirm)
            , m_onCancel(onCancel)
            , m_taskData(taskData)
        {
            ConfirmState::initialize_static_members();
            ConfirmState::center_no();
            ConfirmState::center_yes();
            sm_dialogPop->play();
        }

        /// @brief Returns a new ConfirmState. See constructor.
        static inline std::shared_ptr<ConfirmState> create(std::string_view query,
                                                           bool holdRequired,
                                                           sys::threadpool::JobFunction onConfirm,
                                                           sys::threadpool::JobFunction onCancel,
                                                           sys::Task::TaskData taskData)
        {
            return std::make_shared<ConfirmState>(query, holdRequired, onConfirm, onCancel, taskData);
        }

        /// @brief Returns a new ConfirmState. See constructor.
        static inline std::shared_ptr<ConfirmState> create(std::string &query,
                                                           bool holdRequired,
                                                           sys::threadpool::JobFunction onConfirm,
                                                           sys::threadpool::JobFunction onCancel,
                                                           sys::Task::TaskData taskData)
        {
            return std::make_shared<ConfirmState>(query, holdRequired, onConfirm, onCancel, taskData);
        }

        /// @brief Creates and returns a new ConfirmState and pushes it.
        static inline std::shared_ptr<ConfirmState> create_and_push(std::string_view query,
                                                                    bool holdRequired,
                                                                    sys::threadpool::JobFunction onConfirm,
                                                                    sys::threadpool::JobFunction onCancel,
                                                                    sys::Task::TaskData taskData)
        {
            // I'm gonna use a sneaky trick here. This shouldn't do this because it's confusing.
            auto newState = ConfirmState::create(query, holdRequired, onConfirm, onCancel, taskData);
            StateManager::push_state(newState);
            return newState;
        }

        /// @brief Creates and returns a new ConfirmState and pushes it.
        static inline std::shared_ptr<ConfirmState> create_and_push(std::string &query,
                                                                    bool holdRequired,
                                                                    sys::threadpool::JobFunction onConfirm,
                                                                    sys::threadpool::JobFunction onCancel,
                                                                    sys::Task::TaskData taskData)
        {
            // I'm gonna use a sneaky trick here. This shouldn't do this because it's confusing.
            auto newState = ConfirmState::create(query, holdRequired, onConfirm, onCancel, taskData);
            StateManager::push_state(newState);
            return newState;
        }

        /// @brief Same as above but with a fade transition pushed  in between.
        static std::shared_ptr<ConfirmState> create_push_fade(std::string_view query,
                                                              bool holdRequired,
                                                              sys::threadpool::JobFunction onConfirm,
                                                              sys::threadpool::JobFunction onCancel,
                                                              sys::Task::TaskData taskData)
        {
            auto newState = ConfirmState::create(query, holdRequired, onConfirm, onCancel, taskData);
            FadeState::create_and_push(colors::DIM_BACKGROUND, colors::ALPHA_FADE_BEGIN, colors::ALPHA_FADE_END, newState);
            return newState;
        }

        /// @brief Same as above but with a fade transition pushed  in between.
        static std::shared_ptr<ConfirmState> create_push_fade(std::string &query,
                                                              bool holdRequired,
                                                              sys::threadpool::JobFunction onConfirm,
                                                              sys::threadpool::JobFunction onCancel,
                                                              sys::Task::TaskData taskData)
        {
            auto newState = ConfirmState::create(query, holdRequired, onConfirm, onCancel, taskData);
            FadeState::create_and_push(colors::DIM_BACKGROUND, colors::ALPHA_FADE_BEGIN, colors::ALPHA_FADE_END, newState);
            return newState;
        }

        /// @brief Just updates the ConfirmState.
        void update() override
        {
            switch (m_state)
            {
                case State::Opening:    ConfirmState::update_dimensions(); break;
                case State::Displaying: ConfirmState::update_handle_input(); break;
                case State::Closing:    ConfirmState::update_dimensions(); break;
            }
        }

        /// @brief Renders the state to screen.
        void render() override
        {
            // These are the rendering coordinates that aren't affected by the transition.
            static constexpr int TEXT_X          = 312;
            static constexpr int TEXT_Y_OFFSET   = 24;
            static constexpr int TEXT_FONT_SIZE  = 20;
            static constexpr int TEXT_WRAP_WIDTH = 656;

            // Divider line A.
            static constexpr int LINE_A_X_A = 280;
            static constexpr int LINE_A_X_B = 999;

            // Divider line B.
            static constexpr int LINE_B_X   = 640;
            static constexpr int LINE_B_Y_B = 255;

            // Shared line stuff.
            static constexpr int LINE_Y_OFFSET = 192;

            // Yes/NO
            static constexpr int OPTION_Y_OFFSET  = 214;
            static constexpr int OPTION_FONT_SIZE = 22;

            const bool hasFocus = BaseState::has_focus();
            const int y         = m_transition.get_y();

            // This is the dimming rectangle.
            sdl::render_rect_fill(sdl::Texture::Null,
                                  0,
                                  0,
                                  graphics::SCREEN_WIDTH,
                                  graphics::SCREEN_HEIGHT,
                                  colors::DIM_BACKGROUND);

            // Render the dialog. Only render the rest if we're in the display state.
            sm_dialog->render(sdl::Texture::Null, hasFocus);
            if (!m_transition.in_place() || m_state != State::Displaying) { return; }

            // Main string.
            sdl::text::render(sdl::Texture::Null,
                              TEXT_X,
                              y + TEXT_Y_OFFSET,
                              TEXT_FONT_SIZE,
                              TEXT_WRAP_WIDTH,
                              colors::WHITE,
                              m_query);

            // Divider lines.
            sdl::render_line(sdl::Texture::Null,
                             LINE_A_X_A,
                             y + LINE_Y_OFFSET,
                             LINE_A_X_B,
                             y + LINE_Y_OFFSET,
                             colors::DIV_COLOR);
            sdl::render_line(sdl::Texture::Null, LINE_B_X, y + LINE_Y_OFFSET, LINE_B_X, y + LINE_B_Y_B, colors::DIV_COLOR);

            // Yes. Al mantener A el texto cambia a uno largo ("Sigue manteniendo presionado"): se achica hasta
            // caber en su mitad del cuadro (360 px) y se centra con ese tamano, para que no se salga.
            {
                int yesSize = OPTION_FONT_SIZE;
                while (yesSize > 14 && sdl::text::get_width(yesSize, sm_yes) > 330) { --yesSize; }
                const int yesX = COORD_YES_X - (sdl::text::get_width(yesSize, sm_yes) / 2);
                sdl::text::render(sdl::Texture::Null,
                                  yesX,
                                  y + OPTION_Y_OFFSET + (OPTION_FONT_SIZE - yesSize) / 2,
                                  yesSize,
                                  sdl::text::NO_WRAP,
                                  colors::WHITE,
                                  sm_yes);
            }

            // No
            sdl::text::render(sdl::Texture::Null,
                              m_noX,
                              y + OPTION_Y_OFFSET,
                              OPTION_FONT_SIZE,
                              sdl::text::NO_WRAP,
                              colors::WHITE,
                              sm_no);
        }

    private:
        /// @brief States this state can be in.
        enum class State : uint8_t
        {
            Opening,
            Displaying,
            Closing
        };

        /// @brief This stores the query text.
        std::string m_query{};

        /// @brief X coordinate to render the Yes No
        int m_yesX{};
        int m_noX{};

        /// @brief This is to prevent the dialog from triggering immediately.
        bool m_triggerGuard{};

        /// @brief Whether or not the action was confirmed
        bool m_confirmed{};

        /// @brief Whether or not holding [A] to confirm is required.
        const bool m_holdRequired{};

        /// @brief Keep track of the ticks/time needed to confirm.
        uint64_t m_startingTickCount{};

        /// @brief Keeps track of which stage the confirmation is in.
        int m_stage{};

        /// @brief To make the dialog pop in from bottom to be more inline with the rest of the UI.
        ui::Transition m_transition{};

        /// @brief Tracks the state the dialog is currently in.
        ConfirmState::State m_state{};

        /// @brief Function to execute if action is confirmed.
        const sys::threadpool::JobFunction m_onConfirm{};

        /// @brief Function to execute if action is cancelled.
        const sys::threadpool::JobFunction m_onCancel{};

        /// @brief Pointer to data struct passed to ^
        const sys::Task::TaskData m_taskData{};

        // All of these strings are shared between all instances.
        static inline const char *sm_yes{};
        static inline const char *sm_no{};
        static inline const char *sm_hold[3]{};

        /// @brief This dialog is shared between all instances.
        static inline std::shared_ptr<ui::DialogBox> sm_dialog{};

        /// @brief This sound is shared.
        static inline sdl::SharedSound sm_dialogPop{};

        void initialize_static_members()
        {
            // Name and path to the sound used.
            static constexpr std::string_view POP_SOUND = "ConfirmPop";
            static constexpr const char *POP_PATH       = "romfs:/Sound/ConfirmPop.wav";

            // To do: Not sure about checking the holding text.
            if (sm_dialog && sm_dialogPop && sm_yes && sm_no) { return; }

            sm_dialog    = ui::DialogBox::create(0, 0, 0, 0);
            sm_dialogPop = sdl::SoundManager::load(POP_SOUND, POP_PATH);
            sm_dialog->set_from_transition(m_transition, true);

            // Load yes and no.
            sm_yes = strings::get_by_name(strings::names::YES_NO_OK, 0);
            sm_no  = strings::get_by_name(strings::names::YES_NO_OK, 1);

            // Loop load the holding strings.
            const char *hold{};
            for (int i = 0; (hold = strings::get_by_name(strings::names::HOLDING_STRINGS, i)); i++) { sm_hold[i] = hold; }
        }

        /// @brief Updates the dimensions of the dialog.
        void update_dimensions() noexcept
        {
            // Update the dialog to be centered.
            m_transition.update();
            sm_dialog->set_from_transition(m_transition, true);

            // Conditions for state switching.
            const bool opened = m_state == State::Opening && m_transition.in_place();
            const bool closed = m_state == State::Closing && m_transition.in_place();
            if (opened) { m_state = State::Displaying; }
            else if (closed) { ConfirmState::deactivate_state(); }
        }

        /// @brief Handles the input and updating.
        void update_handle_input() noexcept
        {
            // Grab our input bools.
            const bool aPressed  = input::button_pressed(HidNpadButton_A);
            const bool bPressed  = input::button_pressed(HidNpadButton_B);
            const bool aHeld     = input::button_held(HidNpadButton_A);
            const bool aReleased = input::button_released(HidNpadButton_A);

            // This is to prevent A from auto triggering the dialog.
            m_triggerGuard = m_triggerGuard || (aPressed && !m_triggerGuard);

            // Conditions.
            const bool noHoldTrigger = m_triggerGuard && aPressed && !m_holdRequired;
            const bool holdTriggered = m_triggerGuard && aPressed && m_holdRequired;
            const bool holdSustained = m_triggerGuard && aHeld && m_holdRequired;

            if (noHoldTrigger) { ConfirmState::confirmed(); }
            else if (holdTriggered) { ConfirmState::hold_triggered(); }
            else if (holdSustained) { ConfirmState::hold_sustained(); }
            else if (aReleased) { ConfirmState::hold_released(); }
            else if (bPressed) { ConfirmState::close_dialog(); }
        }

        // This just centers the Yes or holding text.
        void center_yes()
        {
            const int yesWidth = sdl::text::get_width(22, sm_yes);
            m_yesX             = COORD_YES_X - (yesWidth / 2);
        }

        void center_no()
        {
            const int noWidth = sdl::text::get_width(22, sm_no);
            m_noX             = COORD_NO_X - (noWidth / 2);
        }

        void confirmed()
        {
            m_confirmed = true;
            ConfirmState::close_dialog();
        }

        void hold_triggered()
        {
            m_startingTickCount = SDL_GetTicks64();
            ConfirmState::change_holding_text(0);
        }

        void hold_sustained()
        {
            const uint64_t totalTicks = SDL_GetTicks64() - m_startingTickCount;
            const bool threeSeconds   = totalTicks >= 3000;
            const bool twoSeconds     = totalTicks >= 2000;
            const bool oneSecond      = totalTicks >= 1000;

            if (threeSeconds) { ConfirmState::confirmed(); }
            else if (twoSeconds) { ConfirmState::change_holding_text(2); }
            else if (oneSecond) { ConfirmState::change_holding_text(1); }
        }

        void hold_released()
        {
            sm_yes = strings::get_by_name(strings::names::YES_NO_OK, 0);
            ConfirmState::center_yes();
        }

        void deactivate_state()
        {
            // Conditions.
            const bool validConfirm = m_confirmed && m_onConfirm;
            const bool validCancel  = !m_confirmed && m_onCancel;

            if (validConfirm) { StateType::create_and_push(m_onConfirm, m_taskData); }
            else if (validCancel) { StateType::create_and_push(m_onCancel, m_taskData); }
            else
            {
                FadeState::create_and_push(colors::DIM_BACKGROUND, colors::ALPHA_FADE_END, colors::ALPHA_FADE_BEGIN, nullptr);
            }

            // Return the yes to its original state.
            sm_yes = strings::get_by_name(strings::names::YES_NO_OK, 0);

            // Deactivate this state.
            BaseState::deactivate();
        }

        void change_holding_text(int index)
        {
            sm_yes = sm_hold[index];
            ConfirmState::center_yes();
        }

        void close_dialog()
        {
            m_state = State::Closing;
            m_transition.set_target_width(32);
            m_transition.set_target_height(32);
        }
};

using ConfirmTask     = ConfirmState<sys::Task, TaskState>;
using ConfirmProgress = ConfirmState<sys::ProgressTask, ProgressState>;
