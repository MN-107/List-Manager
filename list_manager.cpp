#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

// ---------- control IDs ----------
enum {
    ID_MAIN_SAVE = 1009,
    ID_MAIN_LOAD = 1010,
    ID_FILELIST  = 1011,
    ID_REFRESH   = 1012,
    ID_OPEN_BTN  = 1013,
    ID_CLOSE_BTN = 1014,
    ID_MAIN_TXT  = 1023,
    ID_ADD_LIST  = 1024,
    ID_DEL_LIST  = 1025,
    ID_PAGE_PREV = 1026,
    ID_PAGE_NEXT = 1027,
    ID_OK         = 3001,
    ID_CANCEL    = 3002,
    ID_INPUT_TEXT = 3003
};

// dynamic per-panel controls: PANEL_BASE + index*10 + kind
const int PANEL_BASE = 2000;
const int PK_ADD = 0, PK_EDIT = 1, PK_DEL = 2, PK_RENAME = 3,
          PK_TAG = 4, PK_UP = 5, PK_DOWN = 6, PK_LIST = 7;
const int PANEL_KINDS = 10;
const int PANEL_CTRLS = 9;      // group, list, add, edit, del, rename, tag, up, down

static HWND gMain = NULL;
static HWND gFilesWnd = NULL;
static HWND gView = NULL;   // viewport: clips the panels against the toolbars
static HWND hFileList = NULL;
static HWND hPagePrev = NULL, hPageNext = NULL, hPageLabel = NULL;
static HWND hInputDialog = NULL, hInputEdit = NULL;
static BOOL gInputFinished = FALSE;
static BOOL gInputOk = FALSE;
static std::wstring gInputText;
static std::wstring gSavesDir;

// ---------- data model ----------
struct Tag {
    std::wstring name;
    std::vector<int> indices;
};
struct ListStore {
    std::vector<std::wstring> items;
    std::vector<Tag> tags;
    std::wstring title;      // empty until the user names the sheet
};
struct Panel {
    ListStore store;
    int idBase = 0;
    HWND group = NULL, list = NULL, btnAdd = NULL;
    HWND btnEdit = NULL, btnDel = NULL, btnRename = NULL, btnTag = NULL;
    HWND btnUp = NULL, btnDown = NULL;
};
static std::vector<Panel> gPanels;
static int gPage = 0;                 // one page shows two sheets
static HFONT gFont = NULL;

static void SetChildFont(HWND parent, HFONT font);
static void UpdateLayout();
static void RefreshButtons();

// ---------- dpi ----------
static int g_dpi = 96;
typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
typedef UINT (WINAPI *GetDpiForSystemFn)(void);
static GetDpiForWindowFn pGetDpiForWindow = NULL;
static GetDpiForSystemFn pGetDpiForSystem = NULL;

// design units are 96dpi pixels; every on-screen coordinate goes through S()
static int S(int v) { return MulDiv(v, g_dpi, 96); }

static void InitDpiApi() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) u32 = LoadLibraryW(L"user32.dll");
    if (!u32) return;
    pGetDpiForWindow = (GetDpiForWindowFn)GetProcAddress(u32, "GetDpiForWindow");
    pGetDpiForSystem = (GetDpiForSystemFn)GetProcAddress(u32, "GetDpiForSystem");
}

static void EnableDpiAwareness() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) u32 = LoadLibraryW(L"user32.dll");
    if (!u32) return;
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    SetCtxFn setCtx = (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (setCtx) {
        if (setCtx((HANDLE)(INT_PTR)-4)) return;   // PER_MONITOR_AWARE_V2
        if (setCtx((HANDLE)(INT_PTR)-3)) return;   // PER_MONITOR_AWARE
    }
    typedef BOOL (WINAPI *SetAwareFn)(void);
    SetAwareFn setAware = (SetAwareFn)GetProcAddress(u32, "SetProcessDPIAware");
    if (setAware) setAware();
}

static int SystemDpi() {
    if (pGetDpiForSystem) {
        UINT d = pGetDpiForSystem();
        if (d) return (int)d;
    }
    HDC dc = GetDC(NULL);
    int d = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return d > 0 ? d : 96;
}

static int WindowDpi(HWND hwnd) {
    if (pGetDpiForWindow) {
        UINT d = pGetDpiForWindow(hwnd);
        if (d) return (int)d;
    }
    return SystemDpi();
}

// system message font, rasterized for the current dpi (DEFAULT_GUI_FONT is a
// legacy bitmap font: tiny and blurry once windows are dpi aware)
static void CreateAppFont() {
    HFONT f = NULL;
    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        LOGFONTW lf = ncm.lfMessageFont;
        int sdpi = SystemDpi();
        if (sdpi > 0 && sdpi != g_dpi) {
            lf.lfHeight = MulDiv(lf.lfHeight, g_dpi, sdpi);
            lf.lfWidth  = MulDiv(lf.lfWidth,  g_dpi, sdpi);
        }
        f = CreateFontIndirectW(&lf);
    }
    if (!f) f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HFONT old = gFont;
    gFont = f;
    if (gMain) SetChildFont(gMain, gFont);
    if (gView) SetChildFont(gView, gFont);
    if (gFilesWnd) SetChildFont(gFilesWnd, gFont);
    if (old && old != f && old != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(old);
}

// ---------- UTF-8 helpers ----------
static std::string ToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), NULL, 0, NULL, NULL);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], n, NULL, NULL);
    return s;
}

static std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return ws;
}

// ---------- input dialog ----------
static LRESULT CALLBACK InputDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = GetModuleHandleW(NULL);
        hInputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            S(10), S(14), S(304), S(24), hwnd, (HMENU)(INT_PTR)ID_INPUT_TEXT, hInst, NULL);
        CreateWindowW(L"BUTTON", L"ok",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            S(160), S(54), S(80), S(28), hwnd, (HMENU)(INT_PTR)ID_OK, hInst, NULL);
        CreateWindowW(L"BUTTON", L"cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            S(250), S(54), S(80), S(28), hwnd, (HMENU)(INT_PTR)ID_CANCEL, hInst, NULL);
        SetChildFont(hwnd, gFont ? gFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_OK: {
            int len = GetWindowTextLengthW(hInputEdit);
            std::vector<wchar_t> buf(len + 1);
            GetWindowTextW(hInputEdit, buf.data(), len + 1);
            std::wstring s(buf.data());
            while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' ||
                                  s.back() == L' ' || s.back() == L'\t'))
                s.pop_back();
            if (!s.empty()) {
                gInputText = s;
                gInputOk = TRUE;
                gInputFinished = TRUE;
            } else {
                MessageBeep(MB_ICONWARNING);
            }
            break;
        }
        case ID_CANCEL:
            gInputFinished = TRUE;
            break;
        }
        return 0;
    case WM_CLOSE:
        gInputFinished = TRUE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowInputDialog(HWND owner, const wchar_t* title, const std::wstring& initial) {
    gInputFinished = FALSE;
    gInputOk = FALSE;
    gInputText.clear();

    HINSTANCE hInst = GetModuleHandleW(NULL);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT dr = { 0, 0, S(340), S(104) };
    AdjustWindowRectEx(&dr, style, FALSE, WS_EX_DLGMODALFRAME);
    int dw = dr.right - dr.left, dh = dr.bottom - dr.top;

    RECT rc;
    GetWindowRect(owner, &rc);
    int x = rc.left + ((rc.right - rc.left) - dw) / 2;
    int y = rc.top + ((rc.bottom - rc.top) - dh) / 2;

    HWND prevFocus = GetFocus();
    // owner window => stays above it and gets no taskbar button of its own
    hInputDialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ListAppInput", title,
        style, x, y, dw, dh, owner, NULL, hInst, NULL);
    if (!hInputDialog) return false;

    BOOL ownerWasEnabled = IsWindowEnabled(owner);
    EnableWindow(owner, FALSE);
    SetWindowTextW(hInputEdit, initial.c_str());
    SendMessageW(hInputEdit, EM_SETSEL, 0, -1);
    ShowWindow(hInputDialog, SW_SHOWNORMAL);
    UpdateWindow(hInputDialog);
    SetFocus(hInputEdit);
    SendMessageW(hInputEdit, EM_SETSEL, 0, -1);

    MSG msg;
    while (!gInputFinished) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { PostQuitMessage(0); break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, QS_ALLINPUT, 50);
    }

    if (IsWindow(hInputDialog)) DestroyWindow(hInputDialog);
    hInputDialog = NULL;
    hInputEdit = NULL;
    if (IsWindow(owner)) {
        if (ownerWasEnabled) EnableWindow(owner, TRUE);
        if (IsWindowVisible(owner)) {
            SetActiveWindow(owner);
            if (IsWindow(prevFocus) && GetWindowThreadProcessId(prevFocus, NULL) ==
                                      GetWindowThreadProcessId(owner, NULL))
                SetFocus(prevFocus);
            else
                SetFocus(owner);
        }
    }
    return gInputOk == TRUE;
}

// ---------- list helpers ----------
static int CurSel(HWND list) {
    return (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
}

static const int PAGE_SHEETS = 2;

static int PageCount() {
    int n = (int)((gPanels.size() + PAGE_SHEETS - 1) / PAGE_SHEETS);
    return n > 0 ? n : 1;
}

static int PageOfIndex(size_t index) {
    return (int)(index / PAGE_SHEETS);
}

static int PanelIndex(const Panel& p) {
    for (size_t i = 0; i < gPanels.size(); i++)
        if (&gPanels[i] == &p) return (int)i;
    return -1;
}

// group, list, add, edit, del, rename, tag, up, down
static void PanelControls(const Panel& p, HWND* out) {
    out[0] = p.group;   out[1] = p.list;     out[2] = p.btnAdd;
    out[3] = p.btnEdit; out[4] = p.btnDel;   out[5] = p.btnRename;
    out[6] = p.btnTag;  out[7] = p.btnUp;    out[8] = p.btnDown;
}

static Panel* PanelFromCtrl(HWND ctrl) {
    if (!ctrl) return NULL;
    HWND hs[PANEL_CTRLS];
    for (size_t i = 0; i < gPanels.size(); i++) {
        PanelControls(gPanels[i], hs);
        for (int k = 0; k < PANEL_CTRLS; k++)
            if (hs[k] == ctrl) return &gPanels[i];
    }
    return NULL;
}

// a sheet the user never named still needs a caption; the placeholder is not
// a name, so such a sheet counts as unnamed
static std::wstring PanelCaption(const Panel& p, int index) {
    if (!p.store.title.empty()) return p.store.title;
    return L"list" + std::to_wstring(index + 1);
}

static void ApplyCaption(Panel& p, int index) {
    std::wstring cap = PanelCaption(p, index);
    if (p.group) SetWindowTextW(p.group, cap.c_str());
}

// only a named or a non-empty sheet is worth writing to disk
static bool SheetWorthSaving(const Panel& p) {
    return !p.store.title.empty() || !p.store.items.empty() || !p.store.tags.empty();
}

static void RefreshPageNav() {
    int pages = PageCount();
    if (gPage > pages - 1) gPage = pages - 1;
    if (gPage < 0) gPage = 0;
    if (hPagePrev) EnableWindow(hPagePrev, gPage > 0);
    if (hPageNext) EnableWindow(hPageNext, gPage < pages - 1);
    if (hPageLabel) {
        std::wstring s = std::to_wstring(gPage + 1) + L" / " + std::to_wstring(pages);
        SetWindowTextW(hPageLabel, s.c_str());
    }
}

// the sheet that had the keyboard focus may have just been hidden, so the
// focus is moved to the first sheet of the new page
static void FocusPageSheet() {
    int first = gPage * PAGE_SHEETS;
    if (first >= 0 && first < (int)gPanels.size()) SetFocus(gPanels[first].list);
    else if (gView) SetFocus(gView);
}

static void SetPage(int page) {
    if (page == gPage) { RefreshPageNav(); return; }
    gPage = page;
    UpdateLayout();
    RefreshButtons();
    FocusPageSheet();
}

static void RefreshButtons() {
    for (size_t i = 0; i < gPanels.size(); i++) {
        Panel& p = gPanels[i];
        if (!p.list) continue;
        int sel = CurSel(p.list);
        int cnt = (int)SendMessageW(p.list, LB_GETCOUNT, 0, 0);
        EnableWindow(p.btnAdd, TRUE);
        EnableWindow(p.btnRename, TRUE);
        EnableWindow(p.btnEdit, sel >= 0);
        EnableWindow(p.btnDel, sel >= 0);
        EnableWindow(p.btnTag, sel >= 0);
        EnableWindow(p.btnUp, sel > 0);
        EnableWindow(p.btnDown, sel >= 0 && sel < cnt - 1);
    }
    RefreshPageNav();
}

static std::wstring TagSuffix(const ListStore& st, int index) {
    std::wstring s;
    bool first = true;
    for (const auto& t : st.tags) {
        if (std::binary_search(t.indices.begin(), t.indices.end(), index)) {
            if (!first) s += L", ";
            s += t.name;
            first = false;
        }
    }
    return s.empty() ? L"" : L" (" + s + L")";
}

static std::vector<int> ParseIndexList(const std::wstring& s, size_t limit) {
    std::vector<int> out;
    std::wstring cur;
    auto addRange = [&](long long lo, long long hi) {
        long long from = std::max(0LL, std::min(lo, hi));
        long long to = std::min(std::max(lo, hi), (long long)limit - 1);
        if (to > 100000) to = 100000;
        for (long long x = from; x <= to; x++) out.push_back((int)x);
    };
    auto commit = [&]() {
        if (cur.empty()) return;
        size_t dash = cur.find(L'-');
        long long va = 0, vb = 0;
        bool oka = true, okb = true;
        auto parse = [](const std::wstring& t, long long& v) -> bool {
            if (t.empty()) return false;
            long long x = 0;
            for (size_t i = 0; i < t.size(); i++) {
                int d = t[i] - L'0';
                if (d < 0 || d > 9) return false;
                x = x * 10 + d;
            }
            v = x;
            return true;
        };
        if (dash == std::wstring::npos) {
            oka = parse(cur, va);
        } else {
            oka = parse(cur.substr(0, dash), va);
            okb = dash + 1 < cur.size() && parse(cur.substr(dash + 1), vb);
            if (!okb) oka = false;
        }
        if (oka) {
            if (dash == std::wstring::npos) {
                if (va >= 0 && va < (int)limit) out.push_back((int)va);
            } else {
                addRange(va, vb);
            }
        }
        cur.clear();
    };
    for (wchar_t c : s) {
        if (c == L',' || c == L' ' || c == L'\t') commit();
        else cur += c;
    }
    commit();
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// repaints the whole list; keeps the current selection and scroll offset so
// that the highlight does not disappear under the user's cursor
static void RebuildList(Panel& p) {
    if (!p.list) return;
    int sel = CurSel(p.list);
    int top = (int)SendMessageW(p.list, LB_GETTOPINDEX, 0, 0);
    SendMessageW(p.list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < p.store.items.size(); i++)
        SendMessageW(p.list, LB_ADDSTRING, 0,
            (LPARAM)(p.store.items[i] + TagSuffix(p.store, (int)i)).c_str());
    int cnt = (int)SendMessageW(p.list, LB_GETCOUNT, 0, 0);
    if (cnt > 0) {
        if (sel >= cnt) sel = cnt - 1;
        if (sel < 0) sel = 0;
        SendMessageW(p.list, LB_SETCURSEL, sel, 0);
        if (top >= 0) SendMessageW(p.list, LB_SETTOPINDEX, top, 0);
    }
    RefreshButtons();
}

static void OnAdd(Panel& p) {
    if (ShowInputDialog(gMain, L"add", L"")) {
        p.store.items.push_back(gInputText);
        RebuildList(p);
        SendMessageW(p.list, LB_SETCURSEL, (int)p.store.items.size() - 1, 0);
        RefreshButtons();
    }
}

static void OnEdit(Panel& p) {
    int i = CurSel(p.list);
    if (i < 0) return;
    if (ShowInputDialog(gMain, L"edit", p.store.items[i])) {
        p.store.items[i] = gInputText;
        RebuildList(p);
    }
}

static void OnDelete(Panel& p) {
    int i = CurSel(p.list);
    if (i < 0) return;
    p.store.items.erase(p.store.items.begin() + i);
    for (auto& t : p.store.tags) {
        std::vector<int> ni;
        for (int idx : t.indices) {
            if (idx < i) ni.push_back(idx);
            else if (idx > i) ni.push_back(idx - 1);
        }
        t.indices.swap(ni);
    }
    RebuildList(p);
    if (!p.store.items.empty()) {
        int sel = i;
        if (sel >= (int)p.store.items.size()) sel = (int)p.store.items.size() - 1;
        SendMessageW(p.list, LB_SETCURSEL, sel, 0);
    }
    RefreshButtons();
}

static std::wstring TrimTagName(const std::wstring& in) {
    size_t a = in.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return L"";
    size_t b = in.find_last_not_of(L" \t");
    return in.substr(a, b - a + 1);
}

static void SwapRows(ListStore& st, int i, int j) {
    std::swap(st.items[i], st.items[j]);
    for (auto& t : st.tags) {
        for (auto& idx : t.indices) {
            if (idx == i) idx = j;
            else if (idx == j) idx = i;
        }
        std::sort(t.indices.begin(), t.indices.end());
    }
}

static void OnMove(Panel& p, int dir) {
    int i = CurSel(p.list);
    if (i < 0) return;
    int j = i + dir;
    if (j < 0 || j >= (int)p.store.items.size()) { MessageBeep(MB_ICONWARNING); return; }
    SwapRows(p.store, i, j);
    RebuildList(p);
    SendMessageW(p.list, LB_SETCURSEL, j, 0);
    RefreshButtons();
}

static void OnTag(Panel& p) {
    int sel = CurSel(p.list);
    if (sel < 0) { MessageBeep(MB_ICONWARNING); return; }
    if (!ShowInputDialog(gMain, L"tag name", L"")) return;
    std::wstring name = TrimTagName(gInputText);
    if (name.empty()) return;

    for (auto it = p.store.tags.begin(); it != p.store.tags.end(); ++it) {
        if (it->name == name) {
            auto pos = std::find(it->indices.begin(), it->indices.end(), sel);
            if (pos != it->indices.end()) {
                it->indices.erase(pos);
                if (it->indices.empty()) p.store.tags.erase(it);
            } else {
                it->indices.push_back(sel);
                std::sort(it->indices.begin(), it->indices.end());
            }
            RebuildList(p);
            return;
        }
    }
    Tag t;
    t.name = name;
    t.indices.push_back(sel);
    p.store.tags.push_back(t);
    RebuildList(p);
}

static void OnRename(Panel& p) {
    int i = PanelIndex(p);
    if (i < 0) return;
    if (ShowInputDialog(gMain, L"list name", PanelCaption(p, i))) {
        p.store.title = gInputText;
        ApplyCaption(p, i);
    }
}

static void WriteList(std::ofstream& f, const ListStore& st) {
    for (const auto& it : st.items)
        f << ToUtf8(it) << "\n";
}

static void WriteTags(std::ostream& f, const ListStore& st) {
    for (const auto& t : st.tags) {
        f << ToUtf8(t.name) << ":";
        for (size_t i = 0; i < t.indices.size(); ) {
            if (i) f << ",";
            size_t j = i;
            while (j + 1 < t.indices.size() && t.indices[j + 1] == t.indices[j] + 1) j++;
            if (j > i)
                f << t.indices[i] << "-" << t.indices[j];
            else
                f << t.indices[i];
            i = j + 1;
        }
        f << "\n";
    }
}

// ---------- save / load ----------
static std::wstring GetSavesDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring exe(buf);
    size_t pos = exe.find_last_of(L"\\/");
    std::wstring dir = exe.substr(0, pos) + L"\\saves";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

static void RefreshFileList() {
    if (!hFileList) return;
    SendMessageW(hFileList, LB_RESETCONTENT, 0, 0);
    std::wstring pattern = gSavesDir + L"\\*.list";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        SendMessageW(hFileList, LB_ADDSTRING, 0, (LPARAM)fd.cFileName);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static void OnSave(HWND owner) {
    if (!ShowInputDialog(owner, L"file name for saving", L"")) return;

    std::wstring name = gInputText;
    for (auto& c : name)
        if (c == L'\\' || c == L'/' || c == L':') c = L'_';

    if (name.size() >= 5 &&
        name.compare(name.size() - 5, 5, L".list") == 0)
        name.resize(name.size() - 5);

    std::wstring path = gSavesDir + L"\\" + name + L".list";

    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        int r = MessageBoxW(owner, L"the file already exists. overwrite?",
            L"save", MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return;
    }

    std::ofstream f(ToUtf8(path), std::ios::binary);
    if (!f) {
        MessageBoxW(owner, L"failed to write the file.", L"error", MB_ICONERROR);
        return;
    }
    for (size_t i = 0; i < gPanels.size(); i++) {
        Panel& p = gPanels[i];
        if (!SheetWorthSaving(p)) continue;
        f << "[TITLE" << (i + 1) << "]\n";
        f << ToUtf8(PanelCaption(p, (int)i)) << "\n";
        f << "[LIST" << (i + 1) << "]\n";
        WriteList(f, p.store);
        if (!p.store.tags.empty()) {
            f << "[TAGS" << (i + 1) << "]\n";
            WriteTags(f, p.store);
        }
    }

    if (gFilesWnd) {
        RefreshFileList();
        int n = (int)SendMessageW(hFileList, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(name + L".list").c_str());
        if (n != LB_ERR) SendMessageW(hFileList, LB_SETCURSEL, n, 0);
    }
}

static void WriteReport(std::ostream& f, const std::wstring& title, const ListStore& st) {
    f << ToUtf8(title) << "\n";
    for (size_t i = 0; i < st.items.size(); i++)
        f << ToUtf8(st.items[i] + TagSuffix(st, (int)i)) << "\n";
}

static void OnSaveTxt(HWND owner) {
    if (!ShowInputDialog(owner, L"report file name", L"")) return;

    std::wstring name = gInputText;
    for (auto& c : name)
        if (c == L'\\' || c == L'/' || c == L':') c = L'_';

    if (name.size() >= 4 &&
        name.compare(name.size() - 4, 4, L".txt") == 0)
        name.resize(name.size() - 4);

    std::wstring path = gSavesDir + L"\\" + name + L".txt";

    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        int r = MessageBoxW(owner, L"the file already exists. overwrite?",
            L"save txt", MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return;
    }

    std::ofstream f(ToUtf8(path), std::ios::binary);
    if (!f) {
        MessageBoxW(owner, L"failed to write the file.", L"error", MB_ICONERROR);
        return;
    }
    bool first = true;
    for (size_t i = 0; i < gPanels.size(); i++) {
        Panel& p = gPanels[i];
        if (!SheetWorthSaving(p)) continue;
        if (!first) f << "\n";
        WriteReport(f, PanelCaption(p, (int)i), p.store);
        first = false;
    }
}

static void ParseTagLine(const std::string& line, std::vector<Tag>& out, size_t limit) {
    size_t p = line.find(':');
    std::wstring name = (p == std::string::npos) ? FromUtf8(line) : FromUtf8(line.substr(0, p));
    if (name.empty()) return;
    Tag t;
    t.name = name;
    if (p != std::string::npos)
        t.indices = ParseIndexList(FromUtf8(line.substr(p + 1)), limit);
    out.push_back(t);
}

static void ParseLists(const std::string& content, std::vector<ListStore>& lists) {
    lists.clear();
    std::string line;
    std::istringstream input(content);
    int cur = -1, mode = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && line.front() == '[' && line.back() == ']') {
            std::string name = line.substr(1, line.size() - 2);
            size_t d = 0;
            while (d < name.size() && (name[d] < '0' || name[d] > '9')) d++;
            int idx = -1;
            if (d < name.size()) {
                int num = 0;
                bool ok = true;
                for (size_t j = d; j < name.size(); j++) {
                    int c = name[j] - '0';
                    if (c < 0 || c > 9) { ok = false; break; }
                    num = num * 10 + c;
                }
                if (ok) idx = num - 1;
            }
            std::string pre = name.substr(0, d);
            mode = 0;
            if (idx >= 0 && pre == "LIST")  { mode = 1; }
            else if (idx >= 0 && pre == "TITLE") { mode = 2; }
            else if (idx >= 0 && pre == "TAGS")  { mode = 3; }
            else { cur = -1; continue; }
            cur = idx;
            while ((int)lists.size() <= idx) lists.push_back(ListStore());
            continue;
        }
        if (mode == 1 && cur >= 0 && cur < (int)lists.size())
            lists[cur].items.push_back(FromUtf8(line));
        else if (mode == 2 && cur >= 0 && cur < (int)lists.size())
            lists[cur].title = FromUtf8(line);
        else if (mode == 3 && cur >= 0 && cur < (int)lists.size())
            ParseTagLine(line, lists[cur].tags, lists[cur].items.size());
    }
}

static bool LoadFile(HWND owner, const std::wstring& path, std::vector<ListStore>& lists) {
    std::ifstream f(ToUtf8(path), std::ios::binary);
    if (!f) {
        MessageBoxW(owner, L"failed to open the file.", L"error", MB_ICONERROR);
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    ParseLists(ss.str(), lists);
    return true;
}

static void OpenFilesWindow();
static void AddPanel(HWND parent);
static void DestroyPanel(size_t index);

static void OnPickFile(HWND hwnd) {
    int i = CurSel(hFileList);
    if (i < 0) {
        MessageBoxW(hwnd, L"select a file from the list.", L"load", MB_ICONINFORMATION);
        return;
    }
    int len = (int)SendMessageW(hFileList, LB_GETTEXTLEN, i, 0);
    std::vector<wchar_t> buf(len + 1);
    SendMessageW(hFileList, LB_GETTEXT, i, (LPARAM)buf.data());

    std::vector<ListStore> loaded;
    if (LoadFile(hwnd, gSavesDir + L"\\" + buf.data(), loaded)) {
        if (loaded.empty()) {
            MessageBoxW(hwnd, L"no lists found in the file.", L"load", MB_ICONINFORMATION);
            return;
        }
        while (gPanels.size() > loaded.size()) DestroyPanel(gPanels.size() - 1);
        while (gPanels.size() < loaded.size()) AddPanel(gMain);
        for (size_t k = 0; k < gPanels.size(); k++) {
            Panel& p = gPanels[k];
            p.store = std::move(loaded[k]);
            ApplyCaption(p, (int)k);
            RebuildList(p);
        }
        gPage = 0;
        UpdateLayout();
        DestroyWindow(hwnd);
    }
}

// ---------- files window ----------
static LRESULT CALLBACK FilesWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = GetModuleHandleW(NULL);
        CreateWindowW(L"BUTTON", L"files (*.list) in the saves folder",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(8), S(6), S(424), S(316), hwnd, NULL, hInst, NULL);
        hFileList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            S(18), S(24), S(404), S(230), hwnd, (HMENU)(INT_PTR)ID_FILELIST, hInst, NULL);
        CreateWindowW(L"BUTTON", L"refresh",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(18), S(270), S(122), S(28),
            hwnd, (HMENU)(INT_PTR)ID_REFRESH, hInst, NULL);
        CreateWindowW(L"BUTTON", L"open",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(148), S(270), S(122), S(28),
            hwnd, (HMENU)(INT_PTR)ID_OPEN_BTN, hInst, NULL);
        CreateWindowW(L"BUTTON", L"close",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(278), S(270), S(144), S(28),
            hwnd, (HMENU)(INT_PTR)ID_CLOSE_BTN, hInst, NULL);
        SetChildFont(hwnd, gFont ? gFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        RefreshFileList();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = S(360);
        mmi->ptMinTrackSize.y = S(240);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_REFRESH:  RefreshFileList(); break;
        case ID_OPEN_BTN: OnPickFile(hwnd); break;
        case ID_CLOSE_BTN: DestroyWindow(hwnd); break;
        }
        if (HIWORD(wParam) == LBN_DBLCLK && LOWORD(wParam) == ID_FILELIST)
            OnPickFile(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        gFilesWnd = NULL;
        hFileList = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void OpenFilesWindow() {
    if (gFilesWnd && IsWindow(gFilesWnd)) {
        SetForegroundWindow(gFilesWnd);
        return;
    }
    HINSTANCE hInst = GetModuleHandleW(NULL);
    RECT rc = { 0, 0, S(440), S(330) };
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    // owned by the main window: no extra taskbar button, always above it
    gFilesWnd = CreateWindowW(L"ListAppFiles", L"load file",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        gMain, NULL, hInst, NULL);
    if (!gFilesWnd) return;
    ShowWindow(gFilesWnd, SW_SHOW);
    UpdateWindow(gFilesWnd);
}

// ---------- main window ----------
// All metrics below are design units (96dpi). Sheets are shown two at a time as
// a page inside gView (the viewport), which clips them to the area between the
// window edges and the tool bar, so no sheet can ever paint over the buttons.
static const int PANEL_DX = 10;      // left margin of a panel
static const int PANEL_DY = 8;       // top margin of a panel
static const int PANEL_W = 370;
static const int PANEL_H = 348;
static const int PANEL_COL_PITCH = 392;

static const int LIST_DX = 10, LIST_DY = 22, LIST_W = 235, LIST_H = 288;

static const int BTN_DX = 253, BTN_DY = 22, BTN_W = 108, BTN_H = 26, BTN_PITCH = 30;

static const int NAV_DY = 316, NAV_H = 24;
static const int NAV_DX1 = 10, NAV_W1 = 116, NAV_DX2 = 132, NAV_W2 = 113;

static const int PAGEBAR_H = 34;
static const int PAGE_BTN_W = 44, PAGE_BTN_H = 26, PAGE_LBL_W = 150, PAGE_GAP = 6;

static const int TOOL_H = 44;
static const int TOOL_BTN_W = 110, TOOL_BTN_H = 28;
static const int TOOL_DX = 8, TOOL_PITCH = 116, TOOL_BOT = 8;

// A page switch leaves the pixels of the sheets that just went away on screen: a
// group box only repaints its own frame and caption, so the leftovers of the old
// caption stay visible. Repaint the viewport and every child from scratch.
static void RepaintViewport() {
    if (!gView) return;
    LONG_PTR style = GetWindowLongPtrW(gView, GWL_STYLE);
    SetWindowLongPtrW(gView, GWL_STYLE, style & ~(LONG_PTR)WS_CLIPCHILDREN);
    RedrawWindow(gView, NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    SetWindowLongPtrW(gView, GWL_STYLE, style);
}

static void LayoutPageBar(const RECT& vr) {
    if (!hPagePrev || !hPageNext || !hPageLabel) return;
    int by = S(PAGEBAR_H) / 2 - S(PAGE_BTN_H) / 2;
    int total = S(PAGE_BTN_W) * 2 + S(PAGE_GAP) * 2 + S(PAGE_LBL_W);
    int x = (vr.right - total) / 2;
    if (x < S(2)) x = S(2);
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    SetWindowPos(hPagePrev, NULL, x, by, S(PAGE_BTN_W), S(PAGE_BTN_H), flags);
    SetWindowPos(hPageLabel, NULL, x + S(PAGE_BTN_W) + S(PAGE_GAP), by,
        S(PAGE_LBL_W), S(PAGE_BTN_H), flags);
    SetWindowPos(hPageNext, NULL, x + S(PAGE_BTN_W) + S(PAGE_GAP) + S(PAGE_LBL_W) + S(PAGE_GAP),
        by, S(PAGE_BTN_W), S(PAGE_BTN_H), flags);
}

static void LayoutPanels() {
    if (!gView) return;
    RECT vr;
    GetClientRect(gView, &vr);
    RefreshPageNav();
    LayoutPageBar(vr);
    if (gPanels.empty()) {
        RepaintViewport();
        return;
    }
    int first = gPage * PAGE_SHEETS;
    HDWP def = BeginDeferWindowPos((int)gPanels.size() * PANEL_CTRLS);
    if (!def) return;
    for (size_t i = 0; i < gPanels.size(); i++) {
        Panel& p = gPanels[i];
        int col = (int)i - first;
        bool onPage = col >= 0 && col < PAGE_SHEETS;
        int x = S(PANEL_DX) + col * S(PANEL_COL_PITCH);
        int y = S(PAGEBAR_H) + S(PANEL_DY);
        struct RS { int x, y, w, h; } rs[PANEL_CTRLS] = {
            { x, y, S(PANEL_W), S(PANEL_H) },
            { x + S(LIST_DX), y + S(LIST_DY), S(LIST_W), S(LIST_H) },
            { x + S(BTN_DX), y + S(BTN_DY + 0 * BTN_PITCH), S(BTN_W), S(BTN_H) },
            { x + S(BTN_DX), y + S(BTN_DY + 1 * BTN_PITCH), S(BTN_W), S(BTN_H) },
            { x + S(BTN_DX), y + S(BTN_DY + 2 * BTN_PITCH), S(BTN_W), S(BTN_H) },
            { x + S(BTN_DX), y + S(BTN_DY + 3 * BTN_PITCH), S(BTN_W), S(BTN_H) },
            { x + S(BTN_DX), y + S(BTN_DY + 4 * BTN_PITCH), S(BTN_W), S(BTN_H) },
            { x + S(NAV_DX1), y + S(NAV_DY), S(NAV_W1), S(NAV_H) },
            { x + S(NAV_DX2), y + S(NAV_DY), S(NAV_W2), S(NAV_H) },
        };
        HWND hs[PANEL_CTRLS];
        PanelControls(p, hs);
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE |
                     (onPage ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
        for (int k = 0; k < PANEL_CTRLS; k++)
            if (hs[k])
                def = DeferWindowPos(def, hs[k], NULL, rs[k].x, rs[k].y, rs[k].w, rs[k].h,
                    flags);
    }
    EndDeferWindowPos(def);
    // the slots changed: redraw everything they uncovered
    RepaintViewport();
}

static void LayoutMain(HWND hwnd) {
    if (!gView) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int viewH = rc.bottom - S(TOOL_H);
    if (viewH < 0) viewH = 0;
    int toolY = rc.bottom - S(TOOL_BTN_H) - S(TOOL_BOT);
    if (toolY < viewH) toolY = viewH;

    HDWP def = BeginDeferWindowPos(6);
    if (!def) return;
    def = DeferWindowPos(def, gView, NULL, 0, 0, rc.right, viewH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    const int ids[5] = { ID_MAIN_TXT, ID_MAIN_SAVE, ID_MAIN_LOAD, ID_ADD_LIST, ID_DEL_LIST };
    for (int k = 0; k < 5; k++) {
        HWND b = GetDlgItem(hwnd, ids[k]);
        if (!b) continue;
        def = DeferWindowPos(def, b, NULL, S(TOOL_DX + k * TOOL_PITCH), toolY,
            S(TOOL_BTN_W), S(TOOL_BTN_H), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    EndDeferWindowPos(def);
}

// A page holds two sheets, so nothing ever scrolls: this only has to keep the
// two slots, the page strip and the tool bar in sync.
static void UpdateLayout() {
    if (!gMain || !gView) return;
    LayoutMain(gMain);
    LayoutPanels();
}

// A new sheet is appended, so it either fills the free slot of the page the
// user is on (when that page is the last one) or opens a page of its own.
static void AddPanel(HWND parent) {
    size_t i = gPanels.size();
    // control ids are baked in at creation time, so a fresh sheet needs a base
    // no other sheet uses; deleting a sheet in the middle leaves a hole
    int base = PANEL_BASE;
    for (size_t k = 0; k < gPanels.size(); k++)
        base = std::max(base, gPanels[k].idBase + PANEL_KINDS);
    HINSTANCE hInst = GetModuleHandleW(NULL);
    HWND host = gView ? gView : parent;
    Panel p;
    p.idBase = base;
    std::wstring title = L"list" + std::to_wstring(i + 1);

    p.group = CreateWindowW(L"BUTTON", title.c_str(),
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, S(PANEL_W), S(PANEL_H),
        host, NULL, hInst, NULL);
    p.list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
        0, 0, S(LIST_W), S(LIST_H), host, (HMENU)(INT_PTR)(base + PK_LIST), hInst, NULL);
    p.btnAdd = CreateWindowW(L"BUTTON", L"add",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(BTN_W), S(BTN_H), host,
        (HMENU)(INT_PTR)(base + PK_ADD), hInst, NULL);
    p.btnEdit = CreateWindowW(L"BUTTON", L"edit",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(BTN_W), S(BTN_H), host,
        (HMENU)(INT_PTR)(base + PK_EDIT), hInst, NULL);
    p.btnDel = CreateWindowW(L"BUTTON", L"delete",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(BTN_W), S(BTN_H), host,
        (HMENU)(INT_PTR)(base + PK_DEL), hInst, NULL);
    p.btnRename = CreateWindowW(L"BUTTON", L"rename",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(BTN_W), S(BTN_H), host,
        (HMENU)(INT_PTR)(base + PK_RENAME), hInst, NULL);
    p.btnTag = CreateWindowW(L"BUTTON", L"tag",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(BTN_W), S(BTN_H), host,
        (HMENU)(INT_PTR)(base + PK_TAG), hInst, NULL);
    p.btnUp = CreateWindowW(L"BUTTON", L"up",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(NAV_W1), S(NAV_H), host,
        (HMENU)(INT_PTR)(base + PK_UP), hInst, NULL);
    p.btnDown = CreateWindowW(L"BUTTON", L"down",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(NAV_W2), S(NAV_H), host,
        (HMENU)(INT_PTR)(base + PK_DOWN), hInst, NULL);

    SetChildFont(host, gFont);
    gPanels.push_back(std::move(p));
    gPage = PageOfIndex(i);
    UpdateLayout();
    RefreshButtons();
}

static void DestroyPanel(size_t index) {
    if (index >= gPanels.size()) return;
    HWND hs[PANEL_CTRLS];
    PanelControls(gPanels[index], hs);
    for (int k = 0; k < PANEL_CTRLS; k++)
        if (hs[k]) DestroyWindow(hs[k]);
    gPanels.erase(gPanels.begin() + index);
    // the sheets after the hole shift one slot down, so their placeholders
    // ("list3", "list4", ...) no longer match their position
    for (size_t k = 0; k < gPanels.size(); k++) {
        if (gPanels[k].store.title.empty())
            ApplyCaption(gPanels[k], (int)k);
    }
}

static int FocusPanelIndex() {
    HWND focus = GetFocus();
    if (!focus) return -1;
    HWND hs[PANEL_CTRLS];
    for (size_t i = 0; i < gPanels.size(); i++) {
        PanelControls(gPanels[i], hs);
        for (int k = 0; k < PANEL_CTRLS; k++)
            if (hs[k] == focus) return (int)i;
    }
    return -1;
}

static void OnDeleteList() {
    if (gPanels.size() <= 1) { MessageBeep(MB_ICONWARNING); return; }
    int first = gPage * PAGE_SHEETS;
    int i = FocusPanelIndex();
    // the focused sheet wins, but only if it is one of the two on screen
    if (i < first || i >= first + PAGE_SHEETS) i = first;
    if (i < 0 || i >= (int)gPanels.size()) i = (int)gPanels.size() - 1;
    DestroyPanel((size_t)i);
    if (i >= (int)gPanels.size()) i = (int)gPanels.size() - 1;
    UpdateLayout();
    RefreshButtons();
    if (i >= 0 && i < (int)gPanels.size()) SetFocus(gPanels[i].list);
}

static void SetChildFont(HWND parent, HFONT font) {
    HWND child = GetWindow(parent, GW_CHILD);
    while (child) {
        SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK ViewportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // the sheets and the page strip live inside the viewport, so the
    // notifications of their controls have to reach the window that owns
    // the model
    if (msg == WM_COMMAND || msg == WM_NOTIFY)
        return MainWndProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = GetModuleHandleW(NULL);
        gView = CreateWindowW(L"ListAppView", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 0, 0, hwnd, NULL, hInst, NULL);

        CreateWindowW(L"BUTTON", L"save txt",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(TOOL_BTN_W), S(TOOL_BTN_H),
            hwnd, (HMENU)(INT_PTR)ID_MAIN_TXT, hInst, NULL);
        CreateWindowW(L"BUTTON", L"save",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(TOOL_BTN_W), S(TOOL_BTN_H),
            hwnd, (HMENU)(INT_PTR)ID_MAIN_SAVE, hInst, NULL);
        CreateWindowW(L"BUTTON", L"load",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(TOOL_BTN_W), S(TOOL_BTN_H),
            hwnd, (HMENU)(INT_PTR)ID_MAIN_LOAD, hInst, NULL);
        CreateWindowW(L"BUTTON", L"add list",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(TOOL_BTN_W), S(TOOL_BTN_H),
            hwnd, (HMENU)(INT_PTR)ID_ADD_LIST, hInst, NULL);
        CreateWindowW(L"BUTTON", L"del list",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(TOOL_BTN_W), S(TOOL_BTN_H),
            hwnd, (HMENU)(INT_PTR)ID_DEL_LIST, hInst, NULL);

        hPagePrev = CreateWindowW(L"BUTTON", L"<",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(PAGE_BTN_W), S(PAGE_BTN_H),
            gView, (HMENU)(INT_PTR)ID_PAGE_PREV, hInst, NULL);
        hPageLabel = CreateWindowW(L"STATIC", L"1 / 1",
            SS_CENTER | WS_CHILD | WS_VISIBLE, 0, 0, S(PAGE_LBL_W), S(PAGE_BTN_H),
            gView, (HMENU)(INT_PTR)-1, hInst, NULL);
        hPageNext = CreateWindowW(L"BUTTON", L">",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, S(PAGE_BTN_W), S(PAGE_BTN_H),
            gView, (HMENU)(INT_PTR)ID_PAGE_NEXT, hInst, NULL);

        if (!gFont) gFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SetChildFont(hwnd, gFont);
        gSavesDir = GetSavesDir();
        AddPanel(hwnd);
        AddPanel(hwnd);
        LayoutMain(hwnd);
        UpdateLayout();
        RefreshButtons();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = S(PANEL_DX) + S(PANEL_COL_PITCH) + S(PANEL_W);
        mmi->ptMinTrackSize.y = S(PAGEBAR_H) + S(PANEL_DY) + S(PANEL_H) + S(TOOL_H);
        return 0;
    }
    case WM_DPICHANGED: {
        int dpi = (int)LOWORD(wParam);
        if (dpi > 0 && dpi != g_dpi) {
            g_dpi = dpi;
            CreateAppFont();
            RECT* r = (RECT*)lParam;
            if (r)
                SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left,
                    r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateLayout();
        }
        return 0;
    }
    case WM_SIZE:
        UpdateLayout();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= PANEL_BASE) {
            Panel* p = PanelFromCtrl((HWND)lParam);
            if (p) {
                switch (id - p->idBase) {
                case PK_ADD:    OnAdd(*p); break;
                case PK_EDIT:   OnEdit(*p); break;
                case PK_DEL:    OnDelete(*p); break;
                case PK_RENAME: OnRename(*p); break;
                case PK_TAG:    OnTag(*p); break;
                case PK_UP:     OnMove(*p, -1); break;
                case PK_DOWN:   OnMove(*p, 1); break;
                }
            }
        } else {
            switch (id) {
            case ID_MAIN_SAVE: OnSave(hwnd); break;
            case ID_MAIN_TXT:  OnSaveTxt(hwnd); break;
            case ID_MAIN_LOAD: OpenFilesWindow(); break;
            case ID_ADD_LIST:  AddPanel(hwnd); break;
            case ID_DEL_LIST:  OnDeleteList(); break;
            case ID_PAGE_PREV: SetPage(gPage - 1); break;
            case ID_PAGE_NEXT: SetPage(gPage + 1); break;
            }
        }
        return 0;
    }
    case WM_NOTIFY:
        if (lParam && LOWORD(((NMHDR*)lParam)->code) == LBN_SELCHANGE) RefreshButtons();
        return 0;
    case WM_DESTROY:
        gView = NULL;
        if (gFilesWnd) DestroyWindow(gFilesWnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    EnableDpiAwareness();
    InitDpiApi();
    g_dpi = SystemDpi();

    WNDCLASSW wc = { 0 };
    wc.style = CS_HREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ListAppMain";
    if (!RegisterClassW(&wc)) return 1;

    WNDCLASSW ic = { 0 };
    ic.lpfnWndProc = InputDialogProc;
    ic.hInstance = hInstance;
    ic.hCursor = LoadCursorW(NULL, IDC_ARROW);
    ic.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    ic.lpszClassName = L"ListAppInput";
    if (!RegisterClassW(&ic)) return 1;

    WNDCLASSW fc = { 0 };
    fc.style = CS_HREDRAW;
    fc.lpfnWndProc = FilesWndProc;
    fc.hInstance = hInstance;
    fc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    fc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    fc.lpszClassName = L"ListAppFiles";
    if (!RegisterClassW(&fc)) return 1;

    WNDCLASSW vc = { 0 };
    vc.lpfnWndProc = ViewportWndProc;
    vc.hInstance = hInstance;
    vc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    vc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    vc.lpszClassName = L"ListAppView";
    if (!RegisterClassW(&vc)) return 1;

    CreateAppFont();

    DWORD wstyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    RECT rc = { 0, 0, S(800), S(480) };
    AdjustWindowRectEx(&rc, wstyle, FALSE, 0);

    gMain = CreateWindowW(L"ListAppMain", L"list_manager",
        wstyle,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL);
    if (!gMain) return 1;

    int dpi = WindowDpi(gMain);
    if (dpi > 0 && dpi != g_dpi) {
        g_dpi = dpi;
        CreateAppFont();
    }
    UpdateLayout();

    ShowWindow(gMain, SW_SHOW);
    UpdateWindow(gMain);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}