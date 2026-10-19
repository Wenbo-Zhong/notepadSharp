#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/aui/auibook.h>
#include <wx/stc/stc.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/numdlg.h>
#include <wx/stdpaths.h>
#include <wx/dirdlg.h>
#include <wx/clipbrd.h>
#include <wx/statline.h>
#include <wx/artprov.h>

#include "App.h"
#include "Scintilla.h"
#include "ILexer.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#ifdef __APPLE__
#include <CoreText/CoreText.h>
#endif

namespace {
wxString W(const std::string &s) { return wxString::FromUTF8(s); }
std::string U(const wxString &s) { const auto b = s.ToUTF8(); return b.data() ? std::string(b.data(), b.length()) : std::string(); }

class MainFrame;

class SearchFrame final : public wxDialog {
public:
    SearchFrame(MainFrame *owner);
    void ShowFor(const FindRequest &seed, bool replaceMode,
                 const std::function<void(const FindRequest &)> &perform);
    void SetFeedback(const wxString &message) { status_->SetLabel(message); }
private:
    MainFrame *owner_;
    wxChoice *page_, *syntax_;
    wxTextCtrl *find_, *replace_;
    wxCheckBox *matchCase_, *wholeWord_, *wrap_, *backwards_, *selection_, *dot_, *unicode_;
    wxStaticText *status_;
    FindRequest request_;
    std::function<void(const FindRequest &)> perform_;
    void OnAction(wxCommandEvent &event);
    void OnSyntax(wxCommandEvent &event);
    wxDECLARE_EVENT_TABLE();
};

class MainFrame final : public wxFrame, public Host {
public:
    MainFrame() : wxFrame(nullptr, wxID_ANY, "NotepadSharp", wxDefaultPosition, wxSize(1040, 720)), app_(*this) {
        CreateMenus();
        auto *root = new wxBoxSizer(wxVERTICAL);
        toolbar_ = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_FLAT);
        const struct { const char *label; Command command; const char *help; const char *art; } buttons[] = {
            {"新建",CmdNew,"新建文档",wxART_NEW},{"打开",CmdOpen,"打开文件",wxART_FILE_OPEN},
            {"保存",CmdSave,"保存文档",wxART_FILE_SAVE},{"全部保存",CmdSaveAll,"保存所有文档",wxART_FILE_SAVE},
            {"撤销",CmdUndo,"撤销",wxART_UNDO},{"重做",CmdRedo,"重做",wxART_REDO},
            {"查找",CmdFind,"查找",wxART_FIND},{"替换",CmdReplace,"替换",wxART_FIND_AND_REPLACE},
            {"文件夹搜索",CmdFindInFolder,"在文件夹中查找",wxART_FOLDER_OPEN},
            {"只读",CmdReadOnly,"切换只读",wxART_TICK_MARK}};
        for (const auto &button : buttons) {
            wxBitmap bitmap = wxArtProvider::GetBitmap(button.art, wxART_TOOLBAR, FromDIP(wxSize(24,24)));
            toolbar_->AddTool(button.command, W(button.label), bitmap, W(button.help), wxITEM_NORMAL);
        }
        toolbar_->Realize();
        tabs_ = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS |
            wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_MIDDLE_CLICK_CLOSE);
        status_ = CreateStatusBar(1);
        root->Add(tabs_, 1, wxEXPAND);
        SetSizer(root);
        search_ = new SearchFrame(this);
        Bind(wxEVT_MENU, &MainFrame::OnMenu, this);
        Bind(wxEVT_MENU_OPEN, &MainFrame::OnMenuOpen, this);
        Bind(wxEVT_TOOL, &MainFrame::OnMenu, this);
        Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &MainFrame::OnPageChanged, this, tabs_->GetId());
        Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &MainFrame::OnPageClose, this, tabs_->GetId());
        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
        Bind(wxEVT_TIMER, &MainFrame::OnTimer, this);
        timer_.SetOwner(this);
        timer_.Start(2000);
        checkpoint_.SetOwner(this, wxID_HIGHEST + 24);
        checkpoint_.Start(30000);
        app_.start();
        Centre();
    }

    ~MainFrame() override { if (!closing_ && !docs_.empty()) app_.onCloseRequested(); }

    intptr_t send(int doc, unsigned msg, uintptr_t wp, intptr_t lp) override {
        auto it = docs_.find(doc); if (it == docs_.end()) return 0;
        return it->second->SendMsg(static_cast<int>(msg), static_cast<wxUIntPtr>(wp), static_cast<wxIntPtr>(lp));
    }
    int createEditor() override {
        const int id = nextId_++;
        auto *editor = new wxStyledTextCtrl(tabs_, wxID_ANY);
        editor->SetModEventMask(SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
        docs_[id] = editor;
        tabs_->AddPage(editor, "未命名", true);
        editor->Bind(wxEVT_STC_CHARADDED, [this,id](wxStyledTextEvent &e) { Notify(id, SCN_CHARADDED, &e); });
        editor->Bind(wxEVT_STC_MODIFIED, [this,id](wxStyledTextEvent &e) { Notify(id, SCN_MODIFIED, &e); });
        editor->Bind(wxEVT_STC_SAVEPOINTLEFT, [this,id](wxStyledTextEvent &e) { Notify(id, SCN_SAVEPOINTLEFT, &e); });
        editor->Bind(wxEVT_STC_SAVEPOINTREACHED, [this,id](wxStyledTextEvent &e) { Notify(id, SCN_SAVEPOINTREACHED, &e); });
        editor->Bind(wxEVT_STC_MARGINCLICK, [this,id](wxStyledTextEvent &e) { Notify(id, SCN_MARGINCLICK, &e); });
        editor->Bind(wxEVT_STC_UPDATEUI, [this,id](wxStyledTextEvent &e) { Notify(id, SCN_UPDATEUI, &e); });
        editor->Bind(wxEVT_STC_MACRORECORD, [this,id](wxStyledTextEvent &e) { Notify(id, SCN_MACRORECORD, &e); });
        return id;
    }
    void destroyEditor(int doc) override {
        auto it = docs_.find(doc); if (it == docs_.end()) return;
        const int page = tabs_->FindPage(it->second);
        if (page != wxNOT_FOUND) tabs_->DeletePage(page);
        docs_.erase(it);
    }
    void showEditor(int doc) override {
        auto it = docs_.find(doc); if (it == docs_.end()) return;
        const int page = tabs_->FindPage(it->second);
        if (page != wxNOT_FOUND && tabs_->GetSelection() != page) tabs_->SetSelection(page);
        it->second->SetFocus();
    }
    void setWindowTitle(const std::string &title) override { SetTitle(W(title)); }
    void setStatus(const std::string &text) override { SetStatusText(W(text)); }
    void setTabTitle(int doc, const std::string &title) override {
        auto it = docs_.find(doc); if (it != docs_.end()) { const int page=tabs_->FindPage(it->second); if(page!=wxNOT_FOUND) tabs_->SetPageText(page,W(title)); }
    }
    std::string promptOpenPath() override {
        wxFileDialog dialog(this,"打开文件",{}, {},"所有文件 (*.*)|*.*|文本文件 (*.txt)|*.txt",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        return dialog.ShowModal()==wxID_OK ? U(dialog.GetPath()) : std::string();
    }
    std::string promptDirectory() override {
        wxDirDialog dialog(this,"选择搜索文件夹"); return dialog.ShowModal()==wxID_OK ? U(dialog.GetPath()) : std::string();
    }
    std::string promptSavePath(const std::string &suggested) override {
        wxFileDialog dialog(this,"保存文件",{},W(suggested),"所有文件 (*.*)|*.*",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        return dialog.ShowModal()==wxID_OK ? U(dialog.GetPath()) : std::string();
    }
    SaveChoice askSave(const std::string &name) override {
        wxMessageDialog dialog(this,"是否保存对“"+W(name)+"”的更改？","NotepadSharp",wxYES_NO|wxCANCEL|wxICON_QUESTION);
        dialog.SetYesNoLabels("保存","不保存"); const int result=dialog.ShowModal();
        return result==wxID_YES?SaveChoice::Save:result==wxID_NO?SaveChoice::Discard:SaveChoice::Cancel;
    }
    void alert(const std::string &message) override { wxMessageBox(W(message),"NotepadSharp",wxOK|wxICON_INFORMATION,this); }
    void findFeedback(const std::string &message) override { if(search_) search_->SetFeedback(W(message)); else alert(message); }
    bool showFindDialog(const FindRequest &seed,bool replaceMode,const std::function<void(const FindRequest &)> &perform) override {
        search_->ShowFor(seed,replaceMode,perform); return true;
    }
    FindRequest promptFind(const FindRequest &seed,bool replaceMode) override { search_->ShowFor(seed,replaceMode,{}); return seed; }
    int promptGoto(int currentLine,int lineCount) override { return promptNumber("转到行",currentLine,lineCount); }
    int promptNumber(const std::string &title,int initial,int maximum) override {
        wxNumberEntryDialog dialog(this,W(title+" (1-"+std::to_string(maximum)+")"),"行号：",W(title),initial,1,std::max(1,maximum));
        return dialog.ShowModal()==wxID_OK?static_cast<int>(dialog.GetValue()):0;
    }
    std::string monoFont() const override { return "JetBrains Mono"; }
    int defaultEol() const override { return SC_EOL_LF; }
    std::string stateDirectory() const override { return U(wxStandardPaths::Get().GetUserConfigDir()+"/NotepadSharp"); }
    bool askReloadExternal(const std::string &message) override { return wxMessageBox(W(message),"磁盘文件已变化",wxYES_NO|wxICON_QUESTION|wxNO_DEFAULT,this)==wxYES; }
    bool askOverwriteExternal(const std::string &message) override { return wxMessageBox(W(message),"确认覆盖外部修改",wxYES_NO|wxICON_WARNING|wxNO_DEFAULT,this)==wxYES; }
    void setFullScreen(bool enabled) override { ShowFullScreen(enabled); }
    void setAlwaysOnTop(bool enabled) override { SetWindowStyleFlag(enabled ? GetWindowStyleFlag()|wxSTAY_ON_TOP : GetWindowStyleFlag()&~wxSTAY_ON_TOP); }
    void quit() override { Close(); }
    bool IsClosing() const { return closing_; }
    std::map<int,wxStyledTextCtrl*> docs_;
    App app_;
    SearchFrame *search_ = nullptr;
private:
    wxToolBar *toolbar_ = nullptr;
    wxAuiNotebook *tabs_ = nullptr;
    wxStatusBar *status_ = nullptr;
    int nextId_=1;
    bool closing_=false;
    wxTimer timer_,checkpoint_;
    wxMenu *recentMenu_ = nullptr;
    wxMenu *windowMenu_ = nullptr;

    void Notify(int id,int code,wxStyledTextEvent *event) {
        SCNotification n{}; n.nmhdr.code=code;
        if(event) {
            n.position=event->GetPosition(); n.ch=event->GetKey(); n.modificationType=event->GetModificationType();
            n.margin=event->GetMargin(); n.message=event->GetMessage(); n.wParam=static_cast<uptr_t>(event->GetWParam()); n.lParam=event->GetLParam();
        }
        app_.onNotify(id,&n);
    }
    void CreateMenus() {
        auto *bar=new wxMenuBar();
        auto *file=new wxMenu();
        Add(file,CmdNew,"新建\tCtrl+N"); Add(file,CmdOpen,"打开...\tCtrl+O"); Add(file,CmdSave,"保存\tCtrl+S");
        Add(file,CmdSaveAs,"另存为...\tCtrl+Shift+S"); Add(file,CmdSaveAll,"全部保存");
        recentMenu_=new wxMenu(); file->AppendSubMenu(recentMenu_,"最近文件"); file->AppendSeparator();
        Add(file,CmdClose,"关闭\tCtrl+W"); Add(file,CmdCloseAll,"全部关闭"); Add(file,CmdExit,"退出");
        auto *edit=new wxMenu(); Add(edit,CmdUndo,"撤销\tCtrl+Z"); Add(edit,CmdRedo,"重做\tCtrl+Y"); edit->AppendSeparator();
        Add(edit,CmdCut,"剪切\tCtrl+X"); Add(edit,CmdCopy,"复制\tCtrl+C"); Add(edit,CmdPaste,"粘贴\tCtrl+V"); Add(edit,CmdDelete,"删除\tDel"); Add(edit,CmdSelectAll,"全选\tCtrl+A");
        auto *search=new wxMenu(); Add(search,CmdFind,"查找...\tCtrl+F"); Add(search,CmdReplace,"替换...\tCtrl+H"); Add(search,CmdFindNext,"查找下一个\tF3"); Add(search,CmdFindPrev,"查找上一个\tShift+F3"); Add(search,CmdGoto,"转到行...\tCtrl+G");
        auto *view=new wxMenu(); Add(view,CmdWrap,"自动换行"); Add(view,CmdLineNumbers,"行号"); Add(view,CmdWhitespace,"空白字符"); Add(view,CmdEol,"换行符"); Add(view,CmdZoomIn,"放大\tCtrl++"); Add(view,CmdZoomOut,"缩小\tCtrl+-"); Add(view,CmdZoomReset,"重置缩放\tCtrl+0");
        auto *tools=new wxMenu();
        for(const char *group:{"会话与文件","编码","搜索结果","文本处理","书签与折叠","宏","代码补全","视图"}) {
            auto *sub=new wxMenu(); for(const auto &entry:extraCommands) if(std::string(extraCommandGroup(entry.command))==group) Add(sub,entry.command,W(entry.label));
            tools->Append(wxID_ANY,W(group),sub);
        }
        auto *languages=new wxMenu(); for(int i=0;i<languageChoiceCount;++i) Add(languages,static_cast<Command>(CmdLanguageBase+i),W(languageChoices[i].label));
        auto *font=new wxMenu(); auto *sizes=new wxMenu(); for(int s=6;s<=72;++s) Add(sizes,static_cast<Command>(CmdFontSizeBase+s),wxString::Format("%d pt",s)); font->Append(wxID_ANY,"字号",sizes);
        for(int i=0;i<8;++i) Add(font,static_cast<Command>(CmdFontWeightBase+i),wxString::Format("Weight %d",(i+1)*100)); Add(font,CmdFontItalic,"切换斜体");
        windowMenu_=new wxMenu();
        bar->Append(file,"文件"); bar->Append(edit,"编辑"); bar->Append(search,"搜索"); bar->Append(view,"视图"); bar->Append(tools,"代码工具"); bar->Append(languages,"语言"); bar->Append(font,"字体"); bar->Append(windowMenu_,"窗口"); SetMenuBar(bar);
        auto *accels=new wxAcceleratorEntry[7]{{wxACCEL_CTRL,WXK_TAB,CmdNextTab},{wxACCEL_CTRL|wxACCEL_SHIFT,WXK_TAB,CmdPrevTab},{wxACCEL_CTRL, 'D',CmdDuplicateLine},{wxACCEL_CTRL|wxACCEL_SHIFT,'S',CmdSaveAll},{wxACCEL_CTRL|wxACCEL_SHIFT,'W',CmdCloseAll},{wxACCEL_NORMAL,WXK_F11,CmdFullScreen},{wxACCEL_CTRL,WXK_SPACE,CmdCompleteWord}};
        SetAcceleratorTable(wxAcceleratorTable(7,accels)); delete[] accels;
    }
    static void Add(wxMenu *menu,Command command,const wxString &label) { menu->Append(static_cast<int>(command),label); }
    void OnMenu(wxCommandEvent &event) {
        if(event.GetId()==CmdExit){Close();return;}
        if(event.GetId()==CmdFind||event.GetId()==CmdReplace){ app_.onCommand(static_cast<Command>(event.GetId())); return; }
        app_.onCommand(static_cast<Command>(event.GetId()));
    }
    void OnMenuOpen(wxMenuEvent &event) {
        wxMenu *menu=event.GetMenu();
        if(menu==recentMenu_) {
            while(recentMenu_->GetMenuItemCount()) recentMenu_->Destroy(recentMenu_->FindItemByPosition(0));
            const auto &files=app_.recentFiles();
            if(files.empty()) recentMenu_->Append(wxID_ANY,"（无）")->Enable(false);
            for(size_t i=0;i<files.size()&&i<20;++i) recentMenu_->Append(CmdRecentBase+static_cast<int>(i),W(files[i]));
        } else if(menu==windowMenu_) {
            while(windowMenu_->GetMenuItemCount()) windowMenu_->Destroy(windowMenu_->FindItemByPosition(0));
            const auto names=app_.documentNames();
            if(names.empty()) windowMenu_->Append(wxID_ANY,"（无）")->Enable(false);
            for(size_t i=0;i<names.size()&&i<1000;++i) windowMenu_->Append(CmdDocumentBase+static_cast<int>(i),W(names[i]));
        }
        event.Skip();
    }
    void OnPageChanged(wxAuiNotebookEvent &event) {
        const int sel=event.GetSelection(); if(sel>=0) for(const auto &[id,editor]:docs_) if(tabs_->GetPage(sel)==editor){app_.onTabChanged(id);break;}
        event.Skip();
    }
    void OnPageClose(wxAuiNotebookEvent &event) {
        const int page=event.GetSelection();
        if(page>=0 && page<static_cast<int>(tabs_->GetPageCount())) {
            tabs_->SetSelection(static_cast<size_t>(page));
            app_.onCommand(CmdClose);
        }
        event.Veto();
    }
    void OnTimer(wxTimerEvent &event) { if(event.GetId()==wxID_HIGHEST+24) app_.checkpoint(); else app_.pollFiles(); }
    void OnClose(wxCloseEvent &event) {
        if(!closing_ && !app_.onCloseRequested()){event.Veto();return;}
        closing_=true; timer_.Stop(); checkpoint_.Stop(); event.Skip();
    }
    friend class SearchFrame;
};

wxBEGIN_EVENT_TABLE(SearchFrame, wxDialog)
    EVT_BUTTON(wxID_ANY, SearchFrame::OnAction)
    EVT_CHOICE(wxID_ANY, SearchFrame::OnSyntax)
wxEND_EVENT_TABLE()

SearchFrame::SearchFrame(MainFrame *owner)
    : wxDialog(owner,wxID_ANY,"查找与替换",wxDefaultPosition,wxSize(720,430),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER),owner_(owner) {
    auto *root=new wxBoxSizer(wxVERTICAL);
    page_=new wxChoice(this,wxID_ANY); page_->Append("查找");page_->Append("替换");page_->Append("标记");page_->SetSelection(0);root->Add(page_,0,wxALL,8);
    auto *grid=new wxFlexGridSizer(2,2,6,8);grid->Add(new wxStaticText(this,wxID_ANY,"查找目标："),0,wxALIGN_CENTER_VERTICAL);find_=new wxTextCtrl(this,wxID_ANY);grid->Add(find_,1,wxEXPAND);
    grid->Add(new wxStaticText(this,wxID_ANY,"替换为："),0,wxALIGN_CENTER_VERTICAL);replace_=new wxTextCtrl(this,wxID_ANY);grid->Add(replace_,1,wxEXPAND);grid->AddGrowableCol(1,1);root->Add(grid,0,wxEXPAND|wxLEFT|wxRIGHT,8);
    auto *opts=new wxFlexGridSizer(3,3,2,12);
    matchCase_=new wxCheckBox(this,wxID_ANY,"匹配大小写");wholeWord_=new wxCheckBox(this,wxID_ANY,"全词匹配");wrap_=new wxCheckBox(this,wxID_ANY,"循环查找");backwards_=new wxCheckBox(this,wxID_ANY,"反向查找");selection_=new wxCheckBox(this,wxID_ANY,"选区范围");dot_=new wxCheckBox(this,wxID_ANY,". 匹配换行");
    for(auto *c:{matchCase_,wholeWord_,wrap_,backwards_,selection_,dot_})opts->Add(c,0,wxALL,3);root->Add(opts,0,wxALL,6);
    auto *modeRow=new wxBoxSizer(wxHORIZONTAL);syntax_=new wxChoice(this,wxID_ANY);syntax_->Append("普通");syntax_->Append("扩展");syntax_->Append("正则");syntax_->SetSelection(0);modeRow->Add(syntax_,0,wxRIGHT,12);unicode_=new wxCheckBox(this,wxID_ANY,"完整 Unicode（ICU 属性、折叠、字素簇）");modeRow->Add(unicode_,0,wxALIGN_CENTER_VERTICAL);root->Add(modeRow,0,wxALL,8);
    status_=new wxStaticText(this,wxID_ANY,"正则替换支持 \\1、$1、$+{name}。");root->Add(status_,0,wxEXPAND|wxALL,8);
    auto *actions=new wxGridSizer(2,5,5,5);const struct{const char*name;int tag;} acts[]={{"查找下一个",1},{"查找上一个",2},{"计数",3},{"查找当前文档",4},{"查找所有文档",5},{"替换",6},{"全部替换",7},{"替换打开文档",8},{"标记全部",9},{"清除标记",10}};
    for(auto a:acts){auto*b=new wxButton(this,a.tag,W(a.name));actions->Add(b,1,wxEXPAND);}
    root->Add(actions,0,wxEXPAND|wxALL,8);SetSizer(root);Layout();CentreOnParent();
}
void SearchFrame::ShowFor(const FindRequest &seed,bool replaceMode,const std::function<void(const FindRequest &)> &perform) {
    perform_=perform;
    if(!IsShown()||seed.find!=request_.find){request_=seed;find_->SetValue(W(seed.find));replace_->SetValue(W(seed.replace));matchCase_->SetValue(seed.matchCase);wholeWord_->SetValue(seed.wholeWord);wrap_->SetValue(seed.wrapAround);backwards_->SetValue(seed.backwards);selection_->SetValue(seed.inSelection);dot_->SetValue(seed.dotMatchesNewline);unicode_->SetValue(seed.unicode);syntax_->SetSelection(seed.regex?2:seed.extended?1:0);}
    page_->SetSelection(replaceMode?1:0);Show();Raise();find_->SetFocus();find_->SelectAll();
}
void SearchFrame::OnSyntax(wxCommandEvent &) { const auto mode=syntax_->GetSelection();dot_->Enable(mode==2);unicode_->Enable(mode==2); }
void SearchFrame::OnAction(wxCommandEvent &event) {
    if(event.GetId()<1||event.GetId()>10)return;
    request_.find=U(find_->GetValue());request_.replace=U(replace_->GetValue());request_.matchCase=matchCase_->GetValue();request_.wholeWord=wholeWord_->GetValue();request_.wrapAround=wrap_->GetValue();request_.backwards=backwards_->GetValue();request_.inSelection=selection_->GetValue();request_.dotMatchesNewline=dot_->GetValue();request_.unicode=unicode_->GetValue();request_.regex=syntax_->GetSelection()==2;request_.extended=syntax_->GetSelection()==1;request_.cancelled=request_.find.empty();
    const FindAction actions[]={FindAction::Next,FindAction::Previous,FindAction::Count,FindAction::FindAllCurrent,FindAction::FindAllOpen,FindAction::ReplaceOne,FindAction::ReplaceAll,FindAction::ReplaceAllOpen,FindAction::MarkAll,FindAction::ClearMarks};request_.action=actions[event.GetId()-1];if(perform_&&!request_.cancelled)perform_(request_);
}

class NotepadSharpApp final : public wxApp {
public: bool OnInit() override {
#ifdef __APPLE__
    const auto dir=wxStandardPaths::Get().GetExecutablePath().BeforeLast('/');const auto fonts=dir+"/fonts";wxArrayString files;wxDir::GetAllFiles(fonts,&files,"*.ttf");
    for(const auto &file:files){CFURLRef u=CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,reinterpret_cast<const UInt8*>(file.utf8_str().data()),file.utf8_str().length(),false);if(u){CTFontManagerRegisterFontsForURL(u,kCTFontManagerScopeProcess,nullptr);CFRelease(u);}}
#endif
    auto *frame=new MainFrame();SetTopWindow(frame);frame->Show();return true;
}};
} // namespace
wxIMPLEMENT_APP(NotepadSharpApp);
