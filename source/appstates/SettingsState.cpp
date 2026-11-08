#include "appstates/SettingsState.hpp"

#include "appstates/BlacklistEditState.hpp"
#include "appstates/MainMenuState.hpp"
#include "appstates/MessageState.hpp"
#include "config/config.hpp"
#include "data/data.hpp"
#include "error.hpp"
#include "fs/fs.hpp"
#include "fslib.hpp"
#include "graphics/colors.hpp"
#include "graphics/theme.hpp"
#include "ui/ListaSecciones.hpp"
#include "input.hpp"
#include "keyboard/keyboard.hpp"
#include "logging/logger.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"

#include <array>
#include <string>
#include <vector>

namespace
{
    /// @brief Name of the target all "secondary" states share.
    constexpr std::string_view SECONDARY_TARGET = "SecondaryTarget";

    /// @brief This is used to place "NULL" keys in our local array
    constexpr std::string_view CONFIG_KEY_NULL = "NULL";

    /// @brief This makes it easier to work with key indexes. Anything NULL is something that is not easily toggled.
    constexpr std::array<std::string_view, 26> CONFIG_KEY_ARRAY = {CONFIG_KEY_NULL,
                                                                   CONFIG_KEY_NULL,
                                                                   config::keys::INCLUDE_DEVICE_SAVES,
                                                                   config::keys::AUTO_BACKUP_ON_RESTORE,
                                                                   config::keys::AUTO_NAME_BACKUPS,
                                                                   config::keys::AUTO_UPLOAD,
                                                                   config::keys::KEEP_LOCAL_BACKUPS,
                                                                   config::keys::USE_TITLE_IDS,
                                                                   config::keys::ENGLISH_SAFE_TITLES,
                                                                   config::keys::HOLD_FOR_DELETION,
                                                                   config::keys::HOLD_FOR_RESTORATION,
                                                                   config::keys::HOLD_FOR_OVERWRITE,
                                                                   config::keys::ONLY_LIST_MOUNTABLE,
                                                                   config::keys::LIST_ACCOUNT_SYS_SAVES,
                                                                   config::keys::ALLOW_WRITING_TO_SYSTEM,
                                                                   config::keys::EXPORT_TO_ZIP,
                                                                   config::keys::ZIP_COMPRESSION_LEVEL,
                                                                   config::keys::TITLE_SORT_TYPE,
                                                                   config::keys::JKSM_TEXT_MODE,
                                                                   config::keys::FORCE_ENGLISH,
                                                                   config::keys::SHOW_DEVICE_USER,
                                                                   config::keys::SHOW_BCAT_USER,
                                                                   config::keys::SHOW_CACHE_USER,
                                                                   config::keys::SHOW_SYSTEM_USER,
                                                                   config::keys::ENABLE_TRASH_BIN,
                                                                   CONFIG_KEY_NULL};

    /// @brief These are the indexes used for case indexing.
    enum CaseIndexes
    {
        ChangeWorkDir = 0,
        EditBlacklist = 1,
        CycleZip      = 16,
        CycleSortType = 17,
        ToggleJKSM    = 18,
        ToggleTrash   = 24,
        CycleScaling  = 25,
        CycleTheme    = 26 // agregado por OQB: no esta en los textos traducidos, se arma con strings::tr
    };

    // Rediseno: el orden en pantalla, por secciones. idx = indice original de la opcion.
    struct Opcion
    {
        int idx;
        int seccion;
        const char *en;
        const char *es;
    };
    constexpr std::array<Opcion, 27> ORDEN = {{
        {3, 0, "Back up before restoring", "Respaldar antes de restaurar"},
        {4, 0, "Name backups automatically", "Nombre automático al respaldar"},
        {15, 0, "Save backups as .zip", "Guardar respaldos en .zip"},
        {16, 0, ".zip compression", "Compresión del .zip"},
        {24, 0, "Trash bin for deleted backups", "Papelera para lo borrado"},
        {0, 0, "Backup folder", "Carpeta de respaldos"},
        {9, 1, "Hold A to delete", "Mantener A para borrar"},
        {10, 1, "Hold A to restore", "Mantener A para restaurar"},
        {11, 1, "Hold A to overwrite", "Mantener A para sobrescribir"},
        {5, 2, "Upload to the cloud when backing up", "Subir a la nube al respaldar"},
        {6, 2, "Keep a copy on the console", "Conservar copia en la consola"},
        {17, 3, "Sort games by", "Ordenar juegos por"},
        {12, 3, "Only games with an accessible save", "Solo juegos con partida accesible"},
        {1, 3, "Hidden games", "Juegos ocultos"},
        {7, 3, "Folders named by game ID", "Carpetas con el ID del juego"},
        {8, 3, "Folder names without accents", "Carpetas sin acentos"},
        {18, 3, "Plain text list", "Lista de solo texto"},
        {2, 4, "Device saves with each user", "Partidas del dispositivo en cada usuario"},
        {20, 4, "Show Device", "Mostrar Dispositivo"},
        {21, 4, "Show BCAT", "Mostrar BCAT"},
        {22, 4, "Show Cache", "Mostrar Caché"},
        {23, 4, "Show System", "Mostrar Sistema"},
        {13, 4, "System saves of each account", "Partidas de sistema de cada cuenta"},
        {14, 4, "Allow writing system saves", "Permitir escribir partidas del sistema"},
        {26, 5, "Theme", "Tema"},
        {25, 5, "Animation speed", "Velocidad de animaciones"},
        {19, 5, "App in English", "App en inglés"},
    }};

    const char *nombre_seccion(int seccion)
    {
        switch (seccion)
        {
            case 0:  return strings::tr("BACKUPS", "RESPALDOS");
            case 1:  return strings::tr("CONFIRMATIONS", "CONFIRMACIONES");
            case 2:  return strings::tr("CLOUD", "NUBE");
            case 3:  return strings::tr("GAME LIST", "LISTA DE JUEGOS");
            case 4:  return strings::tr("SPECIAL USERS", "USUARIOS ESPECIALES");
            default: return strings::tr("APPEARANCE", "APARIENCIA");
        }
    }
} // namespace

//                      ---- Construction ----

SettingsState::SettingsState()
    : m_settingsMenu(ui::Menu::create(32, 10, 1000, 23, 555))
    , m_controlGuide(ui::ControlGuide::create(strings::get_by_name(strings::names::CONTROL_GUIDES, 3)))
    , m_renderTarget(sdl::TextureManager::load(SECONDARY_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
{
    SettingsState::load_settings_menu();
    SettingsState::load_extra_strings();
    SettingsState::update_menu_options();
}

//                      ---- Public functions ----

void SettingsState::update()
{
    const bool hasFocus     = BaseState::has_focus();
    const bool aPressed     = input::button_pressed(HidNpadButton_A);
    const bool bPressed     = input::button_pressed(HidNpadButton_B);
    const bool xPressed     = input::button_pressed(HidNpadButton_X);
    const bool minusPressed = input::button_pressed(HidNpadButton_Minus);

    m_settingsMenu->update(hasFocus);
    if (aPressed) { SettingsState::toggle_options(); }
    else if (xPressed) { SettingsState::reset_settings(); }
    else if (minusPressed) { SettingsState::create_push_description_message(); }
    else if (bPressed) { BaseState::deactivate(); }

    m_controlGuide->update(hasFocus);
}

void SettingsState::sub_update() { m_controlGuide->sub_update(); }

void SettingsState::render()
{
    const bool hasFocus = BaseState::has_focus();

    std::vector<ui::lista::Fila> filas{};
    filas.reserve(ORDEN.size());
    for (const Opcion &o : ORDEN)
    {
        ui::lista::Fila f{};
        f.seccion = nombre_seccion(o.seccion);
        f.titulo  = strings::tr(o.en, o.es);
        if (o.idx == CaseIndexes::ChangeWorkDir) { f.valor = config::get_working_directory().string(); }
        else if (o.idx == CaseIndexes::EditBlacklist) { f.valor = strings::tr("Edit", "Editar"); }
        else if (o.idx == CaseIndexes::CycleZip) { f.valor = std::to_string(config::get_by_key(config::keys::ZIP_COMPRESSION_LEVEL)); }
        else if (o.idx == CaseIndexes::CycleSortType)
        {
            const char *tipo = SettingsState::get_sort_type_text(config::get_by_key(config::keys::TITLE_SORT_TYPE));
            f.valor          = tipo ? tipo : "";
        }
        else if (o.idx == CaseIndexes::CycleScaling) { f.valor = stringutil::get_formatted_string("%.2f", config::get_animation_scaling()); }
        else if (o.idx == CaseIndexes::CycleTheme)
        {
            f.valor       = theme::name(config::get_by_key(config::keys::THEME));
            f.valorActivo = true;
        }
        else
        {
            const bool on = config::get_by_key(CONFIG_KEY_ARRAY[o.idx]);
            f.valor       = on ? strings::tr("On", "Encendido") : strings::tr("Off", "Apagado");
            f.valorActivo = on;
        }
        const char *desc = o.idx == CaseIndexes::CycleTheme
                               ? strings::tr("Changes the app's colors. Amber is the default.", "Cambia los colores de la app. Ámbar es el de por defecto.")
                               : strings::get_by_name(strings::names::SETTINGS_DESCRIPTIONS, o.idx);
        f.descripcion = desc ? desc : "";
        filas.push_back(std::move(f));
    }
    ui::lista::dibujar(filas, m_settingsMenu->get_selected(), m_desplazamiento, hasFocus);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void SettingsState::load_settings_menu()
{
    const char *option{};
    for (int i = 0; (option = strings::get_by_name(strings::names::SETTINGS_MENU, i)); i++)
    {
        m_settingsMenu->add_option(option);
    }
    // Tema: va al final, con su texto armado aqui (update_menu_options lo completa).
    m_settingsMenu->add_option(strings::tr("Theme", "Tema"));
}

void SettingsState::load_extra_strings()
{
    const char *onOff{}, *sortType{};
    for (int i = 0; (onOff = strings::get_by_name(strings::names::ON_OFF, i)); i++) { m_onOff[i] = onOff; }
    for (int i = 0; (sortType = strings::get_by_name(strings::names::SORT_TYPES, i)); i++) { m_sortTypes[i] = sortType; }
}

void SettingsState::update_menu_options()
{
    // These are the indexes that are a really simple toggle.
    static constexpr std::array<int, 21> TOGGLE_INDEXES = {2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                           13, 14, 15, 18, 19, 20, 21, 22, 23, 24};

    for (const int index : TOGGLE_INDEXES)
    {
        const char *optionFormat = strings::get_by_name(strings::names::SETTINGS_MENU, index);
        const uint8_t value      = config::get_by_key(CONFIG_KEY_ARRAY[index]);
        const char *status       = SettingsState::get_status_text(value);
        const std::string option = stringutil::get_formatted_string(optionFormat, status);
        m_settingsMenu->edit_option(index, option);
    }

    {
        const char *zipCompFormat   = strings::get_by_name(strings::names::SETTINGS_MENU, CaseIndexes::CycleZip);
        const uint8_t zipLevel      = config::get_by_key(CONFIG_KEY_ARRAY[CaseIndexes::CycleZip]);
        const std::string zipOption = stringutil::get_formatted_string(zipCompFormat, zipLevel);
        m_settingsMenu->edit_option(CaseIndexes::CycleZip, zipOption);
    }

    {
        const char *titleSortFormat      = strings::get_by_name(strings::names::SETTINGS_MENU, CaseIndexes::CycleSortType);
        const uint8_t sortType           = config::get_by_key(CONFIG_KEY_ARRAY[CaseIndexes::CycleSortType]);
        const char *typeText             = SettingsState::get_sort_type_text(sortType);
        const std::string sortTypeOption = stringutil::get_formatted_string(titleSortFormat, typeText);
        m_settingsMenu->edit_option(CaseIndexes::CycleSortType, sortTypeOption);
    }

    {
        const std::string themeOption = stringutil::get_formatted_string(strings::tr("Theme: %s", "Tema: %s"),
                                                                         theme::name(config::get_by_key(config::keys::THEME)));
        m_settingsMenu->edit_option(CaseIndexes::CycleTheme, themeOption);
    }

    {
        const char *scalingFormat       = strings::get_by_name(strings::names::SETTINGS_MENU, CaseIndexes::CycleScaling);
        const double scaling            = config::get_animation_scaling();
        const std::string scalingOption = stringutil::get_formatted_string(scalingFormat, scaling);
        m_settingsMenu->edit_option(CaseIndexes::CycleScaling, scalingOption);
    }
}

void SettingsState::change_working_directory()
{
    const int popTicks           = ui::PopMessageManager::DEFAULT_TICKS;
    const char *inputHeader      = strings::get_by_name(strings::names::KEYBOARD, 2);
    const char *popSuccessFormat = strings::get_by_name(strings::names::SETTINGS_POPS, 1);
    const char *popFailed        = strings::get_by_name(strings::names::SETTINGS_POPS, 2);

    std::array<char, FS_MAX_PATH> pathBuffer = {0};
    const fslib::Path oldPath                = config::get_working_directory();

    const bool input =
        keyboard::get_input(SwkbdType_Normal, oldPath.string().c_str(), inputHeader, pathBuffer.data(), FS_MAX_PATH);
    if (!input) { return; }

    const fslib::Path newPath{pathBuffer.data()};
    if (!newPath.is_valid())
    {
        ui::PopMessageManager::push_message(popTicks, popFailed);
        return;
    }

    const bool exists = fslib::directory_exists(newPath);
    bool moved{};
    if (exists)
    {
        moved = fs::move_directory_recursively(oldPath, newPath);
        error::fslib(fslib::delete_directory_recursively(oldPath));
    }
    else { moved = fslib::rename_directory(oldPath, newPath); }

    if (!moved)
    {
        ui::PopMessageManager::push_message(popTicks, popFailed);
        return;
    }

    // Since the path was validated before, this shouldn't need to be checked.
    config::set_working_directory(newPath);

    const std::string newPathString = newPath.string();
    std::string popMessage          = stringutil::get_formatted_string(popSuccessFormat, newPathString.c_str());
    ui::PopMessageManager::push_message(popTicks, popMessage);
}

void SettingsState::create_push_blacklist_edit()
{
    const bool isEmpty = config::blacklist_is_empty();
    if (isEmpty)
    {
        const int popTicks   = ui::PopMessageManager::DEFAULT_TICKS;
        const char *popEmpty = strings::get_by_name(strings::names::SETTINGS_POPS, 0);
        ui::PopMessageManager::push_message(popTicks, popEmpty);
        return;
    }

    BlacklistEditState::create_and_push();
}

void SettingsState::toggle_options()
{
    const int fila = m_settingsMenu->get_selected();
    if (fila < 0 || fila >= static_cast<int>(ORDEN.size())) { return; }
    const int selected = ORDEN[fila].idx;

    switch (selected)
    {
        case CaseIndexes::ChangeWorkDir: SettingsState::change_working_directory(); break;
        case CaseIndexes::EditBlacklist: SettingsState::create_push_blacklist_edit(); break;
        case CaseIndexes::CycleZip:      SettingsState::cycle_zip_level(); break;
        case CaseIndexes::CycleSortType: SettingsState::cycle_sort_type(); break;
        case CaseIndexes::ToggleJKSM:    SettingsState::toggle_jksm_mode(); break;
        case CaseIndexes::ToggleTrash:   SettingsState::toggle_trash_folder(); break;
        case CaseIndexes::CycleScaling:  SettingsState::cycle_anim_scaling(); break;
        case CaseIndexes::CycleTheme:    theme::cycle(); break;
        default:                         config::toggle_by_key(CONFIG_KEY_ARRAY[selected]); break;
    }

    SettingsState::update_menu_options();
}

void SettingsState::reset_settings()
{
    config::reset_to_default();
    theme::apply_from_config();
    SettingsState::update_menu_options();
}

void SettingsState::create_push_description_message()
{
    const int fila = m_settingsMenu->get_selected();
    if (fila < 0 || fila >= static_cast<int>(ORDEN.size())) { return; }
    const int selected      = ORDEN[fila].idx;
    const char *description = selected == CaseIndexes::CycleTheme
                                  ? strings::tr("Changes the app's colors. Amber is the default.", "Cambia los colores de la app. Ámbar es el de por defecto.")
                                  : strings::get_by_name(strings::names::SETTINGS_DESCRIPTIONS, selected);
    if (!description) { return; }

    MessageState::create_push_fade(description);
}

void SettingsState::cycle_zip_level()
{
    uint8_t zipLevel = config::get_by_key(config::keys::ZIP_COMPRESSION_LEVEL);
    if (++zipLevel % 10 == 0) { zipLevel = 0; }

    config::set_by_key(config::keys::ZIP_COMPRESSION_LEVEL, zipLevel);
}

void SettingsState::cycle_sort_type()
{
    uint8_t sortType = config::get_by_key(config::keys::TITLE_SORT_TYPE);
    if (++sortType % 3 == 0) { sortType = 0; }
    config::set_by_key(config::keys::TITLE_SORT_TYPE, sortType);

    data::UserList users{};
    data::get_users(users);
    for (data::User *user : users) { user->sort_data(); }

    MainMenuState::refresh_view_states();
}

void SettingsState::toggle_jksm_mode()
{
    config::toggle_by_key(config::keys::JKSM_TEXT_MODE);

    MainMenuState::initialize_view_states();
}

void SettingsState::toggle_trash_folder()
{
    const bool trashEnabled = config::get_by_key(config::keys::ENABLE_TRASH_BIN);
    const fslib::Path trashPath{config::get_working_directory() / "_TRASH_"};
    config::toggle_by_key(config::keys::ENABLE_TRASH_BIN);

    if (trashEnabled) { error::fslib(fslib::delete_directory_recursively(trashPath)); }
    else { error::fslib(fslib::create_directory(trashPath)); }
}

void SettingsState::cycle_anim_scaling()
{
    double scaling = config::get_animation_scaling();
    if ((scaling += 0.25f) > 4.0f) { scaling = 1.0f; }

    config::set_animation_scaling(scaling);
    ui::Transition::update_scaling();
}

const char *SettingsState::get_status_text(uint8_t value)
{
    if (value > 1) { return nullptr; }

    return m_onOff[value];
}

const char *SettingsState::get_sort_type_text(uint8_t value)
{
    if (value > 2) { return nullptr; }

    return m_sortTypes[value];
}
