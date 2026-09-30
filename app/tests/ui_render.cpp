// Developer-only fixtures: production paint functions and native controls.
// All messages/contacts below are illustrative; no profile or network is opened.
#include "../src/main.cpp"
#include "render_bitmap.h"
#include <cstdio>

RECT ChildRect(HWND parent, HWND child) {
    RECT r{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,parent,(POINT*)&r,2); return r;
}
void PrintControl(HDC dc, HWND parent, HWND child) {
    RECT r=ChildRect(parent,child); int saved=SaveDC(dc);
    SetViewportOrgEx(dc,r.left,r.top,nullptr);
    IntersectClipRect(dc,0,0,r.right-r.left,r.bottom-r.top);
    wchar_t name[32]{}; GetClassNameW(child,name,32);
    auto style=GetWindowLongPtrW(child,GWL_STYLE);
    if(_wcsicmp(name,L"Button")==0&&(style&BS_TYPEMASK)==BS_OWNERDRAW) {
        DRAWITEMSTRUCT item{}; item.CtlType=ODT_BUTTON; item.CtlID=GetDlgCtrlID(child);
        item.hwndItem=child; item.hDC=dc; item.rcItem={0,0,r.right-r.left,r.bottom-r.top};
        if(!IsWindowEnabled(child))item.itemState=ODS_DISABLED;
        SendMessageW(parent,WM_DRAWITEM,item.CtlID,(LPARAM)&item);
    }else{
        SendMessageW(child,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN);
    }
    RestoreDC(dc,saved);
}
bool RenderClassic(const std::wstring& output) {
    App app;app.preview=true;app.myName=L"MIKE DAVIS";
    app.contacts.push_back({42,std::string(64,'A'),"LAB23",popup::Connection::Direct});
    for(int i=0;i<10;++i)app.inbox.push_back({42,L"LAB23",L"MIKE DAVIS",L"Mike, I'm updating the database. Right after that...",L"09/07/2004 5:22:48PM",std::string(64,'A')});
    app.currentMessage=7;app.composeContact=42;app.drafts[42]=L"I got it. If that...";
    HWND main=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupP2P",L"WinPopup fixture",
        WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,
        0,0,S(350),S(284),nullptr,nullptr,instance,&app);
    if(!main)return false;
    RefreshMessage(app);
    HWND compose=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupCompose",L"Compose fixture",
        WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_CLIPCHILDREN,
        0,0,S(308),S(298),main,nullptr,instance,&app);
    if(!compose){DestroyWindow(main);return false;}
    app.mainActive=false;app.composeActive=true;
    fixture::Bitmap received(S(350),S(284)),send(S(308),S(298)),scene(S(640),S(480));
    PaintMain(app,received.dc,{0,0,received.width,received.height});PrintControl(received.dc,main,app.body);
    PaintCompose(app,send.dc,{0,0,send.width,send.height});
    for(HWND child=GetWindow(compose,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))PrintControl(send.dc,compose,child);
    scene.Fill(RGB(58,110,165));scene.Copy(received,S(88),S(11));scene.Copy(send,S(291),S(145));
    bool valid=WindowText(app.body)==app.inbox[7].text&&WindowText(app.messageEdit)==app.drafts[42]&&
        WindowText(app.toCombo)==L"LAB23"&&!IsWindowEnabled(app.groupRadio);
    std::wstring suffix=L"-"+std::to_wstring(dpi)+L"dpi.bmp";
    bool saved=scene.Save(output+L"\\classic"+suffix)&&received.Save(output+L"\\received"+suffix)&&send.Save(output+L"\\send"+suffix);
    std::printf("Classic %d DPI: native text, recipient and disabled Workgroup: %s\n",dpi,valid?"PASS":"FAIL");
    DestroyWindow(main);return saved&&valid;
}
bool RenderSetup(const std::wstring& output) {
    Form form;form.title=L"WinPopup Setup";
    form.description=L"Choose your display name and protect your portable profile. No account or email address is required.";
    form.fields={{L"Display name:",L"Mike",L"Your name"},{L"Password (at least 8 characters):",L"fixture-password",L"",true},{L"Confirm password:",L"fixture-password",L"",true}};
    form.note=L"Your encrypted identity travels with the data folder. Back it up and remember this password: there is no password reset or recovery.";
    int height=S(350),width=S(430);
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupForm",form.title.c_str(),WS_POPUP|WS_SYSMENU|WS_CLIPCHILDREN,
        0,0,width,height,nullptr,nullptr,instance,&form);
    if(!w)return false;
    fixture::Bitmap bitmap(width,height);PaintForm(form,bitmap.dc,{0,0,width,height});
    for(HWND child=GetWindow(w,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))PrintControl(bitmap.dc,w,child);
    HDC dc=GetDC(w);auto previous=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,previous);ReleaseDC(w,dc);
    bool valid=true;
    for(size_t i=0;i<form.fields.size();++i){
        auto& field=form.fields[i];RECT r{};GetClientRect(field.edit,&r);auto style=GetWindowLongPtrW(field.edit,GWL_STYLE);
        bool okay=WindowText(field.edit)==field.value&&r.bottom>=metrics.tmHeight&&r.right>S(100)&&
            (HFONT)SendMessageW(field.edit,WM_GETFONT,0,0)==font&&(!field.secret||(style&ES_PASSWORD));
        std::printf("Setup %d DPI field %zu (text, size, font, masking): %s\n",dpi,i+1,okay?"PASS":"FAIL");valid=valid&&okay;
    }
    bool saved=bitmap.Save(output+L"\\setup-"+std::to_wstring(dpi)+L"dpi.bmp");DestroyWindow(w);return saved&&valid;
}
bool RenderInvitationFixture(const std::wstring& output) {
    const std::string uri="tox:000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122230202";
    App app;app.preview=true;app.invitationText=Wide(uri);std::string error;
    if(!popup::MakeInvitationQr(uri,app.invitationQr,error))return false;
    int width=S(560),height=S(318);
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupInvitation",L"My Invitation fixture",WS_POPUP|WS_SYSMENU|WS_CLIPCHILDREN,
        0,0,width,height,nullptr,nullptr,instance,&app);
    if(!w)return false;
    fixture::Bitmap bitmap(width,height);PaintInvitation(app,bitmap.dc,{0,0,width,height});
    for(HWND child=GetWindow(w,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))PrintControl(bitmap.dc,w,child);
    bool okay=WindowText(app.invitationEdit)==Wide(uri)&&bitmap.Save(output+L"\\invitation-"+std::to_wstring(dpi)+L"dpi.bmp");
    auto dib=QrClipboardDib(app.invitationQr);
    if(dib.size()<sizeof(BITMAPINFOHEADER))okay=false;
    else{
        auto header=reinterpret_cast<const BITMAPINFOHEADER*>(dib.data());
        fixture::Bitmap copy(header->biWidth,header->biHeight);
        StretchDIBits(copy.dc,0,0,copy.width,copy.height,0,0,copy.width,copy.height,dib.data()+sizeof(BITMAPINFOHEADER),
            reinterpret_cast<const BITMAPINFO*>(dib.data()),DIB_RGB_COLORS,SRCCOPY);
        okay=copy.Save(output+L"\\clipboard-qr-"+std::to_wstring(dpi)+L"dpi.bmp")&&okay;
    }
    DestroyWindow(w);std::printf("Invitation %d DPI: displayed ID and clipboard bitmap: %s\n",dpi,okay?"PASS":"FAIL");return okay;
}
bool RenderTransfersFixture(const std::wstring& output) {
    App app;app.preview=true;
    popup::FileTransfer incoming;incoming.token=10;incoming.contact=42;incoming.publicKey=std::string(64,'A');incoming.name="holiday-photo.png";
    incoming.size=245760;incoming.direction=popup::FileDirection::Incoming;incoming.detail="Waiting for your permission to save this file.";
    popup::FileTransfer outgoing;outgoing.token=11;outgoing.contact=43;outgoing.publicKey=std::string(64,'B');outgoing.name="project-files.zip";
    outgoing.size=5242880;outgoing.transferred=1940000;outgoing.direction=popup::FileDirection::Outgoing;outgoing.state=popup::FileState::Transferring;
    app.transfers={incoming,outgoing};app.transferNames[10]=L"Alex";app.transferNames[11]=L"Jamie";app.selectedTransfer=10;
    int width=S(760),height=S(398);
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupTransfers",L"File Transfers fixture",WS_POPUP|WS_SYSMENU|WS_CLIPCHILDREN,
        0,0,width,height,nullptr,nullptr,instance,&app);
    if(!w)return false;
    auto paint=[&](const std::wstring& name){fixture::Bitmap bitmap(width,height);PaintTransfers(app,bitmap.dc,{0,0,width,height});
        for(HWND child=GetWindow(w,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))PrintControl(bitmap.dc,w,child);
        return bitmap.Save(output+L"\\"+name+L"-"+std::to_wstring(dpi)+L"dpi.bmp");};
    bool okay=ListView_GetItemCount(app.transferList)==2&&IsWindowEnabled(app.transferAccept)&&IsWindowEnabled(app.transferDecline)&&!IsWindowEnabled(app.transferFolder);
    okay=paint(L"file-offer")&&okay;
    app.transfers[0].state=popup::FileState::Transferring;app.transfers[0].transferred=123000;app.transfers[0].detail="Receiving encrypted image data.";RefreshTransfers(app);
    okay=IsWindowEnabled(app.transferCancel)&&!IsWindowEnabled(app.transferAccept)&&paint(L"file-progress")&&okay;
    app.transfers[0].state=popup::FileState::Completed;app.transfers[0].transferred=incoming.size;app.transfers[0].path=L"C:\\Downloads\\holiday-photo.png";
    app.transfers[0].detail="File received completely and saved. It has not been opened.";RefreshTransfers(app);
    okay=IsWindowEnabled(app.transferFolder)&&!IsWindowEnabled(app.transferCancel)&&paint(L"file-saved")&&okay;
    DestroyWindow(w);std::printf("File transfers %d DPI: offer/progress/saved controls and fixtures: %s\n",dpi,okay?"PASS":"FAIL");return okay;
}
int wmain(int argc,wchar_t**argv){
    if(argc!=2){std::fwprintf(stderr,L"Usage: ui_render EXISTING_OUTPUT_DIRECTORY\n");return 2;}
    instance=GetModuleHandleW(nullptr);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);SetThemeAppProperties(0);
    faceBrush=CreateSolidBrush(FaceColor);whiteBrush=CreateSolidBrush(White);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);RegisterClasses();
    bool okay=true;
    for(int scale:{96,120,144}){
        dpi=scale;MakeFonts();okay=RenderClassic(argv[1])&&okay;okay=RenderSetup(argv[1])&&okay;
        okay=RenderInvitationFixture(argv[1])&&okay;okay=RenderTransfersFixture(argv[1])&&okay;
        DeleteObject(font);DeleteObject(boldFont);
    }
    DeleteObject(faceBrush);DeleteObject(whiteBrush);
    std::printf("Classic interface fixture files: %s\n",okay?"PASS":"FAIL");return okay?0:1;
}
