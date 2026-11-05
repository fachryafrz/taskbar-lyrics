#include "taskbar_lyrics/app/Application.h"

#include <windows.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const int result = taskbar_lyrics::app::Application{}.Run(instance);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return result;
}
