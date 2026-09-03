// MSAA accessibility check for the configuration dialog.
//
//   a11y_dump <LucentConfig.exe>
//
// Launches the utility, waits for its window, walks the tab order with GetNextDlgTabItem
// and prints each focusable control's MSAA name / role / value.  Exits 1 if any focusable
// control has no accessible name or if an access key is used twice.
#include <windows.h>
#include <oleacc.h>
#include <cstdio>
#include <string>
#include <map>

#pragma comment(lib, "oleacc.lib")

static std::wstring roleName(DWORD role) {
    wchar_t buf[128];
    GetRoleTextW(role, buf, 128);
    return buf;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { fwprintf(stderr, L"usage: a11y_dump <LucentConfig.exe>\n"); return 2; }
    CoInitialize(nullptr);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = std::wstring(L"\"") + argv[1] + L"\"";
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        fwprintf(stderr, L"cannot start %s\n", argv[1]);
        return 1;
    }
    HWND dlg = nullptr;
    for (int i = 0; i < 100 && !dlg; ++i) {
        Sleep(100);
        dlg = FindWindowW(nullptr, L"Lucent TTS Configuration");
    }
    if (!dlg) { fwprintf(stderr, L"dialog not found\n"); TerminateProcess(pi.hProcess, 1); return 1; }
    Sleep(300);
    int failures = 0;
    std::map<wchar_t, std::wstring> accessKeys;
    HWND first = GetNextDlgTabItem(dlg, nullptr, FALSE);
    HWND h = first;
    int n = 0;
    do {
        IAccessible* acc = nullptr;
        VARIANT self; self.vt = VT_I4; self.lVal = CHILDID_SELF;
        std::wstring name, role, value;
        if (SUCCEEDED(AccessibleObjectFromWindow(h, OBJID_CLIENT, IID_IAccessible, reinterpret_cast<void**>(&acc))) && acc) {
            BSTR b = nullptr;
            if (SUCCEEDED(acc->get_accName(self, &b)) && b) { name = b; SysFreeString(b); }
            VARIANT r; VariantInit(&r);
            if (SUCCEEDED(acc->get_accRole(self, &r)) && r.vt == VT_I4) role = roleName(r.lVal);
            b = nullptr;
            if (SUCCEEDED(acc->get_accValue(self, &b)) && b) { value = b; SysFreeString(b); }
            acc->Release();
        }
        wchar_t cls[64];
        GetClassNameW(h, cls, 64);
        wprintf(L"%2d. [%-16s] name=\"%s\" role=%s value=\"%s\"\n", ++n, cls, name.c_str(), role.c_str(), value.c_str());
        if (name.empty()) { wprintf(L"    FAIL: no accessible name\n"); failures++; }
        // access key check from the window text of the control or its label
        wchar_t text[256] = L"";
        GetWindowTextW(h, text, 256);
        std::wstring t = text;
        if (t.empty()) t = name;
        for (size_t i = 0; i + 1 < t.size(); ++i) {
            if (t[i] == L'&' && t[i + 1] != L'&') {
                wchar_t k = towlower(t[i + 1]);
                if (accessKeys.count(k)) { wprintf(L"    FAIL: access key '%c' also used by \"%s\"\n", k, accessKeys[k].c_str()); failures++; }
                else accessKeys[k] = t;
                break;
            }
        }
        h = GetNextDlgTabItem(dlg, h, FALSE);
    } while (h && h != first && n < 100);
    wprintf(L"%d controls in tab order, %d failure(s)\n", n, failures);
    PostMessageW(dlg, WM_CLOSE, 0, 0);
    WaitForSingleObject(pi.hProcess, 3000);
    TerminateProcess(pi.hProcess, 0);
    CoUninitialize();
    return failures ? 1 : 0;
}
