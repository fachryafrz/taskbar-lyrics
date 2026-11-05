#include "taskbar_lyrics/app/Application.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#if defined(_MSC_VER)
#pragma comment(linker, "/manifestdependency:\"type='win32' " \
                        "name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' " \
                        "processorArchitecture='*' " \
                        "publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX icex{.dwSize = sizeof(INITCOMMONCONTROLSEX),
                              .dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&icex);

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const int result = taskbar_lyrics::app::Application{}.Run(instance);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return result;
}
