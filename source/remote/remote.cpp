#include <atomic>
#include "remote/remote.hpp"

#include "StateManager.hpp"
#include "appstates/TaskState.hpp"
#include "error.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "remote/GoogleDrive.hpp"
#include "remote/WebDav.hpp"
#include "strings/strings.hpp"
#include "ui/PopMessageManager.hpp"

#include <chrono>
#include <ctime>
#include <memory>
#include <thread>

namespace
{
    /// @brief This is just the string for finding and creating the JKSV dir.
    constexpr const char *STRING_JKSV_DIR = "JKSV";

    /// @brief This is the single (for now) instance of a storage class.
    std::unique_ptr<remote::Storage> s_storage{};

    // clang-format off
    struct DriveStruct : sys::Task::DataStruct
    {
        remote::GoogleDrive *drive{};
    };
    // clang-format on
} // namespace

static void initialize_google_drive();
static void initialize_webdav();

// Declarations here. Definitions at bottom.
/// @brief This is the thread function that handles logging into Google.
static void drive_sign_in(sys::threadpool::JobData taskData);

/// @brief This creates (if needed) the JKSV folder for Google Drive and sets it as the root.
/// @param drive Pointer to the drive instance.
static void drive_set_jksv_root(remote::GoogleDrive *drive);

bool remote::has_internet_connection() noexcept
{
    NifmInternetConnectionType type{};
    uint32_t strength{};
    NifmInternetConnectionStatus status{};
    const bool getError = error::libnx(nifmGetInternetConnectionStatus(&type, &strength, &status));
    if (getError || status != NifmInternetConnectionStatus_Connected) { return false; }
    return true;
}

void remote::initialize(sys::threadpool::JobData jobData)
{
    const bool driveExists  = fslib::file_exists(remote::PATH_GOOGLE_DRIVE_CONFIG);
    const bool webdavExists = fslib::file_exists(remote::PATH_WEBDAV_CONFIG);
    if ((driveExists || webdavExists) && !remote::has_internet_connection())
    {
        const char *popNoInternet = strings::get_by_name(strings::names::REMOTE_POPS, 0);
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, popNoInternet);
        return;
    }

    if (driveExists) { initialize_google_drive(); }
    else if (webdavExists) { initialize_webdav(); }
}

void initialize_google_drive()
{
    const int popTicks = ui::PopMessageManager::DEFAULT_TICKS;

    s_storage                  = std::make_unique<remote::GoogleDrive>();
    remote::GoogleDrive *drive = static_cast<remote::GoogleDrive *>(s_storage.get());
    if (drive->sign_in_required())
    {
        auto driveStruct   = std::make_shared<DriveStruct>();
        driveStruct->drive = drive;

        // To do: StateManager isn't thread safe. This might/probably will cause data race randomly.
        TaskState::create_push_fade(drive_sign_in, driveStruct);
        return;
    }

    // To do: Handle this better. Maybe retry somehow?
    if (!drive->is_initialized()) { return; }

    drive_set_jksv_root(drive);
    const char *popDriveSuccess = strings::get_by_name(strings::names::GOOGLE_DRIVE, 1);
    ui::PopMessageManager::push_message(popTicks, popDriveSuccess);
}

void initialize_webdav()
{
    s_storage          = std::make_unique<remote::WebDav>();
    const int popTicks = ui::PopMessageManager::DEFAULT_TICKS;
    if (s_storage->is_initialized())
    {
        const char *popDavSuccess = strings::get_by_name(strings::names::WEBDAV, 0);
        ui::PopMessageManager::push_message(popTicks, popDavSuccess);
    }
    else
    {
        const std::string motivo = remote::motivo_webdav();
        const char *popDavFailed = strings::get_by_name(strings::names::WEBDAV, 1);
        ui::PopMessageManager::push_message(popTicks, motivo.empty() ? std::string(popDavFailed) : motivo);
    }
}

remote::Storage *remote::get_remote_storage() noexcept
{
    if (!s_storage || !s_storage->is_initialized()) { return nullptr; }
    return s_storage.get();
}

static void drive_sign_in(sys::threadpool::JobData taskData)
{
    static constexpr const char *STRING_ERROR_SIGNING_IN = "Error signing into Google Drive: %s";

    auto castData = std::static_pointer_cast<DriveStruct>(taskData);

    sys::Task *task            = castData->task;
    remote::GoogleDrive *drive = castData->drive;

    const int popTicks = ui::PopMessageManager::DEFAULT_TICKS;
    std::string message{}, deviceCode{};
    std::time_t expiration{};
    int pollingInterval{};
    if (!drive->get_sign_in_data(message, deviceCode, expiration, pollingInterval))
    {
        logger::log(STRING_ERROR_SIGNING_IN, "Getting sign in data failed!");
        TASK_FINISH_RETURN(task);
    }

    task->set_status(message);

    while (std::time(NULL) < expiration && !drive->poll_sign_in(deviceCode))
    {
        const bool bPressed = input::button_pressed(HidNpadButton_B);
        const bool bHeld    = input::button_held(HidNpadButton_B);
        if (bPressed || bHeld) { break; }

        std::this_thread::sleep_for(std::chrono::seconds(pollingInterval));
    }

    if (drive->is_initialized())
    {
        drive_set_jksv_root(drive);
        const char *popDriveSuccess = strings::get_by_name(strings::names::GOOGLE_DRIVE, 1);
        ui::PopMessageManager::push_message(popTicks, popDriveSuccess);
    }
    else
    {
        const char *popDriveFailed = strings::get_by_name(strings::names::GOOGLE_DRIVE, 2);
        ui::PopMessageManager::push_message(popTicks, popDriveFailed);
    }

    task->complete();
}

static void drive_set_jksv_root(remote::GoogleDrive *drive)
{
    const bool jksvExists  = drive->directory_exists(STRING_JKSV_DIR);
    const bool jksvCreated = !jksvExists && drive->create_directory(STRING_JKSV_DIR);
    if (!jksvExists && !jksvCreated) { return; }

    const remote::Item *jksvDir = drive->get_directory_by_name(STRING_JKSV_DIR);
    if (!jksvDir) { return; }

    drive->set_root_directory(jksvDir);
    drive->change_directory(jksvDir);
}

namespace
{
    std::atomic<long> s_codigoWebDav{-1};
}

long remote::ultimo_codigo_webdav() noexcept { return s_codigoWebDav.load(); }
void remote::anotar_codigo_webdav(long codigo) noexcept { s_codigoWebDav = codigo; }

std::string remote::motivo_webdav()
{
    // Errores con la causa probable y cuando descartarla, nunca con certeza.
    const long c = s_codigoWebDav.load();
    if (c == 401 || c == 403)
    {
        return strings::tr("Your cloud rejected the username or password. Check that the username is your account email and that the password is the one generated for apps in your cloud's settings (in Koofr: Preferences › Password › App passwords), not your account password.",
                           "Tu nube rechazó el usuario o la clave. Revisa que el usuario sea el correo de tu cuenta y que la clave sea la generada para aplicaciones en los ajustes de tu nube (en Koofr: Preferencias › Contraseña › Contraseñas de aplicaciones), no la clave de tu cuenta.");
    }
    if (c == 404 || c == 409)
    {
        return strings::tr("Your cloud says that address or folder doesn't exist. Most likely the address or the folder is mistyped; rule it out if it matches what your service shows for WebDAV.",
                           "Tu nube dice que esa dirección o carpeta no existe. Lo más probable es que la dirección o la carpeta estén mal escritas; descártalo si coincide con lo que tu servicio muestra para WebDAV.");
    }
    if (c == 0)
    {
        return strings::tr("Your cloud didn't answer. Most likely the address is mistyped or the service is down; rule it out if the site opens on another device.",
                           "Tu nube no respondió. Lo más probable es que la dirección esté mal escrita o que el servicio esté caído; descártalo si la página abre en otro equipo.");
    }
    if (c >= 500)
    {
        return strings::tr("Your cloud had an internal error. Most likely it's temporary on their side; try again in a few minutes.",
                           "Tu nube tuvo un error interno. Lo más probable es que sea algo pasajero de su lado; prueba de nuevo en unos minutos.");
    }
    return {};
}
