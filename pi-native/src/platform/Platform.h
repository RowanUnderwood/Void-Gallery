#pragma once
// OS-specific bits: where settings/caches live and the Windows screensaver plumbing.

#include <string>

namespace it::platform {

std::string configDir();  // Linux: $XDG_CONFIG_HOME|~/.config/imagetunnel   Windows: %APPDATA%\ImageTunnel
std::string cacheDir();   // Linux: $XDG_CACHE_HOME|~/.cache/imagetunnel     Windows: %LOCALAPPDATA%\ImageTunnel\cache

// Windows screensaver helpers (no-ops / false elsewhere). Handles are HWNDs as void*.
void* createPreviewChild(void* parentHwnd);  // child window filling the control-panel preview
bool windowAlive(void* hwnd);
bool acquireSingleInstance();                // false if another /s instance is already running
void showError(const std::string& title, const std::string& message);

}  // namespace it::platform
