#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "core.h"
#include "invite_qr.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")

namespace {
constexpr COLORREF FaceColor=RGB(212,208,200), White=RGB(255,255,255), Black=RGB(0,0,0), Dark=RGB(128,128,128), Navy=RGB(0,0,128);
constexpr UINT TrayMessage=WM_APP+1, PollTimer=1, SmokeTimer=2;
constexpr int IdSend=101, IdDelete=102, IdPrevious=103, IdNext=104, IdAdd=110, IdCopy=111, IdContacts=112, IdRemove=113;
constexpr int IdRename=114, IdNetwork=115, IdReconnect=116, IdSent=117, IdPopup=118, IdQuit=119, IdAbout=120, IdShow=121, IdMinimise=122;
constexpr int IdTo=201, IdMessage=202, IdUser=203, IdGroup=204;
constexpr int IdInvitation=210, IdCopyQr=211, IdSendFile=212, IdTransfers=213;
constexpr int IdTransferList=220, IdTransferAccept=221, IdTransferDecline=222, IdTransferCancel=223, IdTransferFolder=224;
enum class WindowKind { Main, Compose, Dialog };
HINSTANCE instance;
int dpi=96, modalDepth=0;
HFONT font=nullptr, boldFont=nullptr;
HBRUSH faceBrush=nullptr, whiteBrush=nullptr;
std::vector<HWND> topWindows;
bool changingDpi=false;
int S(int v) { return MulDiv(v,dpi,96); }
std::wstring Wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0); std::wstring r(n,0);
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n); return r;
}
std::string Utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr); std::string r(n,0);
    WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr); return r;
}
std::wstring WindowText(HWND w) {
    int n=GetWindowTextLengthW(w); std::wstring s(n+1,0); GetWindowTextW(w,s.data(),n+1); s.resize(n); return s;
}
void Fill(HDC dc,RECT r,COLORREF c) { HBRUSH b=CreateSolidBrush(c); FillRect(dc,&r,b); DeleteObject(b); }
void Line(HDC dc,int x,int y,int xx,int yy,COLORREF c) {
    HPEN p=CreatePen(PS_SOLID,S(1),c); auto old=SelectObject(dc,p); MoveToEx(dc,x,y,nullptr); LineTo(dc,xx,yy); SelectObject(dc,old); DeleteObject(p);
}
void Text(HDC dc,const std::wstring& s,RECT r,HFONT f=nullptr,COLORREF c=Black,UINT flags=DT_LEFT|DT_TOP|DT_NOPREFIX) {
    auto old=SelectObject(dc,f?f:font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,c); DrawTextW(dc,s.c_str(),(int)s.size(),&r,flags); SelectObject(dc,old);
}
void Bevel(HDC dc,RECT r,bool sunken,bool thin=false) {
    COLORREF t=sunken?Dark:White,b=sunken?White:Black;
    Line(dc,r.left,r.bottom-S(1),r.left,r.top,t); Line(dc,r.left,r.top,r.right-S(1),r.top,t);
    Line(dc,r.right-S(1),r.top,r.right-S(1),r.bottom-S(1),b); Line(dc,r.right-S(1),r.bottom-S(1),r.left-S(1),r.bottom-S(1),b);
    if(!thin) { InflateRect(&r,-S(1),-S(1)); t=sunken?Black:FaceColor; b=sunken?FaceColor:Dark;
        Line(dc,r.left,r.bottom-S(1),r.left,r.top,t); Line(dc,r.left,r.top,r.right-S(1),r.top,t);
        Line(dc,r.right-S(1),r.top,r.right-S(1),r.bottom-S(1),b); Line(dc,r.right-S(1),r.bottom-S(1),r.left-S(1),r.bottom-S(1),b); }
}
void MakeFonts() {
    font=CreateFontW(-S(11),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_RASTER_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,L"MS Sans Serif");
    boldFont=CreateFontW(-S(11),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_RASTER_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,L"MS Sans Serif");
}
HWND Child(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int id,DWORD ex=0) {
    HWND w=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,(HMENU)(INT_PTR)id,instance,nullptr);
    SetWindowTheme(w,L"",L""); SendMessageW(w,WM_SETFONT,(WPARAM)font,TRUE); return w;
}
void Place(HWND w,int x,int y,int width,int height) { MoveWindow(w,x,y,std::max(1,width),std::max(1,height),TRUE); }
void RememberWindow(HWND w,WindowKind k) {
    topWindows.push_back(w); SetPropW(w,L"WinPopupKind",(HANDLE)((INT_PTR)k+1)); SetWindowTheme(w,L"",L"");
    int value=1; DwmSetWindowAttribute(w,33,&value,sizeof(value)); DwmSetWindowAttribute(w,DWMWA_NCRENDERING_POLICY,&value,sizeof(value));
    SendMessageW(w,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEACCEL),0);
}
void ForgetWindow(HWND w) { topWindows.erase(std::remove(topWindows.begin(),topWindows.end(),w),topWindows.end()); RemovePropW(w,L"WinPopupKind"); }
BOOL CALLBACK FontChild(HWND w,LPARAM) { SendMessageW(w,WM_SETFONT,(WPARAM)font,TRUE); return TRUE; }
void ChangeDpi(HWND moved,int next,const RECT& suggested) {
    if(changingDpi||next==dpi) { SetWindowPos(moved,nullptr,suggested.left,suggested.top,suggested.right-suggested.left,suggested.bottom-suggested.top,SWP_NOZORDER|SWP_NOACTIVATE); return; }
    changingDpi=true; int before=dpi; HFONT old=font,oldBold=boldFont; dpi=next; MakeFonts();
    auto windows=topWindows;
    for(HWND w:windows) if(IsWindow(w)) {
        EnumChildWindows(w,FontChild,0); RECT r; GetWindowRect(w,&r);
        if(w==moved) r=suggested; else { r.right=r.left+MulDiv(r.right-r.left,dpi,before); r.bottom=r.top+MulDiv(r.bottom-r.top,dpi,before); }
        SetWindowPos(w,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE); InvalidateRect(w,nullptr,TRUE);
    }
    DeleteObject(old); DeleteObject(oldBold); changingDpi=false;
}
RECT CaptionButton(RECT r,int index) { return {r.right-S(19+index*16),S(5),r.right-S(5+index*16),S(19)}; }
void PaintCaptionButton(HDC dc,RECT r,int glyph,bool pressed=false) {
    Fill(dc,r,FaceColor); Bevel(dc,r,pressed); int x=r.left+S(3+(pressed?1:0)),y=r.top+S(3+(pressed?1:0));
    if(glyph==0) for(int i=0;i<7;++i) {
        Fill(dc,{x+S(i),y+S(i),x+S(i+2),y+S(i+1)},Black);
        Fill(dc,{x+S(6-i),y+S(i),x+S(8-i),y+S(i+1)},Black);
    } else if(glyph==1) {
        Line(dc,x,y,x+S(8),y,Black); Line(dc,x,y+S(1),x+S(8),y+S(1),Black);
        Line(dc,x,y,x,y+S(8),Black); Line(dc,x+S(7),y,x+S(7),y+S(8),Black); Line(dc,x,y+S(7),x+S(8),y+S(7),Black);
    } else if(glyph==2) Fill(dc,{x,y+S(6),x+S(7),y+S(8)},Black);
    else Text(dc,L"?",r,boldFont,Black,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
void PaintClassicFrame(HDC dc,RECT r,const std::wstring& title,bool active,WindowKind kind,int pressed=0) {
    Fill(dc,r,FaceColor); Bevel(dc,r,false); Fill(dc,{S(3),S(3),r.right-S(3),S(21)},active?Navy:Dark);
    HICON icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,S(16),S(16),LR_SHARED);
    if(icon) DrawIconEx(dc,S(4),S(4),icon,S(16),S(16),0,nullptr,DI_NORMAL);
    int reserve=kind==WindowKind::Main?54:kind==WindowKind::Compose?38:22;
    Text(dc,title,{S(22),S(5),r.right-S(reserve),S(20)},boldFont,White,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    PaintCaptionButton(dc,CaptionButton(r,0),0,pressed==1);
    if(kind==WindowKind::Main) { PaintCaptionButton(dc,CaptionButton(r,1),1,pressed==2); PaintCaptionButton(dc,CaptionButton(r,2),2,pressed==3); }
    else if(kind==WindowKind::Compose) PaintCaptionButton(dc,CaptionButton(r,1),3,pressed==2);
}
int HitAction(HWND w,LPARAM point,WindowKind kind){
    POINT p{GET_X_LPARAM(point),GET_Y_LPARAM(point)};RECT r;GetClientRect(w,&r);
    int n=kind==WindowKind::Main?3:kind==WindowKind::Compose?2:1;
    for(int i=0;i<n;++i){RECT button=CaptionButton(r,i);if(PtInRect(&button,p))return i+1;}
    if(kind==WindowKind::Main){
        if(p.y>=S(23)&&p.y<S(41)){if(p.x>=S(4)&&p.x<S(64))return 500;if(p.x>=S(64)&&p.x<S(100))return 501;}
        if(p.y>=S(45)&&p.y<S(67)&&p.x>=S(4)&&p.x<S(96)){int i=(p.x-S(4))/S(23);int commands[]={IdSend,IdDelete,IdPrevious,IdNext};return commands[i];}
    }return 0;
}
bool BeginClick(HWND w,LPARAM point,WindowKind kind){
    int action=HitAction(w,point,kind);if(!action)return false;SetPropW(w,L"PressedAction",(HANDLE)(INT_PTR)action);SetCapture(w);InvalidateRect(w,nullptr,FALSE);return true;
}
int EndClick(HWND w,LPARAM point,WindowKind kind){
    int action=(int)(INT_PTR)GetPropW(w,L"PressedAction");RemovePropW(w,L"PressedAction");if(GetCapture()==w)ReleaseCapture();InvalidateRect(w,nullptr,FALSE);
    return action&&action==HitAction(w,point,kind)?action:0;
}
LRESULT ClassicHitTest(HWND w,LPARAM point,WindowKind kind) {
    POINT p{GET_X_LPARAM(point),GET_Y_LPARAM(point)}; ScreenToClient(w,&p); RECT r; GetClientRect(w,&r);
    if(kind!=WindowKind::Dialog&&!IsZoomed(w)) {
        bool l=p.x<S(3),t=p.y<S(3),rr=p.x>=r.right-S(3),b=p.y>=r.bottom-S(3);
        if(t&&l)return HTTOPLEFT; if(t&&rr)return HTTOPRIGHT; if(b&&l)return HTBOTTOMLEFT; if(b&&rr)return HTBOTTOMRIGHT;
        if(l)return HTLEFT; if(rr)return HTRIGHT; if(t)return HTTOP; if(b)return HTBOTTOM;
    }
    if(p.y>=S(3)&&p.y<S(22)) {
        int n=kind==WindowKind::Main?3:kind==WindowKind::Compose?2:1;
        for(int i=0;i<n;++i) { RECT button=CaptionButton(r,i); if(PtInRect(&button,p))return HTCLIENT; }
        if(p.x<S(21))return HTSYSMENU; return HTCAPTION;
    }
    return HTCLIENT;
}
void SystemMenu(HWND w) {
    RECT r;GetWindowRect(w,&r); HMENU menu=GetSystemMenu(w,FALSE);
    UINT cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTBUTTON,r.left+S(3),r.top+S(22),0,w,nullptr);
    if(cmd)SendMessageW(w,WM_SYSCOMMAND,cmd,0);
}
bool CaptionClick(HWND w,LPARAM point,WindowKind kind) {
    POINT p{GET_X_LPARAM(point),GET_Y_LPARAM(point)}; RECT r;GetClientRect(w,&r);
    RECT close=CaptionButton(r,0); if(PtInRect(&close,p)){PostMessageW(w,WM_CLOSE,0,0);return true;}
    if(kind==WindowKind::Main) {
        RECT max=CaptionButton(r,1),min=CaptionButton(r,2);
        if(PtInRect(&max,p)){ShowWindow(w,IsZoomed(w)?SW_RESTORE:SW_MAXIMIZE);return true;}
        if(PtInRect(&min,p)){ShowWindow(w,SW_MINIMIZE);return true;}
    } else if(kind==WindowKind::Compose) { RECT help=CaptionButton(r,1); if(PtInRect(&help,p)){SendMessageW(w,WM_COMMAND,IdAbout,0);return true;} }
    return false;
}
void PaintButton(const DRAWITEMSTRUCT* item) {
    RECT r=item->rcItem; Fill(item->hDC,r,FaceColor); bool pressed=(item->itemState&ODS_SELECTED)!=0;
    Bevel(item->hDC,r,pressed); if(pressed)OffsetRect(&r,S(1),S(1));
    Text(item->hDC,WindowText(item->hwndItem),r,font,item->itemState&ODS_DISABLED?Dark:Black,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(item->itemState&ODS_FOCUS){InflateRect(&r,-S(4),-S(4));DrawFocusRect(item->hDC,&r);}
}
void PaintToolbarButton(HDC dc,RECT r,int command,bool enabled,bool pressed=false) {
    Fill(dc,r,FaceColor);Bevel(dc,r,pressed); int x=r.left+S(4+(pressed?1:0)),y=r.top+S(4+(pressed?1:0)); COLORREF c=enabled?Black:Dark;
    if(command==IdSend) {
        Line(dc,x,y+S(3),x+S(7),y-S(1),c);Line(dc,x+S(7),y-S(1),x+S(14),y+S(3),c);
        Line(dc,x,y+S(3),x,y+S(13),c);Line(dc,x,y+S(13),x+S(14),y+S(13),c);Line(dc,x+S(13),y+S(3),x+S(13),y+S(13),c);
        Line(dc,x,y+S(5),x+S(7),y+S(9),c);Line(dc,x+S(7),y+S(9),x+S(14),y+S(5),c);
    } else if(command==IdDelete) {
        Line(dc,x,y+S(2),x+S(14),y+S(2),c);Line(dc,x+S(2),y+S(4),x+S(4),y+S(13),c);
        Line(dc,x+S(12),y+S(4),x+S(10),y+S(13),c);Line(dc,x+S(4),y+S(13),x+S(11),y+S(13),c);
        for(int i=4;i<12;i+=2)Line(dc,x+S(2),y+S(i),x+S(12),y+S(i),c);
        Line(dc,x+S(5),y+S(4),x+S(6),y+S(12),c);Line(dc,x+S(8),y+S(4),x+S(8),y+S(12),c);
    } else for(int arrow=0;arrow<2;++arrow)for(int row=0;row<7;++row) {
        int wide=row<=3?row+1:7-row,start=x+S(1+arrow*6+(command==IdPrevious?4-wide:0));
        Fill(dc,{start,y+S(3+row),start+S(wide),y+S(4+row)},c);
    }
}

struct Field {
    std::wstring label,value,cue; bool secret=false,multi=false,readOnly=false; HWND edit=nullptr;
    std::vector<std::wstring> choices; int selected=0;
};
struct Form {
    HWND window=nullptr,owner=nullptr,errorLabel=nullptr;
    std::wstring title,description,note,action=L"OK",cancel=L"Cancel",error;
    std::vector<Field> fields;std::function<bool(Form&)> validate;
    bool accepted=false,done=false,active=true,hideCancel=false; int height=0,width=430;
};
void LayoutForm(Form& f) {
    RECT r;GetClientRect(f.window,&r);f.height=r.bottom;int y=S(76);
    for(size_t i=0;i<f.fields.size();++i) {
        auto& field=f.fields[i];Place(GetDlgItem(f.window,3000+(int)i),S(12),y,r.right-S(24),S(14));
        int h=S(field.multi?76:22);Place(field.edit,S(12),y+S(16),r.right-S(24),field.choices.empty()?h:S(130));y+=h+S(28);
    }
    Place(f.errorLabel,S(12),y,r.right-S(24),S(29));
    Place(GetDlgItem(f.window,IDCANCEL),r.right-S(160),r.bottom-S(35),S(70),S(23));
    Place(GetDlgItem(f.window,IDOK),r.right-S(82),r.bottom-S(35),S(70),S(23));
}
void PaintForm(Form& f,HDC dc,RECT r) {
    PaintClassicFrame(dc,r,f.title,f.active,WindowKind::Dialog,(int)(INT_PTR)GetPropW(f.window,L"PressedAction"));
    Text(dc,f.description,{S(12),S(34),r.right-S(12),S(72)},font,Black,DT_WORDBREAK|DT_NOPREFIX);
    Text(dc,f.note,{S(12),r.bottom-S(85),r.right-S(12),r.bottom-S(40)},font,Black,DT_WORDBREAK|DT_NOPREFIX);
}
LRESULT CALLBACK FormProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    Form* f=(Form*)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(m==WM_NCCREATE){f=(Form*)((CREATESTRUCTW*)lp)->lpCreateParams;f->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)f);RememberWindow(w,WindowKind::Dialog);}
    if(!f)return DefWindowProcW(w,m,wp,lp);
    switch(m) {
    case WM_NCCALCSIZE:case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:return TRUE;
    case WM_NCHITTEST:return ClassicHitTest(w,lp,WindowKind::Dialog);
    case WM_LBUTTONDOWN:if(BeginClick(w,lp,WindowKind::Dialog))return 0;break;
    case WM_LBUTTONUP:if(EndClick(w,lp,WindowKind::Dialog)&&CaptionClick(w,lp,WindowKind::Dialog))return 0;break;
    case WM_CAPTURECHANGED:RemovePropW(w,L"PressedAction");InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_CREATE:
        for(size_t i=0;i<f->fields.size();++i) {
            auto& field=f->fields[i];Child(w,L"STATIC",field.label.c_str(),0,3000+(int)i);
            if(!field.choices.empty()) {
                field.edit=Child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,1000+(int)i);
                for(const auto& choice:field.choices)SendMessageW(field.edit,CB_ADDSTRING,0,(LPARAM)choice.c_str());
                SendMessageW(field.edit,CB_SETCURSEL,field.selected,0);
            } else {
                DWORD style=WS_TABSTOP|ES_AUTOHSCROLL|(field.secret?ES_PASSWORD:0)|(field.readOnly?ES_READONLY:0);
                if(field.multi)style=WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL|(field.readOnly?ES_READONLY:0);
                field.edit=Child(w,L"EDIT",field.value.c_str(),style,1000+(int)i,WS_EX_CLIENTEDGE);
                SendMessageW(field.edit,EM_LIMITTEXT,field.secret?256:field.multi?32768:512,0);
                if(!field.cue.empty())SendMessageW(field.edit,EM_SETCUEBANNER,FALSE,(LPARAM)field.cue.c_str());
            }
        }
        f->errorLabel=Child(w,L"STATIC",L"",0,19);
        Child(w,L"BUTTON",f->cancel.c_str(),WS_TABSTOP|BS_OWNERDRAW,IDCANCEL);Child(w,L"BUTTON",f->action.c_str(),WS_TABSTOP|BS_OWNERDRAW,IDOK);
        if(f->hideCancel)ShowWindow(GetDlgItem(w,IDCANCEL),SW_HIDE);LayoutForm(*f);return 0;
    case WM_SIZE:LayoutForm(*f);InvalidateRect(w,nullptr,TRUE);return 0;
    case WM_ACTIVATE:f->active=LOWORD(wp)!=WA_INACTIVE;InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_DPICHANGED:ChangeDpi(w,HIWORD(wp),*(RECT*)lp);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);PaintForm(*f,dc,r);EndPaint(w,&p);return 0;}
    case WM_CTLCOLORSTATIC:SetBkColor((HDC)wp,FaceColor);SetTextColor((HDC)wp,Black);return (LRESULT)faceBrush;
    case WM_CTLCOLOREDIT:SetBkColor((HDC)wp,White);SetTextColor((HDC)wp,Black);return (LRESULT)whiteBrush;
    case WM_DRAWITEM:PaintButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_COMMAND:
        if(LOWORD(wp)==IDCANCEL){f->done=true;return 0;}
        if(LOWORD(wp)==IDOK){
            for(auto& field:f->fields){field.value=WindowText(field.edit);if(!field.choices.empty())field.selected=(int)SendMessageW(field.edit,CB_GETCURSEL,0,0);}
            f->error.clear();if(f->validate&&!f->validate(*f)){SetWindowTextW(f->errorLabel,f->error.c_str());MessageBeep(MB_ICONWARNING);return 0;}
            f->accepted=true;f->done=true;return 0;
        }break;
    case WM_SYSKEYDOWN:if(wp==VK_SPACE){SystemMenu(w);return 0;}break;
    case WM_CLOSE:f->done=true;return 0;
    case WM_DESTROY:ForgetWindow(w);return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
bool ShowForm(Form& f) {
    f.height=S(200);for(const auto& field:f.fields)f.height+=S(field.multi?104:50);
    RECT parent;if(f.owner&&IsWindow(f.owner))GetWindowRect(f.owner,&parent);else SystemParametersInfoW(SPI_GETWORKAREA,0,&parent,0);
    int width=S(f.width),x=parent.left+(parent.right-parent.left-width)/2,y=parent.top+(parent.bottom-parent.top-f.height)/2;
    std::vector<HWND> disabled;for(HWND w:topWindows)if(IsWindowEnabled(w)){disabled.push_back(w);EnableWindow(w,FALSE);}++modalDepth;
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupForm",f.title.c_str(),WS_POPUP|WS_SYSMENU|WS_CLIPCHILDREN,x,std::max(0,y),width,f.height,f.owner,nullptr,instance,&f);
    if(w) {
        ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(f.fields.empty()?GetDlgItem(w,IDOK):f.fields[0].edit);
        MSG message;while(!f.done) {
            int result=GetMessageW(&message,nullptr,0,0);if(result<=0){if(!result)PostQuitMessage((int)message.wParam);break;}
            if(message.message==WM_SYSKEYDOWN&&message.wParam==VK_SPACE){SystemMenu(w);continue;}
            if(message.message==WM_SYSKEYDOWN&&message.wParam==VK_F4){f.done=true;continue;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){f.done=true;continue;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN&&(GetKeyState(VK_CONTROL)<0||(GetWindowLongPtrW(GetFocus(),GWL_STYLE)&ES_MULTILINE)==0)){
                SendMessageW(w,WM_COMMAND,GetDlgCtrlID(GetFocus())==IDCANCEL?IDCANCEL:IDOK,0);continue;
            }
            if(!IsDialogMessageW(w,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        for(auto& field:f.fields)if(field.secret)SetWindowTextW(field.edit,L"");DestroyWindow(w);
    }
    --modalDepth;for(HWND old:disabled)if(IsWindow(old))EnableWindow(old,TRUE);
    if(f.owner&&IsWindow(f.owner))SetForegroundWindow(f.owner);return f.accepted;
}
void Alert(HWND owner,const std::wstring& title,const std::wstring& text) {
    Form f;f.owner=owner;f.title=title;f.hideCancel=true;f.description=L"WinPopup";f.fields={{L"",text,L"",false,true,true}};ShowForm(f);
}
bool CopyText(HWND owner,const std::wstring& s) {
    if(!OpenClipboard(owner))return false;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));if(!memory){CloseClipboard();return false;}
    void* data=GlobalLock(memory);if(!data){GlobalFree(memory);CloseClipboard();return false;}
    memcpy(data,s.c_str(),(s.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);EmptyClipboard();
    bool okay=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;if(!okay)GlobalFree(memory);CloseClipboard();return okay;
}

struct Message { uint32_t contact=0; std::wstring from,to,text,dateTime; std::string publicKey; };
enum class Delivery { Pending, Sent, Delivered, Failed };
struct Outgoing {
    uint64_t id=0;uint32_t contact=0,receipt=0;std::wstring recipient,text,dateTime;
    Delivery delivery=Delivery::Pending;std::string publicKey;
};
struct App {
    HWND window=nullptr,body=nullptr,composeWindow=nullptr,toCombo=nullptr,messageEdit=nullptr,sendOkay=nullptr;
    HWND userRadio=nullptr,groupRadio=nullptr,sendCancel=nullptr,tooltips=nullptr;
    HWND invitationWindow=nullptr,invitationEdit=nullptr,invitationCopy=nullptr,invitationCopyQr=nullptr,invitationClose=nullptr;
    HWND transfersWindow=nullptr,transferList=nullptr,transferDetails=nullptr,transferProgress=nullptr;
    HWND transferAccept=nullptr,transferDecline=nullptr,transferCancel=nullptr,transferFolder=nullptr,transferSend=nullptr,transferClose=nullptr;
    popup::QrCode invitationQr;std::wstring invitationText;
    std::vector<popup::FileTransfer> transfers;std::map<uint64_t,std::wstring> transferNames;
    std::set<uint64_t> acceptingTransfers;uint64_t selectedTransfer=0;
    popup::Core core;std::vector<popup::Contact> contacts;std::deque<Message> inbox;std::deque<Outgoing> outbox;
    std::map<uint32_t,std::wstring> drafts;std::wstring unassignedDraft,recipientText,profilePath,myName,notice;
    int currentMessage=-1,composeContact=-1,unread=0;uint64_t nextOutgoing=1,pendingSend=0;
    popup::Connection connection=popup::Connection::Offline;
    bool preview=false,smoke=false,previewCompose=false,tray=false,processing=false,popupOnMessage=true;
    bool mainActive=true,composeActive=true,updatingCombo=false,typedRecipient=false;UINT taskbarCreated=0;
    bool invitationActive=true,transfersActive=true,updatingTransfers=false,fileNotification=false;
};
App* activeApp=nullptr;
LRESULT CALLBACK MainProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK ComposeProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK InvitationProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK TransfersProc(HWND,UINT,WPARAM,LPARAM);
void OpenCompose(App&);void UpdateTray(App&);void RefreshMessage(App&);void FillRecipients(App&);void SendFromComposer(App&);
void OpenInvitation(App&);void OpenTransfers(App&,bool activate=true);void SendFileDialog(App&);
void HandleFileEvent(App&,const popup::Event&);void RefreshTransfers(App&);bool ConfirmExit(App&);
std::wstring ContactName(const popup::Contact& c){return c.name.empty()?L"Contact "+Wide(c.publicKey.substr(0,8)):Wide(c.name);}
std::wstring ContactLabel(const App& a,const popup::Contact& c){
    auto name=ContactName(c);int matches=0;for(const auto& other:a.contacts)if(_wcsicmp(ContactName(other).c_str(),name.c_str())==0)++matches;
    return matches>1?name+L" ["+Wide(c.publicKey.substr(0,8))+L"]":name;
}
popup::Contact* FindContact(App& a,uint32_t number){for(auto& c:a.contacts)if(c.number==number)return &c;return nullptr;}
std::wstring ConnectionText(popup::Connection c){return c==popup::Connection::Direct?L"Direct":c==popup::Connection::Relay?L"Relay":L"Offline / connecting";}
void PaintQr(HDC dc,RECT area,const popup::QrCode& qr){
    Fill(dc,area,White);
    if(qr.size<=0||qr.modules.size()!=(size_t)qr.size*(size_t)qr.size)return;
    int edge=qr.size+2*popup::InvitationQrQuietZone;
    int scale=std::min((int)(area.right-area.left),(int)(area.bottom-area.top))/edge;
    if(scale<1)return;
    int x=area.left+((area.right-area.left)-edge*scale)/2+popup::InvitationQrQuietZone*scale;
    int y=area.top+((area.bottom-area.top)-edge*scale)/2+popup::InvitationQrQuietZone*scale;
    HBRUSH black=CreateSolidBrush(Black);
    for(int row=0;row<qr.size;++row)for(int col=0;col<qr.size;++col)if(qr.Module(col,row)){
        RECT cell{x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale};FillRect(dc,&cell,black);
    }
    DeleteObject(black);
}
std::vector<uint8_t> QrClipboardDib(const popup::QrCode& qr){
    if(qr.size<=0||qr.size>177||qr.modules.size()!=(size_t)qr.size*(size_t)qr.size)return {};
    constexpr int scale=6;int edge=(qr.size+2*popup::InvitationQrQuietZone)*scale;
    BITMAPINFOHEADER header{};header.biSize=sizeof(header);header.biWidth=edge;header.biHeight=edge;
    header.biPlanes=1;header.biBitCount=32;header.biCompression=BI_RGB;header.biSizeImage=(DWORD)(edge*edge*4);
    std::vector<uint8_t> dib(sizeof(header)+header.biSizeImage,0);memcpy(dib.data(),&header,sizeof(header));
    for(int y=0;y<edge;++y)for(int x=0;x<edge;++x){
        int col=x/scale-popup::InvitationQrQuietZone,row=y/scale-popup::InvitationQrQuietZone;
        uint8_t value=col>=0&&row>=0&&col<qr.size&&row<qr.size&&qr.Module(col,row)?0:255;
        size_t offset=sizeof(header)+((size_t)(edge-1-y)*edge+x)*4;
        dib[offset]=dib[offset+1]=dib[offset+2]=value;
    }
    return dib;
}
bool CopyQr(HWND owner,const popup::QrCode& qr){
    auto dib=QrClipboardDib(qr);if(dib.empty()||!OpenClipboard(owner))return false;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,dib.size());
    if(!memory){CloseClipboard();return false;}
    void* target=GlobalLock(memory);if(!target){GlobalFree(memory);CloseClipboard();return false;}
    memcpy(target,dib.data(),dib.size());GlobalUnlock(memory);EmptyClipboard();
    bool okay=SetClipboardData(CF_DIB,memory)!=nullptr;if(!okay)GlobalFree(memory);CloseClipboard();return okay;
}
void LayoutInvitation(App& a){
    if(!a.invitationWindow)return;RECT r;GetClientRect(a.invitationWindow,&r);
    Place(a.invitationEdit,S(252),S(76),r.right-S(266),S(76));
    Place(a.invitationCopy,S(252),r.bottom-S(38),S(82),S(23));
    Place(a.invitationCopyQr,S(342),r.bottom-S(38),S(82),S(23));
    Place(a.invitationClose,r.right-S(86),r.bottom-S(38),S(72),S(23));
    InvalidateRect(a.invitationWindow,nullptr,TRUE);
}
void PaintInvitation(App& a,HDC dc,RECT r){
    PaintClassicFrame(dc,r,L"My Invitation",a.invitationActive,WindowKind::Dialog,(int)(INT_PTR)GetPropW(a.invitationWindow,L"PressedAction"));
    Text(dc,L"Share this invitation with a friend to connect on Tox.",{S(12),S(34),r.right-S(12),S(50)});
    PaintQr(dc,{S(12),S(58),S(238),S(284)},a.invitationQr);
    Text(dc,L"My Tox ID:",{S(252),S(58),r.right-S(14),S(72)});
    Text(dc,L"The QR code contains this complete Tox invitation. Scan it with a compatible Tox client, or copy and share the ID.",
        {S(252),S(166),r.right-S(14),S(222)},font,Black,DT_WORDBREAK|DT_NOPREFIX);
    Text(dc,L"Keep your profile file and password private.",{S(252),S(231),r.right-S(14),S(264)},font,Black,DT_WORDBREAK|DT_NOPREFIX);
}
LRESULT CALLBACK InvitationProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    App* a=(App*)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;a->invitationWindow=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)a);RememberWindow(w,WindowKind::Dialog);}
    if(!a)return DefWindowProcW(w,m,wp,lp);
    switch(m){
    case WM_NCCALCSIZE:case WM_NCPAINT:return 0;case WM_NCACTIVATE:return TRUE;
    case WM_NCHITTEST:return ClassicHitTest(w,lp,WindowKind::Dialog);
    case WM_LBUTTONDOWN:if(BeginClick(w,lp,WindowKind::Dialog))return 0;break;
    case WM_LBUTTONUP:if(EndClick(w,lp,WindowKind::Dialog)&&CaptionClick(w,lp,WindowKind::Dialog))return 0;break;
    case WM_CAPTURECHANGED:RemovePropW(w,L"PressedAction");InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_CREATE:
        a->invitationEdit=Child(w,L"EDIT",a->invitationText.c_str(),WS_TABSTOP|ES_MULTILINE|ES_READONLY,300,WS_EX_CLIENTEDGE);
        a->invitationCopy=Child(w,L"BUTTON",L"&Copy ID",WS_TABSTOP|BS_OWNERDRAW,IdCopy);
        a->invitationCopyQr=Child(w,L"BUTTON",L"Copy &QR",WS_TABSTOP|BS_OWNERDRAW,IdCopyQr);
        a->invitationClose=Child(w,L"BUTTON",L"Close",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL);LayoutInvitation(*a);return 0;
    case WM_SIZE:LayoutInvitation(*a);return 0;
    case WM_ACTIVATE:a->invitationActive=LOWORD(wp)!=WA_INACTIVE;InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_DPICHANGED:ChangeDpi(w,HIWORD(wp),*(RECT*)lp);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);PaintInvitation(*a,dc,r);EndPaint(w,&p);return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetBkColor((HDC)wp,White);SetTextColor((HDC)wp,Black);return (LRESULT)whiteBrush;
    case WM_DRAWITEM:PaintButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_COMMAND:
        if(LOWORD(wp)==IDCANCEL)DestroyWindow(w);
        else if(LOWORD(wp)==IdCopy){if(!CopyText(w,a->invitationText))Alert(w,L"Copy Invitation",L"The clipboard is busy. Please try again.");}
        else if(LOWORD(wp)==IdCopyQr){if(!CopyQr(w,a->invitationQr))Alert(w,L"Copy QR Code",L"The QR code could not be copied. Please try again.");}
        return 0;
    case WM_SYSKEYDOWN:if(wp==VK_SPACE){SystemMenu(w);return 0;}break;
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:ForgetWindow(w);a->invitationWindow=a->invitationEdit=a->invitationCopy=a->invitationCopyQr=a->invitationClose=nullptr;return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenInvitation(App& a){
    if(a.invitationWindow){ShowWindow(a.invitationWindow,SW_RESTORE);SetForegroundWindow(a.invitationWindow);return;}
    std::string address=a.core.Address(),error;
    if(!popup::MakeInvitationQr(address,a.invitationQr,error)){Alert(a.window,L"My Invitation",address.empty()?L"Unlock a profile to view its invitation. Preview mode does not open a profile.":Wide(error));return;}
    a.invitationText=L"tox:"+Wide(address);RECT r;GetWindowRect(a.window,&r);
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupInvitation",L"My Invitation",WS_POPUP|WS_SYSMENU|WS_CLIPCHILDREN,
        r.left+S(30),r.top+S(35),S(560),S(318),a.window,nullptr,instance,&a);
    if(w){ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(a.invitationEdit);}
}
bool TerminalFile(popup::FileState state){
    return state==popup::FileState::Completed||state==popup::FileState::Cancelled||state==popup::FileState::Failed;
}
std::wstring FileSizeText(uint64_t bytes){
    if(bytes<1024)return std::to_wstring(bytes)+L" bytes";
    wchar_t value[64]{};double amount=(double)bytes;
    if(bytes<1024*1024)swprintf_s(value,L"%.1f KB",amount/1024.0);
    else if(bytes<1024ULL*1024*1024)swprintf_s(value,L"%.1f MB",amount/(1024.0*1024));
    else swprintf_s(value,L"%.2f GB",amount/(1024.0*1024*1024));
    return value;
}
std::wstring FileStatus(const popup::FileTransfer& transfer){
    switch(transfer.state){
    case popup::FileState::Offered:return transfer.direction==popup::FileDirection::Incoming?L"Awaiting your acceptance":L"Awaiting acceptance";
    case popup::FileState::Transferring:return transfer.direction==popup::FileDirection::Incoming?L"Receiving":L"Sending";
    case popup::FileState::Paused:return L"Paused";
    case popup::FileState::Completed:return transfer.direction==popup::FileDirection::Incoming?L"Saved":L"Sent to peer";
    case popup::FileState::Cancelled:return L"Cancelled";
    case popup::FileState::Failed:return L"Failed";
    }
    return L"Unknown";
}
std::wstring FileKind(const std::wstring& name){
    auto dot=name.find_last_of(L'.');if(dot==std::wstring::npos)return L"File";
    auto ext=name.substr(dot);const wchar_t* images[]={L".jpg",L".jpeg",L".png",L".gif",L".bmp",L".webp",L".tif",L".tiff",L".heic",L".heif",L".avif"};
    for(const auto* image:images)if(_wcsicmp(ext.c_str(),image)==0)return L"Image";return L"File";
}
popup::FileTransfer* SelectedFile(App& a){
    for(auto& transfer:a.transfers)if(transfer.token==a.selectedTransfer)return &transfer;return nullptr;
}
void UpsertTransfer(App& a,const popup::FileTransfer& transfer){
    if(!transfer.token)return;
    if(a.transferNames.find(transfer.token)==a.transferNames.end()){
        auto contact=FindContact(a,transfer.contact);
        a.transferNames[transfer.token]=contact&&contact->publicKey==transfer.publicKey?ContactLabel(a,*contact):L"Contact "+Wide(transfer.publicKey.substr(0,8));
    }
    bool found=false;for(auto& current:a.transfers)if(current.token==transfer.token){current=transfer;found=true;break;}
    if(!found)a.transfers.push_back(transfer);
    while(a.transfers.size()>128){
        auto oldest=std::find_if(a.transfers.begin(),a.transfers.end(),[](const popup::FileTransfer& t){return TerminalFile(t.state);});
        if(oldest==a.transfers.end())break;
        a.transferNames.erase(oldest->token);a.acceptingTransfers.erase(oldest->token);a.transfers.erase(oldest);
    }
}
void UpdateTransferDetails(App& a){
    if(!a.transfersWindow)return;auto transfer=SelectedFile(a);
    bool incoming=transfer&&transfer->direction==popup::FileDirection::Incoming;
    bool offered=incoming&&transfer->state==popup::FileState::Offered;
    bool busy=transfer&&a.acceptingTransfers.count(transfer->token)!=0;
    EnableWindow(a.transferAccept,offered&&!busy);EnableWindow(a.transferDecline,offered&&!busy);
    EnableWindow(a.transferCancel,transfer&&!TerminalFile(transfer->state));
    EnableWindow(a.transferFolder,incoming&&transfer->state==popup::FileState::Completed&&!transfer->path.empty());
    std::wstring detail=L"Select a transfer to see its details. Incoming files require your acceptance and a new save filename.";
    int progress=0;
    if(transfer){
        detail=FileKind(Wide(transfer->name))+L": "+Wide(transfer->name)+L"\r\n"+
            (incoming?L"From ":L"To ")+a.transferNames[transfer->token]+L" - "+FileStatus(*transfer)+L"\r\n"+
            FileSizeText(transfer->transferred)+L" of "+FileSizeText(transfer->size);
        if(busy)detail+=L" - preparing your save destination";
        if(!transfer->detail.empty())detail+=L"\r\n"+Wide(transfer->detail);
        if(incoming&&transfer->state==popup::FileState::Completed)detail+=L"\r\nSaved to: "+transfer->path;
        if(transfer->state==popup::FileState::Completed)progress=1000;
        else if(transfer->size)progress=(int)((long double)std::min(transfer->transferred,transfer->size)*1000/transfer->size);
    }
    SetWindowTextW(a.transferDetails,detail.c_str());SendMessageW(a.transferProgress,PBM_SETPOS,progress,0);
}
void RefreshTransfers(App& a){
    if(!a.transferList||!IsWindow(a.transferList))return;
    if(!SelectedFile(a))a.selectedTransfer=a.transfers.empty()?0:a.transfers.back().token;
    int top=ListView_GetTopIndex(a.transferList);a.updatingTransfers=true;SendMessageW(a.transferList,WM_SETREDRAW,FALSE,0);
    ListView_DeleteAllItems(a.transferList);
    for(size_t i=0;i<a.transfers.size();++i){
        const auto& transfer=a.transfers[i];auto name=Wide(transfer.name);
        LVITEMW item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=(int)i;item.pszText=name.data();item.lParam=(LPARAM)transfer.token;
        int row=(int)SendMessageW(a.transferList,LVM_INSERTITEMW,0,(LPARAM)&item);
        std::wstring amount=FileSizeText(transfer.size),progress;
        if(transfer.state==popup::FileState::Completed)progress=L"100%";
        else if(transfer.size)progress=std::to_wstring((int)((long double)std::min(transfer.transferred,transfer.size)*100/transfer.size))+L"%";
        else progress=L"0%";
        std::wstring values[]={a.transferNames[transfer.token],transfer.direction==popup::FileDirection::Incoming?L"Receive":L"Send",amount,progress,FileStatus(transfer)};
        for(int col=0;col<5;++col){LVITEMW cell{};cell.iSubItem=col+1;cell.pszText=values[col].data();SendMessageW(a.transferList,LVM_SETITEMTEXTW,row,(LPARAM)&cell);}
        if(transfer.token==a.selectedTransfer)ListView_SetItemState(a.transferList,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    if(top>0&&!a.transfers.empty())ListView_EnsureVisible(a.transferList,std::min(top,(int)a.transfers.size()-1),FALSE);
    SendMessageW(a.transferList,WM_SETREDRAW,TRUE,0);InvalidateRect(a.transferList,nullptr,TRUE);a.updatingTransfers=false;
    UpdateTransferDetails(a);
}
void LayoutTransfers(App& a){
    if(!a.transfersWindow)return;RECT r;GetClientRect(a.transfersWindow,&r);
    Place(a.transferList,S(12),S(54),r.right-S(24),r.bottom-S(228));
    int widths[]={225,125,60,80,75,135};for(int col=0;col<6;++col)ListView_SetColumnWidth(a.transferList,col,S(widths[col]));
    Place(a.transferDetails,S(12),r.bottom-S(166),r.right-S(24),S(69));
    Place(a.transferProgress,S(12),r.bottom-S(85),r.right-S(24),S(16));
    Place(a.transferSend,S(12),r.bottom-S(42),S(94),S(23));
    Place(a.transferAccept,S(116),r.bottom-S(42),S(76),S(23));
    Place(a.transferDecline,S(200),r.bottom-S(42),S(76),S(23));
    Place(a.transferCancel,S(284),r.bottom-S(42),S(88),S(23));
    Place(a.transferFolder,S(382),r.bottom-S(42),S(100),S(23));
    Place(a.transferClose,r.right-S(88),r.bottom-S(42),S(76),S(23));InvalidateRect(a.transfersWindow,nullptr,TRUE);
}
void PaintTransfers(App& a,HDC dc,RECT r){
    PaintClassicFrame(dc,r,L"File Transfers",a.transfersActive,WindowKind::Dialog,(int)(INT_PTR)GetPropW(a.transfersWindow,L"PressedAction"));
    Text(dc,L"Images and files use encrypted Tox transfers. Both contacts must stay online.",{S(12),S(34),r.right-S(12),S(49)});
    Text(dc,L"Files are never opened automatically. Sent to peer does not mean opened or read.",
        {S(12),r.bottom-S(62),r.right-S(12),r.bottom-S(47)},font,Black,DT_SINGLELINE|DT_NOPREFIX);
}
std::wstring SuggestedFileName(const std::string& name){
    std::wstring value=Wide(name);auto slash=value.find_last_of(L"\\/");if(slash!=std::wstring::npos)value=value.substr(slash+1);
    for(auto& ch:value)if(ch<32||wcschr(L"<>:\"/\\|?*",ch))ch=L'_';
    while(!value.empty()&&(value.back()==L'.'||value.back()==L' '))value.pop_back();
    if(value.empty()||value==L"."||value==L"..")value=L"received-file";
    if(value.size()>240)value.resize(240);return value;
}
bool ChooseNewDestination(HWND owner,const popup::FileTransfer& transfer,std::wstring& destination){
    std::vector<wchar_t> path(32768,0);auto name=SuggestedFileName(transfer.name);wcsncpy_s(path.data(),path.size(),name.c_str(),_TRUNCATE);
    for(;;){
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;
        dialog.lpstrFilter=L"All files\0*.*\0\0";dialog.lpstrFile=path.data();dialog.nMaxFile=(DWORD)path.size();
        dialog.lpstrTitle=L"Accept File - Choose a New Save Filename";
        dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_NOREADONLYRETURN;
        ++modalDepth;BOOL chosen=GetSaveFileNameW(&dialog);--modalDepth;
        if(!chosen){if(CommDlgExtendedError())Alert(owner,L"Save File",L"Windows could not open the save dialog. Please try again.");return false;}
        if(GetFileAttributesW(path.data())!=INVALID_FILE_ATTRIBUTES){Alert(owner,L"Choose a New Filename",L"That file already exists. Choose a different name. WinPopup never replaces an existing file.");continue;}
        destination=path.data();return true;
    }
}
void AcceptSelectedFile(App& a){
    auto selected=SelectedFile(a);if(!selected||selected->direction!=popup::FileDirection::Incoming||selected->state!=popup::FileState::Offered)return;
    if(a.acceptingTransfers.count(selected->token))return;
    popup::FileTransfer transfer=*selected;std::wstring destination;
    if(!ChooseNewDestination(a.transfersWindow,transfer,destination))return;
    a.acceptingTransfers.insert(transfer.token);UpdateTransferDetails(a);a.core.AcceptFile(transfer.token,destination);
}
void ShowTransferFolder(App& a){
    auto transfer=SelectedFile(a);if(!transfer||transfer->direction!=popup::FileDirection::Incoming||transfer->state!=popup::FileState::Completed||transfer->path.empty())return;
    auto slash=transfer->path.find_last_of(L"\\/");if(slash==std::wstring::npos)return;
    auto folder=transfer->path.substr(0,slash+1);DWORD attributes=GetFileAttributesW(folder.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)){Alert(a.transfersWindow,L"Show Folder",L"The saved file's folder is no longer available.");return;}
    if((INT_PTR)ShellExecuteW(a.transfersWindow,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)
        Alert(a.transfersWindow,L"Show Folder",L"Windows could not open this folder. The saved path is shown in the transfer details.");
}
void SendFileDialog(App& a){
    HWND owner=a.transfersWindow?a.transfersWindow:a.window;
    if(a.contacts.empty()){Alert(owner,L"Send File",L"Add a contact first using Messages > Contacts > Add Contact.");return;}
    auto contacts=a.contacts;Form form;form.owner=owner;form.title=L"Send File or Image";
    form.description=L"Choose the contact who should receive your file. They will choose whether to accept it.";
    Field field;field.label=L"To:";
    for(size_t i=0;i<contacts.size();++i){field.choices.push_back(ContactLabel(a,contacts[i])+L" - "+ConnectionText(contacts[i].connection));if((int)contacts[i].number==a.composeContact)field.selected=(int)i;}
    form.fields.push_back(field);form.action=L"Choose File";
    form.note=L"Both people must be online. Images are sent as files. Maximum size: "+FileSizeText(popup::Core::MaxFileBytes())+L".";
    if(!ShowForm(form)||form.fields[0].selected<0||form.fields[0].selected>=(int)contacts.size())return;
    auto contact=contacts[(size_t)form.fields[0].selected];
    if(contact.connection==popup::Connection::Offline){Alert(owner,L"Send File",L"This contact is offline. Connect with them before sending a file.");return;}
    std::vector<wchar_t> path(32768,0);OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;
    dialog.lpstrFilter=L"All files\0*.*\0Images\0*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.tif;*.tiff;*.heic;*.heif;*.avif\0\0";
    dialog.lpstrFile=path.data();dialog.nMaxFile=(DWORD)path.size();dialog.lpstrTitle=L"Send File or Image";
    dialog.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_HIDEREADONLY;
    ++modalDepth;BOOL chosen=GetOpenFileNameW(&dialog);--modalDepth;
    if(!chosen){if(CommDlgExtendedError())Alert(owner,L"Send File",L"Windows could not open the file picker. Please try again.");return;}
    auto current=a.core.Contacts();
    auto peer=std::find_if(current.begin(),current.end(),[&contact](const popup::Contact& candidate){return candidate.number==contact.number&&candidate.publicKey==contact.publicKey;});
    if(peer==current.end()||peer->connection==popup::Connection::Offline){Alert(owner,L"Send File",L"This contact is no longer connected. Your file has not been sent.");return;}
    uint64_t token=a.core.SendFile(contact.number,path.data(),contact.publicKey);
    if(!token){Alert(owner,L"Send File",L"The file could not be queued. Please try again.");return;}
    a.selectedTransfer=token;a.transferNames[token]=ContactLabel(a,contact);OpenTransfers(a,true);
}
LRESULT CALLBACK TransfersProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    App* a=(App*)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;a->transfersWindow=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)a);RememberWindow(w,WindowKind::Dialog);}
    if(!a)return DefWindowProcW(w,m,wp,lp);
    switch(m){
    case WM_NCCALCSIZE:case WM_NCPAINT:return 0;case WM_NCACTIVATE:return TRUE;
    case WM_NCHITTEST:return ClassicHitTest(w,lp,WindowKind::Dialog);
    case WM_LBUTTONDOWN:if(BeginClick(w,lp,WindowKind::Dialog))return 0;break;
    case WM_LBUTTONUP:if(EndClick(w,lp,WindowKind::Dialog)&&CaptionClick(w,lp,WindowKind::Dialog))return 0;break;
    case WM_CAPTURECHANGED:RemovePropW(w,L"PressedAction");InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_CREATE:{
        a->transferList=Child(w,WC_LISTVIEWW,L"File transfers",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,IdTransferList);
        ListView_SetExtendedListViewStyle(a->transferList,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const wchar_t* headings[]={L"File",L"Contact",L"Direction",L"Size",L"Progress",L"Status"};
        for(int i=0;i<6;++i){LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=const_cast<wchar_t*>(headings[i]);col.cx=S(100);SendMessageW(a->transferList,LVM_INSERTCOLUMNW,i,(LPARAM)&col);}
        a->transferDetails=Child(w,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,230);
        a->transferProgress=Child(w,PROGRESS_CLASSW,L"Transfer progress",0,231);SendMessageW(a->transferProgress,PBM_SETRANGE32,0,1000);
        SendMessageW(a->transferProgress,PBM_SETBARCOLOR,0,Navy);
        a->transferSend=Child(w,L"BUTTON",L"Send &File...",WS_TABSTOP|BS_OWNERDRAW,IdSendFile);
        a->transferAccept=Child(w,L"BUTTON",L"&Accept...",WS_TABSTOP|BS_OWNERDRAW,IdTransferAccept);
        a->transferDecline=Child(w,L"BUTTON",L"&Decline",WS_TABSTOP|BS_OWNERDRAW,IdTransferDecline);
        a->transferCancel=Child(w,L"BUTTON",L"&Cancel Transfer",WS_TABSTOP|BS_OWNERDRAW,IdTransferCancel);
        a->transferFolder=Child(w,L"BUTTON",L"Show F&older",WS_TABSTOP|BS_OWNERDRAW,IdTransferFolder);
        a->transferClose=Child(w,L"BUTTON",L"Close",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL);
        LayoutTransfers(*a);RefreshTransfers(*a);return 0;
    }
    case WM_SIZE:LayoutTransfers(*a);return 0;
    case WM_ACTIVATE:a->transfersActive=LOWORD(wp)!=WA_INACTIVE;InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_DPICHANGED:ChangeDpi(w,HIWORD(wp),*(RECT*)lp);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);PaintTransfers(*a,dc,r);EndPaint(w,&p);return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetBkColor((HDC)wp,FaceColor);SetTextColor((HDC)wp,Black);return (LRESULT)faceBrush;
    case WM_DRAWITEM:PaintButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_NOTIFY:
        if(((NMHDR*)lp)->hwndFrom==a->transferList&&((NMHDR*)lp)->code==LVN_ITEMCHANGED&&!a->updatingTransfers){
            auto change=(NMLISTVIEW*)lp;if(change->uNewState&LVIS_SELECTED){a->selectedTransfer=(uint64_t)change->lParam;UpdateTransferDetails(*a);}
        }return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case IDCANCEL:DestroyWindow(w);break;
        case IdSendFile:SendFileDialog(*a);break;
        case IdTransferAccept:AcceptSelectedFile(*a);break;
        case IdTransferDecline:case IdTransferCancel:{auto selected=SelectedFile(*a);if(selected&&!TerminalFile(selected->state))a->core.CancelFile(selected->token);break;}
        case IdTransferFolder:ShowTransferFolder(*a);break;
        }return 0;
    case WM_SYSKEYDOWN:if(wp==VK_SPACE){SystemMenu(w);return 0;}break;
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:
        ForgetWindow(w);a->transfersWindow=a->transferList=a->transferDetails=a->transferProgress=nullptr;
        a->transferSend=a->transferAccept=a->transferDecline=a->transferCancel=a->transferFolder=a->transferClose=nullptr;return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenTransfers(App& a,bool activate){
    if(!a.preview)for(const auto& transfer:a.core.Transfers())UpsertTransfer(a,transfer);
    if(a.transfersWindow){RefreshTransfers(a);ShowWindow(a.transfersWindow,activate?SW_RESTORE:SW_SHOWNOACTIVATE);if(activate)SetForegroundWindow(a.transfersWindow);return;}
    RECT r;GetWindowRect(a.window,&r);
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupTransfers",L"File Transfers",WS_POPUP|WS_SYSMENU|WS_CLIPCHILDREN,
        r.left+S(25),r.top+S(40),S(760),S(398),a.window,nullptr,instance,&a);
    if(w){ShowWindow(w,activate?SW_SHOW:SW_SHOWNOACTIVATE);if(activate){SetForegroundWindow(w);SetFocus(a.transferList);}}
}
void HandleFileEvent(App& a,const popup::Event& event){
    UpsertTransfer(a,event.transfer);a.acceptingTransfers.erase(event.transfer.token);
    if(event.type==popup::EventType::FileOffer)a.selectedTransfer=event.transfer.token;
    RefreshTransfers(a);UpdateTray(a);
    if(a.preview||!a.window)return;
    if(event.type==popup::EventType::FileOffer)OpenTransfers(a,a.popupOnMessage);
    if(event.type==popup::EventType::FileOffer||event.type==popup::EventType::FileFinished){
        a.fileNotification=true;
        if(GetForegroundWindow()!=a.transfersWindow){
            NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=a.window;data.uID=1;data.uFlags=NIF_INFO;data.dwInfoFlags=NIIF_INFO|NIIF_RESPECT_QUIET_TIME;
            wcscpy_s(data.szInfoTitle,L"WinPopup File Transfer");
            auto text=event.type==popup::EventType::FileOffer?a.transferNames[event.transfer.token]+L" offered you a file.":FileStatus(event.transfer)+L". Open File Transfers for details.";
            wcsncpy_s(data.szInfo,text.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&data);
        }
    }
}
bool ConfirmExit(App& a){
    if(a.preview)return true;
    auto live=a.core.Transfers();bool active=std::any_of(live.begin(),live.end(),[](const popup::FileTransfer& transfer){return !TerminalFile(transfer.state);});
    if(!active)return true;
    Form form;form.owner=a.window;form.title=L"Exit WinPopup";form.action=L"Exit";
    form.description=L"File transfers or offers are still active. Exiting WinPopup cancels them.";
    form.note=L"Closing only the File Transfers window keeps transfers running. Incomplete received files will not be kept.";
    return ShowForm(form);
}
std::wstring Timestamp(){
    SYSTEMTIME t;GetLocalTime(&t);wchar_t date[40]{},clock[40]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&t,nullptr,date,40,nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,0,&t,nullptr,clock,40);return std::wstring(date)+L" "+clock;
}
RECT ReceiveRect(RECT r){return {S(12),S(109),r.right-S(12),r.bottom-S(29)};}
void Layout(App& a){
    if(!a.window)return;RECT r;GetClientRect(a.window,&r);RECT body=ReceiveRect(r);
    Place(a.body,body.left,body.top,body.right-body.left,body.bottom-body.top);InvalidateRect(a.window,nullptr,TRUE);
}
void RefreshMessage(App& a){
    if(a.inbox.empty())a.currentMessage=-1;else a.currentMessage=std::clamp(a.currentMessage,0,(int)a.inbox.size()-1);
    if(a.body)SetWindowTextW(a.body,a.currentMessage<0?L"":a.inbox[(size_t)a.currentMessage].text.c_str());
    if(a.window)InvalidateRect(a.window,nullptr,FALSE);
}
void Navigate(App& a,int delta){if(a.inbox.empty())return;a.currentMessage=std::clamp(a.currentMessage+delta,0,(int)a.inbox.size()-1);RefreshMessage(a);}
void DeleteCurrent(App& a){if(a.currentMessage<0||a.currentMessage>=(int)a.inbox.size())return;a.inbox.erase(a.inbox.begin()+a.currentMessage);RefreshMessage(a);}
void PaintMain(App& a,HDC dc,RECT r){
    int pressed=(int)(INT_PTR)GetPropW(a.window,L"PressedAction");
    PaintClassicFrame(dc,r,L"WinPopup",a.mainActive,WindowKind::Main,pressed);
    Text(dc,L"&Messages",{S(8),S(25),S(64),S(39)},font,Black,DT_SINGLELINE);
    Text(dc,L"&Help",{S(68),S(25),S(98),S(39)},font,Black,DT_SINGLELINE);
    Line(dc,S(3),S(41),r.right-S(3),S(41),Dark);Line(dc,S(3),S(42),r.right-S(3),S(42),White);
    int cmds[]={IdSend,IdDelete,IdPrevious,IdNext};
    bool enabled[]={true,a.currentMessage>=0,a.currentMessage>0,a.currentMessage>=0&&a.currentMessage+1<(int)a.inbox.size()};
    for(int i=0;i<4;++i)PaintToolbarButton(dc,{S(4+23*i),S(45),S(26+23*i),S(67)},cmds[i],enabled[i],pressed==cmds[i]);
    if(a.currentMessage>=0&&a.currentMessage<(int)a.inbox.size()){
        auto& m=a.inbox[(size_t)a.currentMessage];
        Text(dc,L"Message from "+m.from+L" to "+m.to,{S(12),S(75),r.right-S(12),S(89)},font,Black,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        Text(dc,L"on "+m.dateTime,{S(12),S(88),r.right-S(12),S(102)},font,Black,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }else Text(dc,L"No messages",{S(12),S(75),r.right-S(12),S(90)});
    RECT body=ReceiveRect(r);Fill(dc,body,FaceColor);Bevel(dc,body,true);
    RECT first{S(3),r.bottom-S(19),r.right/2-S(3),r.bottom-S(3)},second{first.right+S(2),first.top,r.right-S(3),first.bottom};
    Bevel(dc,first,true,true);Bevel(dc,second,true,true);
    Text(dc,L"Current message: "+std::to_wstring(a.currentMessage<0?0:a.currentMessage+1),{first.left+S(2),first.top+S(2),first.right-S(2),first.bottom},font,Black,DT_SINGLELINE|DT_NOPREFIX);
    Text(dc,L"Total messages: "+std::to_wstring(a.inbox.size()),{second.left+S(2),second.top+S(2),second.right-S(2),second.bottom},font,Black,DT_SINGLELINE|DT_NOPREFIX);
}
void PaintCompose(App& a,HDC dc,RECT r){
    PaintClassicFrame(dc,r,L"Send Message",a.composeActive,WindowKind::Compose,(int)(INT_PTR)GetPropW(a.composeWindow,L"PressedAction"));
    RECT group{S(12),S(39),r.right-S(86),S(106)};Bevel(dc,group,true,true);
    RECT label{S(20),S(33),S(40),S(47)};Fill(dc,label,FaceColor);Text(dc,L"To:",label);
    Text(dc,L"Message:",{S(12),S(122),r.right-S(12),S(137)});
    RECT edit{S(12),S(139),r.right-S(12),r.bottom-S(13)};Fill(dc,edit,White);Bevel(dc,edit,true);
}
void LayoutCompose(App& a){
    if(!a.composeWindow)return;RECT r;GetClientRect(a.composeWindow,&r);
    Place(a.userRadio,S(21),S(54),S(115),S(17));Place(a.groupRadio,S(137),S(54),S(79),S(17));
    Place(a.toCombo,S(21),S(75),r.right-S(122),S(170));
    Place(a.sendOkay,r.right-S(71),S(48),S(59),S(23));Place(a.sendCancel,r.right-S(71),S(76),S(59),S(23));
    Place(a.messageEdit,S(12),S(139),r.right-S(24),r.bottom-S(152));InvalidateRect(a.composeWindow,nullptr,TRUE);
}
void SaveComposeDraft(App& a){
    if(!a.messageEdit||!IsWindow(a.messageEdit))return;
    if(a.typedRecipient||a.composeContact<0){a.unassignedDraft=WindowText(a.messageEdit);a.recipientText=WindowText(a.toCombo);}
    else a.drafts[(uint32_t)a.composeContact]=WindowText(a.messageEdit);
}
void FillRecipients(App& a){
    if(!a.toCombo||!IsWindow(a.toCombo))return;a.updatingCombo=true;auto typed=a.typedRecipient?a.recipientText:WindowText(a.toCombo);
    SendMessageW(a.toCombo,CB_RESETCONTENT,0,0);int selection=-1;
    for(const auto& c:a.contacts){int row=(int)SendMessageW(a.toCombo,CB_ADDSTRING,0,(LPARAM)ContactLabel(a,c).c_str());
        SendMessageW(a.toCombo,CB_SETITEMDATA,row,(LPARAM)c.number);if((int)c.number==a.composeContact)selection=row;}
    if(selection>=0&&!a.typedRecipient)SendMessageW(a.toCombo,CB_SETCURSEL,selection,0);else SetWindowTextW(a.toCombo,typed.c_str());
    a.updatingCombo=false;
}
void ChangeRecipient(App& a){
    if(a.updatingCombo)return;int row=(int)SendMessageW(a.toCombo,CB_GETCURSEL,0,0);if(row<0)return;
    int number=(int)SendMessageW(a.toCombo,CB_GETITEMDATA,row,0);if(number==a.composeContact&&!a.typedRecipient)return;
    bool unassigned=a.typedRecipient||a.composeContact<0;auto text=WindowText(a.messageEdit);
    SaveComposeDraft(a);a.composeContact=number;a.typedRecipient=false;a.recipientText.clear();
    if(unassigned&&a.drafts[(uint32_t)number].empty())a.drafts[(uint32_t)number]=text;
    SetWindowTextW(a.messageEdit,a.drafts[(uint32_t)number].c_str());
}
void EditRecipient(App& a){
    if(a.updatingCombo)return;
    if(!a.typedRecipient&&a.composeContact>=0)a.drafts[(uint32_t)a.composeContact]=WindowText(a.messageEdit);
    a.typedRecipient=true;a.composeContact=-1;a.recipientText=WindowText(a.toCombo);a.unassignedDraft=WindowText(a.messageEdit);
}
popup::Contact* ComposerRecipient(App& a){
    int row=(int)SendMessageW(a.toCombo,CB_GETCURSEL,0,0);
    if(row>=0&&!a.typedRecipient)return FindContact(a,(uint32_t)SendMessageW(a.toCombo,CB_GETITEMDATA,row,0));
    auto typed=WindowText(a.toCombo);popup::Contact* found=nullptr;
    for(auto& c:a.contacts)if(_wcsicmp(ContactName(c).c_str(),typed.c_str())==0||_wcsicmp(ContactLabel(a,c).c_str(),typed.c_str())==0||_wcsicmp(Wide(c.publicKey).c_str(),typed.c_str())==0){
        if(found)return nullptr;found=&c;
    }return found;
}
void SetComposePending(App& a,bool pending){
    if(!a.composeWindow)return;EnableWindow(a.sendOkay,!pending);EnableWindow(a.toCombo,!pending);
    SendMessageW(a.messageEdit,EM_SETREADONLY,pending,0);SetWindowTextW(a.sendOkay,pending?L"Wait...":L"OK");
}
void SendFromComposer(App& a){
    if(!a.composeWindow||a.pendingSend)return;auto c=ComposerRecipient(a);
    if(!c){Alert(a.composeWindow,L"Send Message",L"Select a contact from the list. Use Messages > Contacts > Add Contact to exchange Tox invitations first.");return;}
    uint32_t number=c->number;auto name=ContactName(*c);auto key=c->publicKey;
    if(c->connection==popup::Connection::Offline){Alert(a.composeWindow,L"Send Message",L"This contact is offline. Both people must be online to send. Your draft will be kept for this session.");return;}
    auto text=WindowText(a.messageEdit);if(text.find_first_not_of(L" \t\r\n")==std::wstring::npos){SetFocus(a.messageEdit);return;}
    auto encoded=Utf8(text);if(encoded.size()>popup::Core::MaxMessageBytes()){
        Alert(a.composeWindow,L"Message too long",L"Please shorten your message to "+std::to_wstring(popup::Core::MaxMessageBytes())+L" UTF-8 bytes or fewer.");return;}
    a.composeContact=(int)number;a.typedRecipient=false;a.recipientText.clear();a.drafts[number]=text;a.pendingSend=a.nextOutgoing++;
    a.outbox.push_back({a.pendingSend,number,0,name,text,Timestamp(),Delivery::Pending,key});
    while(a.outbox.size()>200)a.outbox.pop_front();
    SetComposePending(a,true);a.core.Send(number,encoded);
}
void UpdateTray(App& a){
    if(!a.window||a.preview)return;
    NOTIFYICONDATAW d{};d.cbSize=sizeof(d);d.hWnd=a.window;d.uID=1;d.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;d.uCallbackMessage=TrayMessage;
    d.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_SHARED);
    if(!d.hIcon)d.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    auto tip=L"WinPopup"+(a.unread?L" - "+std::to_wstring(a.unread)+L" new messages":L"");
    size_t offers=0;for(const auto& transfer:a.transfers)if(transfer.direction==popup::FileDirection::Incoming&&transfer.state==popup::FileState::Offered)++offers;
    if(offers)tip+=L" - "+std::to_wstring(offers)+L" file offers";
    wcsncpy_s(d.szTip,tip.c_str(),_TRUNCATE);
    if(!a.tray)a.tray=Shell_NotifyIconW(NIM_ADD,&d)!=FALSE;else Shell_NotifyIconW(NIM_MODIFY,&d);
}
void ShowApp(App& a){ShowWindow(a.window,SW_RESTORE);SetForegroundWindow(a.window);a.unread=0;UpdateTray(a);}
void NotifyMessage(App& a,const std::wstring& from){
    if(a.preview)return;a.fileNotification=false;UpdateTray(a);NOTIFYICONDATAW d{};d.cbSize=sizeof(d);d.hWnd=a.window;d.uID=1;d.uFlags=NIF_INFO;d.dwInfoFlags=NIIF_INFO|NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(d.szInfoTitle,L"WinPopup");auto body=L"Message from "+from;wcsncpy_s(d.szInfo,body.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&d);
    FLASHWINFO flash{sizeof(flash),a.window,FLASHW_TRAY|FLASHW_TIMERNOFG,3,0};FlashWindowEx(&flash);
}
void HandleEvent(App& a,const popup::Event& e){
    switch(e.type){
    case popup::EventType::FileOffer:case popup::EventType::FileProgress:case popup::EventType::FileFinished:HandleFileEvent(a,e);break;
    case popup::EventType::Network:a.connection=e.connection;break;
    case popup::EventType::Contacts:a.contacts=a.core.Contacts();FillRecipients(a);break;
    case popup::EventType::Message:{
        auto c=FindContact(a,e.contact);auto from=c?ContactName(*c):L"Contact "+std::to_wstring(e.contact);
        a.inbox.push_back({e.contact,from,a.myName.empty()?L"YOU":a.myName,Wide(e.text),Timestamp(),c?c->publicKey:""});
        if(a.inbox.size()>200){a.inbox.pop_front();--a.currentMessage;}
        if(a.currentMessage<0||a.popupOnMessage)a.currentMessage=(int)a.inbox.size()-1;
        bool seen=a.window&&GetForegroundWindow()==a.window&&!IsIconic(a.window);
        if(!seen){++a.unread;if(a.window){NotifyMessage(a,from);if(a.popupOnMessage&&!modalDepth&&!a.preview)ShowApp(a);}}
        RefreshMessage(a);break;
    }
    case popup::EventType::Sent:
        for(auto& sent:a.outbox)if(sent.contact==e.contact&&sent.delivery==Delivery::Pending){
            auto c=FindContact(a,e.contact);if(!sent.publicKey.empty()&&(!c||c->publicKey!=sent.publicKey))continue;
            sent.receipt=e.receipt;sent.delivery=Delivery::Sent;
            if(sent.id==a.pendingSend){
                a.pendingSend=0;if(a.drafts[sent.contact]==sent.text)a.drafts.erase(sent.contact);
                if(a.composeWindow&&a.composeContact==(int)sent.contact&&WindowText(a.messageEdit)==sent.text){
                    SetWindowTextW(a.messageEdit,L"");DestroyWindow(a.composeWindow);
                }else SetComposePending(a,false);
            }break;
        }break;
    case popup::EventType::Receipt:
        for(auto& sent:a.outbox)if(sent.contact==e.contact&&sent.receipt==e.receipt&&sent.delivery==Delivery::Sent){
            auto c=FindContact(a,e.contact);if(!sent.publicKey.empty()&&(!c||c->publicKey!=sent.publicKey))continue;
            sent.delivery=Delivery::Delivered;break;
        }break;
    case popup::EventType::Error:
        if(e.key=="file"){a.acceptingTransfers.clear();UpdateTransferDetails(a);}
        if(e.key=="send")for(auto& sent:a.outbox)if(sent.contact==e.contact&&sent.delivery==Delivery::Pending){
            sent.delivery=Delivery::Failed;if(sent.id==a.pendingSend){a.pendingSend=0;SetComposePending(a,false);}break;
        }
        a.notice=Wide(e.text);if(a.window&&!a.preview)Alert(a.composeWindow?a.composeWindow:a.window,L"WinPopup",a.notice);break;
    case popup::EventType::Info:a.notice=Wide(e.text);break;
    case popup::EventType::Request:{
        if(!a.window)break;
        Form f;f.owner=a.window;f.title=L"Contact Request";f.description=L"Someone wants to exchange messages with you. Accept only a person you recognise.";
        f.action=L"Accept";f.cancel=L"Reject";
        f.fields={{L"Their introduction:",Wide(e.text),L"",false,true,true},{L"Their public key:",Wide(e.key),L"",false,true,true}};
        f.note=L"A display name alone does not verify identity. Check this key through a trusted channel if necessary.";
        if(ShowForm(f))a.core.AcceptFriend(e.key);break;
    }}
}
void Poll(App& a){if(a.processing||a.preview||modalDepth)return;a.processing=true;for(auto& e:a.core.Poll())HandleEvent(a,e);a.processing=false;}
void CopyInvitation(App& a){
    auto address=a.core.Address();if(address.empty()){Alert(a.window,L"My Tox Invitation",L"Your invitation is available after you unlock a profile. This preview does not open one.");return;}
    if(!CopyText(a.window,L"tox:"+Wide(address)))Alert(a.window,L"My Tox Invitation",L"The clipboard is busy. Please try again.");
    else Alert(a.window,L"My Tox Invitation",L"Your invitation has been copied. Share it with a friend through a trusted channel. They can paste it into Add Contact.\r\n\r\ntox:"+Wide(address));
}
void AddContact(App& a){
    Form f;f.owner=a.window;f.title=L"Add Contact";f.description=L"Paste your friend's invitation. They must accept your request before you can exchange messages.";
    f.fields={{L"Tox invitation:",L"",L"tox:... or a 76-character Tox ID"},{L"Introduction:",L"Hello! Let's talk on WinPopup.",L"",false,true}};
    f.note=L"Use a trusted channel to exchange invitations. Your contacts can learn your IP address.";
    f.validate=[](Form& f){
        std::string normalized,error;if(!popup::Core::ValidateInvitation(Utf8(f.fields[0].value),normalized,error)){f.error=Wide(error);return false;}
        auto intro=Utf8(f.fields[1].value);if(intro.empty()||intro.size()>popup::Core::MaxRequestBytes()){
            f.error=L"Use an introduction of 1 to "+std::to_wstring(popup::Core::MaxRequestBytes())+L" UTF-8 bytes.";return false;}return true;};
    if(ShowForm(f))a.core.AddFriend(Utf8(f.fields[0].value),Utf8(f.fields[1].value));
}
void ContactsDialog(App& a,bool remove){
    if(a.contacts.empty()){Alert(a.window,L"Contacts",L"No contacts yet. Use Messages > Contacts > Add Contact, or copy your invitation and share it with a friend.");return;}
    auto contacts=a.contacts;
    if(!remove){std::wstring listing;for(const auto& c:contacts)listing+=ContactName(c)+L" - "+ConnectionText(c.connection)+L"\r\n"+Wide(c.publicKey)+L"\r\n\r\n";Alert(a.window,L"Contacts",listing);return;}
    Form f;f.owner=a.window;f.title=L"Remove Contact";f.description=L"Select the contact to remove. Their messages and draft in this session will also be cleared.";
    Field field;field.label=L"Contact:";for(const auto& c:contacts)field.choices.push_back(ContactName(c)+L" ("+Wide(c.publicKey.substr(0,8))+L")");
    f.fields.push_back(field);f.action=L"Remove";f.note=L"You can add them again later using their Tox invitation.";
    if(ShowForm(f)&&f.fields[0].selected>=0&&f.fields[0].selected<(int)contacts.size()){
        uint32_t number=contacts[(size_t)f.fields[0].selected].number;
        a.core.RemoveFriend(number);a.drafts.erase(number);
        for(const auto& sent:a.outbox)if(sent.contact==number&&sent.id==a.pendingSend)a.pendingSend=0;
        a.outbox.erase(std::remove_if(a.outbox.begin(),a.outbox.end(),[number](const Outgoing& sent){return sent.contact==number;}),a.outbox.end());
        a.inbox.erase(std::remove_if(a.inbox.begin(),a.inbox.end(),[number](const Message& m){return m.contact==number;}),a.inbox.end());
        if(a.composeContact==(int)number){if(a.composeWindow)DestroyWindow(a.composeWindow);a.composeContact=-1;a.drafts.erase(number);}
        RefreshMessage(a);
    }
}
void Rename(App& a){
    Form f;f.owner=a.window;f.title=L"Profile";f.description=L"This is the name your contacts see. Your Tox invitation remains the same.";
    f.fields={{L"Display name:",Wide(a.core.SelfName()),L""}};
    f.note=L"Your password-encrypted profile is data\\profile.tox beside WinPopup. Back up the whole folder while the app is closed.";
    f.validate=[](Form& f){if(f.fields[0].value.find_first_not_of(L" \t\r\n")==std::wstring::npos||Utf8(f.fields[0].value).size()>128){f.error=L"Enter a name of 1 to 128 UTF-8 bytes.";return false;}return true;};
    if(ShowForm(f)){a.myName=f.fields[0].value;a.core.Rename(Utf8(a.myName));}
}
void SentDialog(App& a){
    std::wstring text;for(const auto& sent:a.outbox){
        const wchar_t* state=sent.delivery==Delivery::Pending?L"Sending":sent.delivery==Delivery::Sent?L"Sent - awaiting delivery":sent.delivery==Delivery::Delivered?L"Delivered to their device":L"Failed - draft kept";
        text+=L"To "+sent.recipient+L" on "+sent.dateTime+L"\r\n"+state+L"\r\n"+sent.text+L"\r\n\r\n";
    }
    if(text.empty())text=L"No messages sent this session.";text+=L"\r\nDelivered means received by the other device, not read by a person.";Alert(a.window,L"Sent Messages",text);
}
void About(App& a,HWND owner=nullptr){
    Alert(owner?owner:a.window,L"About WinPopup",L"WinPopup P2P 0.3.1\r\nClassic Windows messaging over Tox.\r\n\r\nBoth people must be online. Exchange Tox invitations or QR codes using Messages > Contacts. Workgroup broadcast is not supported.\r\n\r\nMessages and file transfers are end-to-end encrypted. Public Tox discovery and relay nodes help connect peers; those nodes and your contacts can learn IP addresses and connection metadata. This is not an anonymity service.\r\n\r\nImages are sent as ordinary files. Incoming files require acceptance and a new save filename. Files are never opened automatically. Sent to peer does not mean opened or read.\r\n\r\nMessages, transfer status and drafts stay in memory for this session. No plaintext message log is written. Received files are saved only where you explicitly choose.\r\n\r\nMinimise to the tray. Closing WinPopup quits. Back up the app folder while closed, and keep your profile password: there is no password recovery.");
}
void NetworkDialog(App& a){
    Alert(a.window,L"Network",L"Tox network: "+ConnectionText(a.connection)+L"\r\n\r\nDirect means a direct network connection. Relay means encrypted traffic is passing through a Tox relay. A contact can use a different route.\r\n\r\nBoth people must be online. If connection takes a while, allow WinPopup through your firewall, check internet access, then use Messages > Reconnect.\r\n\r\n"+a.notice);
}
HMENU MessagesMenu(App& a){
    HMENU menu=CreatePopupMenu(),contacts=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,IdSend,L"&Send Message...\tCtrl+N");
    AppendMenuW(menu,MF_STRING,IdSendFile,L"Send &File...");
    AppendMenuW(menu,MF_STRING|(a.currentMessage<0?MF_GRAYED:0),IdDelete,L"&Delete Message\tDel");
    AppendMenuW(menu,MF_STRING|(a.currentMessage<=0?MF_GRAYED:0),IdPrevious,L"&Previous Message\tAlt+Left");
    AppendMenuW(menu,MF_STRING|(a.currentMessage<0||a.currentMessage+1>=(int)a.inbox.size()?MF_GRAYED:0),IdNext,L"&Next Message\tAlt+Right");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,IdSent,L"Sent &Messages...");
    AppendMenuW(menu,MF_STRING,IdTransfers,L"File &Transfers...");
    AppendMenuW(contacts,MF_STRING,IdAdd,L"&Add Contact...");AppendMenuW(contacts,MF_STRING,IdContacts,L"&View Contacts...");
    AppendMenuW(contacts,MF_STRING,IdRemove,L"&Remove Contact...");AppendMenuW(contacts,MF_SEPARATOR,0,nullptr);
    AppendMenuW(contacts,MF_STRING,IdInvitation,L"My Invitation (&QR Code)...");
    AppendMenuW(contacts,MF_STRING,IdCopy,L"Copy My &Invitation\tCtrl+I");AppendMenuW(menu,MF_POPUP,(UINT_PTR)contacts,L"&Contacts");
    AppendMenuW(menu,MF_STRING,IdRename,L"Pro&file...");AppendMenuW(menu,MF_STRING,IdNetwork,L"Net&work...");
    AppendMenuW(menu,MF_STRING,IdReconnect,L"&Reconnect");AppendMenuW(menu,MF_STRING|(a.popupOnMessage?MF_CHECKED:0),IdPopup,L"Pop &Up on New Message");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,IdMinimise,L"M&inimise to Tray");AppendMenuW(menu,MF_STRING,IdQuit,L"E&xit\tAlt+F4");return menu;
}
void OpenMenu(App& a,bool help){
    HMENU menu=help?CreatePopupMenu():MessagesMenu(a);if(help)AppendMenuW(menu,MF_STRING,IdAbout,L"&About WinPopup...");
    POINT p{S(help?65:4),S(41)};ClientToScreen(a.window,&p);
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTBUTTON,p.x,p.y,0,a.window,nullptr);DestroyMenu(menu);
    if(command)SendMessageW(a.window,WM_COMMAND,command,0);
}
void ToolTip(App& a,HWND target,UINT_PTR id,RECT r,const wchar_t* text){
    TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_SUBCLASS;tool.hwnd=target;tool.uId=id;tool.rect=r;tool.lpszText=const_cast<wchar_t*>(text);
    SendMessageW(a.tooltips,TTM_ADDTOOLW,0,(LPARAM)&tool);
}

LRESULT CALLBACK ComposeEditSubclass(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto& a=*(App*)data;
    if(m==WM_KEYDOWN&&wp==VK_RETURN&&GetKeyState(VK_CONTROL)<0){SendFromComposer(a);return 0;}
    if(m==WM_CHAR&&(wp==10||(wp==13&&GetKeyState(VK_CONTROL)<0)))return 0;
    return DefSubclassProc(w,m,wp,lp);
}
LRESULT CALLBACK ComposeProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    App* a=(App*)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;a->composeWindow=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)a);RememberWindow(w,WindowKind::Compose);}
    if(!a)return DefWindowProcW(w,m,wp,lp);
    switch(m){
    case WM_NCCALCSIZE:case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:return TRUE;
    case WM_NCHITTEST:return ClassicHitTest(w,lp,WindowKind::Compose);
    case WM_LBUTTONDOWN:if(BeginClick(w,lp,WindowKind::Compose))return 0;break;
    case WM_LBUTTONUP:if(EndClick(w,lp,WindowKind::Compose)&&CaptionClick(w,lp,WindowKind::Compose))return 0;break;
    case WM_CAPTURECHANGED:RemovePropW(w,L"PressedAction");InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_CREATE:
        a->userRadio=Child(w,L"BUTTON",L"&User or computer",WS_TABSTOP|BS_AUTORADIOBUTTON|WS_GROUP,IdUser);
        a->groupRadio=Child(w,L"BUTTON",L"&Workgroup",BS_AUTORADIOBUTTON,IdGroup);
        SendMessageW(a->userRadio,BM_SETCHECK,BST_CHECKED,0);EnableWindow(a->groupRadio,FALSE);
        a->toCombo=Child(w,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL,IdTo);SendMessageW(a->toCombo,CB_LIMITTEXT,256,0);
        a->messageEdit=Child(w,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL|WS_VSCROLL,IdMessage,WS_EX_CLIENTEDGE);
        SendMessageW(a->messageEdit,EM_LIMITTEXT,6000,0);SetWindowSubclass(a->messageEdit,ComposeEditSubclass,1,(DWORD_PTR)a);
        a->sendOkay=Child(w,L"BUTTON",L"OK",WS_TABSTOP|BS_OWNERDRAW,IDOK);a->sendCancel=Child(w,L"BUTTON",L"Cancel",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL);
        FillRecipients(*a);SetWindowTextW(a->messageEdit,a->composeContact>=0&&!a->typedRecipient?a->drafts[(uint32_t)a->composeContact].c_str():a->unassignedDraft.c_str());
        LayoutCompose(*a);SetComposePending(*a,a->pendingSend!=0);return 0;
    case WM_SIZE:LayoutCompose(*a);return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={S(308),S(250)};return 0;
    case WM_ACTIVATE:a->composeActive=LOWORD(wp)!=WA_INACTIVE;InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_DPICHANGED:ChangeDpi(w,HIWORD(wp),*(RECT*)lp);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);PaintCompose(*a,dc,r);EndPaint(w,&p);return 0;}
    case WM_CTLCOLORBTN:case WM_CTLCOLORSTATIC:SetBkColor((HDC)wp,FaceColor);SetTextColor((HDC)wp,Black);return (LRESULT)faceBrush;
    case WM_CTLCOLOREDIT:SetBkColor((HDC)wp,White);SetTextColor((HDC)wp,Black);return (LRESULT)whiteBrush;
    case WM_DRAWITEM:PaintButton((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_COMMAND:
        if(LOWORD(wp)==IDOK)SendFromComposer(*a);
        else if(LOWORD(wp)==IDCANCEL)DestroyWindow(w);
        else if(LOWORD(wp)==IdTo&&HIWORD(wp)==CBN_SELCHANGE)ChangeRecipient(*a);
        else if(LOWORD(wp)==IdTo&&HIWORD(wp)==CBN_EDITCHANGE)EditRecipient(*a);
        else if(LOWORD(wp)==IdAbout)Alert(w,L"Send Message Help",L"Select an existing Tox contact or type their exact display name. To add someone, use Messages > Contacts > Add Contact in WinPopup.\r\n\r\nBoth people must be online.\r\n\r\nWorkgroup broadcast is unavailable: this version sends private messages to one contact.\r\n\r\nClick OK or press Ctrl+Enter to send. Cancel keeps your draft for this session. Sent Messages shows delivery status; delivered does not mean read.");
        return 0;
    case WM_SYSKEYDOWN:if(wp==VK_SPACE){SystemMenu(w);return 0;}break;
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:
        SaveComposeDraft(*a);ForgetWindow(w);a->composeWindow=nullptr;
        a->toCombo=a->messageEdit=a->sendOkay=a->sendCancel=a->userRadio=a->groupRadio=nullptr;return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenCompose(App& a){
    if(a.composeWindow){ShowWindow(a.composeWindow,SW_RESTORE);SetForegroundWindow(a.composeWindow);return;}
    if(a.composeContact<0&&!a.typedRecipient&&a.currentMessage>=0){
        const auto& message=a.inbox[(size_t)a.currentMessage];
        for(const auto& c:a.contacts)if(!message.publicKey.empty()&&c.publicKey==message.publicKey){a.composeContact=(int)c.number;break;}
    }
    if(a.composeContact<0&&!a.typedRecipient&&a.currentMessage<0&&!a.contacts.empty())a.composeContact=(int)a.contacts[0].number;
    RECT r;GetWindowRect(a.window,&r);
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,L"WinPopupCompose",L"Send Message",WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_CLIPCHILDREN,
        r.left+S(203),r.top+S(133),S(308),S(298),a.window,nullptr,instance,&a);
    ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(a.composeContact>=0?a.messageEdit:a.toCombo);
}
LRESULT CALLBACK MainProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    App* a=(App*)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;a->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)a);RememberWindow(w,WindowKind::Main);}
    if(!a)return DefWindowProcW(w,m,wp,lp);
    if(m==a->taskbarCreated&&a->taskbarCreated){a->tray=false;UpdateTray(*a);return 0;}
    switch(m){
    case WM_NCCALCSIZE:case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:return TRUE;
    case WM_NCHITTEST:return ClassicHitTest(w,lp,WindowKind::Main);
    case WM_CREATE:
        a->taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
        a->body=Child(w,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,1000,WS_EX_CLIENTEDGE);
        a->tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,w,nullptr,instance,nullptr);
        {const wchar_t* tips[]={L"Send Message (Ctrl+N)",L"Delete Message (Delete)",L"Previous Message (Alt+Left)",L"Next Message (Alt+Right)"};
            for(int i=0;i<4;++i)ToolTip(*a,w,i+1,{S(4+23*i),S(45),S(26+23*i),S(67)},tips[i]);}
        Layout(*a);UpdateTray(*a);if(!a->preview)SetTimer(w,PollTimer,150,nullptr);return 0;
    case WM_GETMINMAXINFO:{
        auto info=(MINMAXINFO*)lp;info->ptMinTrackSize={S(300),S(230)};
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&monitor)){
            info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
            info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
        }return 0;
    }
    case WM_SIZE:if(wp==SIZE_MINIMIZED&&a->tray)ShowWindow(w,SW_HIDE);else Layout(*a);return 0;
    case WM_DPICHANGED:ChangeDpi(w,HIWORD(wp),*(RECT*)lp);return 0;
    case WM_ACTIVATE:a->mainActive=LOWORD(wp)!=WA_INACTIVE;if(a->mainActive){a->unread=0;UpdateTray(*a);}InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);PaintMain(*a,dc,r);EndPaint(w,&p);return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetBkColor((HDC)wp,FaceColor);SetTextColor((HDC)wp,Black);return (LRESULT)faceBrush;
    case WM_LBUTTONUP:{
        if(!EndClick(w,lp,WindowKind::Main))return 0;
        if(CaptionClick(w,lp,WindowKind::Main))return 0;POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(p.y>=S(23)&&p.y<S(41)){if(p.x>=S(4)&&p.x<S(64))OpenMenu(*a,false);else if(p.x>=S(64)&&p.x<S(100))OpenMenu(*a,true);}
        if(p.y>=S(45)&&p.y<S(67)){int i=(p.x-S(4))/S(23),commands[]={IdSend,IdDelete,IdPrevious,IdNext};if(p.x>=S(4)&&i>=0&&i<4)SendMessageW(w,WM_COMMAND,commands[i],0);}
        return 0;
    }
    case WM_LBUTTONDOWN:if(BeginClick(w,lp,WindowKind::Main))return 0;break;
    case WM_CAPTURECHANGED:RemovePropW(w,L"PressedAction");InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_SYSKEYDOWN:
        if(wp==VK_SPACE)SystemMenu(w);else if(wp=='M')OpenMenu(*a,false);else if(wp=='H')OpenMenu(*a,true);else return DefWindowProcW(w,m,wp,lp);return 0;
    case WM_KEYDOWN:if(wp==VK_F10){OpenMenu(*a,false);return 0;}break;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case IdInvitation:OpenInvitation(*a);break;case IdSendFile:SendFileDialog(*a);break;case IdTransfers:OpenTransfers(*a,true);break;
        case IdSend:OpenCompose(*a);break;case IdDelete:DeleteCurrent(*a);break;case IdPrevious:Navigate(*a,-1);break;case IdNext:Navigate(*a,1);break;
        case IdAdd:AddContact(*a);break;case IdCopy:CopyInvitation(*a);break;case IdContacts:ContactsDialog(*a,false);break;case IdRemove:ContactsDialog(*a,true);break;
        case IdRename:Rename(*a);break;case IdNetwork:NetworkDialog(*a);break;case IdReconnect:a->core.RetryBootstrap();break;case IdSent:SentDialog(*a);break;
        case IdPopup:a->popupOnMessage=!a->popupOnMessage;break;case IdMinimise:ShowWindow(w,SW_MINIMIZE);break;case IdShow:ShowApp(*a);break;
        case IdAbout:About(*a);break;case IdQuit:PostMessageW(w,WM_CLOSE,0,0);break;}return 0;
    case WM_TIMER:if(wp==SmokeTimer){DestroyWindow(w);return 0;}Poll(*a);return 0;
    case TrayMessage:
        if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK||lp==NIN_BALLOONUSERCLICK){ShowApp(*a);if(lp==NIN_BALLOONUSERCLICK&&a->fileNotification){OpenTransfers(*a,true);a->fileNotification=false;}}
        else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU){
            HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,IdShow,L"Open WinPopup");AppendMenuW(menu,MF_STRING,IdSend,L"Send Message...");
            AppendMenuW(menu,MF_STRING,IdTransfers,L"File Transfers...");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,IdQuit,L"Exit");POINT p;GetCursorPos(&p);SetForegroundWindow(w);
            TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,w,nullptr);DestroyMenu(menu);PostMessageW(w,WM_NULL,0,0);
        }return 0;
    case WM_CLOSE:if(ConfirmExit(*a))DestroyWindow(w);return 0;
    case WM_DESTROY:{
        KillTimer(w,PollTimer);if(a->composeWindow)DestroyWindow(a->composeWindow);
        if(a->invitationWindow)DestroyWindow(a->invitationWindow);if(a->transfersWindow)DestroyWindow(a->transfersWindow);
        NOTIFYICONDATAW d{};d.cbSize=sizeof(d);d.hWnd=w;d.uID=1;if(a->tray)Shell_NotifyIconW(NIM_DELETE,&d);
        a->core.Stop();for(const auto& e:a->core.Poll())if(e.type==popup::EventType::Error)Alert(nullptr,L"Profile Save Error",Wide(e.text));
        ForgetWindow(w);a->window=nullptr;PostQuitMessage(0);return 0;
    }}
    return DefWindowProcW(w,m,wp,lp);
}
bool StartProfile(App& a){
    wchar_t path[32768];DWORD length=GetModuleFileNameW(nullptr,path,(DWORD)std::size(path));if(!length||length>=std::size(path))return false;
    std::wstring directory(path,length);directory.resize(directory.find_last_of(L"\\/"));directory+=L"\\data";
    if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){
        Alert(nullptr,L"Profile Folder",L"WinPopup cannot create its data folder. Move the whole extracted app folder to a writable location, such as Downloads, then try again.");return false;
    }
    a.profilePath=directory+L"\\profile.tox";bool existing=GetFileAttributesW(a.profilePath.c_str())!=INVALID_FILE_ATTRIBUTES;
    for(;;){
        Form f;f.title=existing?L"Unlock WinPopup":L"WinPopup Setup";
        f.description=existing?L"Enter the password for your portable profile.":L"Choose your display name and protect your portable profile. No account or email address is required.";
        f.action=existing?L"Unlock":L"OK";
        if(!existing)f.fields.push_back({L"Display name:",L"",L"Your name"});
        f.fields.push_back({L"Password (at least 8 characters):",L"",L"",true});
        if(!existing)f.fields.push_back({L"Confirm password:",L"",L"",true});
        f.note=L"Your encrypted identity travels with the data folder. Back it up and remember this password: there is no password reset or recovery.";
        f.validate=[existing](Form& f){
            int p=existing?0:1;if(!existing&&(f.fields[0].value.find_first_not_of(L" \t\r\n")==std::wstring::npos||Utf8(f.fields[0].value).size()>128)){
                f.error=L"Enter a display name of 1 to 128 UTF-8 bytes.";return false;}
            if(f.fields[p].value.size()<8){f.error=L"Use at least 8 characters for your password.";return false;}
            if(!existing&&f.fields[1].value!=f.fields[2].value){f.error=L"The passwords do not match.";return false;}return true;
        };
        bool accepted=ShowForm(f);popup::CoreOptions options;
        if(accepted){options.profilePath=a.profilePath;options.password=Utf8(f.fields[existing?0:1].value);options.name=existing?"":Utf8(f.fields[0].value);}
        for(auto& field:f.fields)if(field.secret&&!field.value.empty())SecureZeroMemory(field.value.data(),field.value.size()*sizeof(wchar_t));
        if(!accepted)return false;std::string error;bool started=a.core.Start(options,error);
        if(!options.password.empty())SecureZeroMemory(options.password.data(),options.password.size());
        if(started){a.myName=Wide(a.core.SelfName());return true;}Alert(nullptr,existing?L"Cannot Unlock Profile":L"Cannot Create Profile",Wide(error));
    }
}
void RegisterClasses(){
    INITCOMMONCONTROLSEX transferControls{sizeof(transferControls),ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&transferControls);
    HICON icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_SHARED);
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=icon;wc.hIconSm=icon;wc.style=CS_DBLCLKS;
    wc.lpfnWndProc=FormProc;wc.lpszClassName=L"WinPopupForm";RegisterClassExW(&wc);
    wc.lpfnWndProc=ComposeProc;wc.lpszClassName=L"WinPopupCompose";RegisterClassExW(&wc);
    wc.lpfnWndProc=MainProc;wc.lpszClassName=L"WinPopupP2P";RegisterClassExW(&wc);
    wc.lpfnWndProc=InvitationProc;wc.lpszClassName=L"WinPopupInvitation";RegisterClassExW(&wc);
    wc.lpfnWndProc=TransfersProc;wc.lpszClassName=L"WinPopupTransfers";RegisterClassExW(&wc);
}
}
int WINAPI wWinMain(HINSTANCE application,HINSTANCE,PWSTR,int show){
    instance=application;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    dpi=(int)GetDpiForSystem();SetThemeAppProperties(0);MakeFonts();faceBrush=CreateSolidBrush(FaceColor);whiteBrush=CreateSolidBrush(White);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);RegisterClasses();
    App app;activeApp=&app;int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    for(int i=1;i<count;++i){
        if(wcscmp(args[i],L"--smoke-ui")==0)app.preview=app.smoke=true;
        if(wcscmp(args[i],L"--preview-ui")==0)app.preview=true;
        if(wcscmp(args[i],L"--preview-compose")==0)app.preview=app.previewCompose=true;
    }LocalFree(args);
    if(!app.preview&&!StartProfile(app))return 0;
    HWND w=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,L"WinPopupP2P",L"WinPopup",
        WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,S(350),S(284),nullptr,nullptr,instance,&app);
    if(!w){app.core.Stop();return 1;}if(!app.preview)app.contacts=app.core.Contacts();ShowWindow(w,show);UpdateWindow(w);
    if(app.previewCompose)OpenCompose(app);if(app.smoke)SetTimer(w,SmokeTimer,1500,nullptr);
    ACCEL bindings[]={{FVIRTKEY|FCONTROL,'N',IdSend},{FVIRTKEY|FCONTROL,'I',IdCopy},{FVIRTKEY|FALT,VK_LEFT,IdPrevious},{FVIRTKEY|FALT,VK_RIGHT,IdNext},{FVIRTKEY,VK_DELETE,IdDelete}};
    HACCEL accel=CreateAcceleratorTableW(bindings,(int)std::size(bindings));MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0){
        bool compose=app.composeWindow&&(message.hwnd==app.composeWindow||IsChild(app.composeWindow,message.hwnd));
        bool invitation=app.invitationWindow&&(message.hwnd==app.invitationWindow||IsChild(app.invitationWindow,message.hwnd));
        bool transfers=app.transfersWindow&&(message.hwnd==app.transfersWindow||IsChild(app.transfersWindow,message.hwnd));
        HWND target=compose?app.composeWindow:invitation?app.invitationWindow:transfers?app.transfersWindow:w;
        if(message.message==WM_SYSKEYDOWN){
            if(message.wParam==VK_SPACE){SystemMenu(target);continue;}
            if(message.wParam==VK_F4){PostMessageW(target,WM_CLOSE,0,0);continue;}
            if(target==w&&(message.wParam=='M'||message.wParam=='H')){OpenMenu(app,message.wParam=='H');continue;}
        }
        if(target==w&&message.message==WM_KEYDOWN&&message.wParam==VK_F10){OpenMenu(app,false);continue;}
        if(invitation||transfers){
            if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){PostMessageW(target,WM_CLOSE,0,0);continue;}
            if(!IsDialogMessageW(target,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }else if(compose){
            if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){DestroyWindow(app.composeWindow);continue;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN&&(GetKeyState(VK_CONTROL)<0||GetFocus()!=app.messageEdit)){
                if(GetFocus()==app.sendCancel)DestroyWindow(app.composeWindow);else SendFromComposer(app);continue;
            }
            if(!IsDialogMessageW(app.composeWindow,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }else if(!TranslateAcceleratorW(w,accel,&message)&&!IsDialogMessageW(w,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    DestroyAcceleratorTable(accel);DeleteObject(font);DeleteObject(boldFont);DeleteObject(faceBrush);DeleteObject(whiteBrush);return (int)message.wParam;
}
