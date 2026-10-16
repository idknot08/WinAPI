#include "ChatDlg.h"

class ChatApp : public CWinApp {
public:
    BOOL InitInstance() override {
        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&controls);
        CWinApp::InitInstance();
        ChatDlg dlg;
        m_pMainWnd = &dlg;
        dlg.DoModal();
        m_pMainWnd = nullptr;
        return FALSE;
    }
};
ChatApp theApp;
