#pragma once
#include "appstates/MainMenuState.hpp"
#include "sys/sys.hpp"

namespace tasks::mainmenu
{
    void backup_all_for_all_local(sys::threadpool::JobData taskData);
    void backup_all_for_all_remote(sys::threadpool::JobData taskData);

    /// @brief Uploads a fresh backup of every favorited (heart) game's current save to the remote.
    void upload_favorites_remote(sys::threadpool::JobData taskData);
}
