#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SCNotification;

enum Command {
    CmdNew = 1,
    CmdOpen,
    CmdSave,
    CmdSaveAs,
    CmdClose,
    CmdExit,
    CmdUndo,
    CmdRedo,
    CmdCut,
    CmdCopy,
    CmdPaste,
    CmdDelete,
    CmdSelectAll,
    CmdFind,
    CmdFindNext,
    CmdFindPrev,
    CmdReplace,
    CmdGoto,
    CmdWrap,
    CmdLineNumbers,
    CmdWhitespace,
    CmdEol,
    CmdZoomIn,
    CmdZoomOut,
    CmdZoomReset,
    CmdSaveAll, CmdCloseAll, CmdDuplicateLine, CmdDeleteLine, CmdMoveLineUp, CmdMoveLineDown,
    CmdUpperCase, CmdLowerCase, CmdTrimTrailing, CmdEolCRLF, CmdEolLF, CmdEolCR,
    CmdToggleBookmark, CmdNextBookmark, CmdPrevBookmark, CmdClearBookmarks,
    CmdFoldAll, CmdUnfoldAll, CmdFontItalic,
    CmdEncodingUtf8, CmdEncodingUtf8Bom, CmdEncodingUtf16LE, CmdEncodingUtf16BE, CmdEncodingLatin1,
    CmdSaveSession, CmdLoadSession, CmdFindAllOpen, CmdNextResult, CmdPrevResult, CmdMarkAll, CmdClearMarks,
    CmdSortAsc, CmdSortDesc, CmdUniqueLines, CmdToggleComment, CmdTabsToSpaces,
    CmdFindInFolder, CmdMacroStart, CmdMacroStop, CmdMacroPlay, CmdMacroSave, CmdMacroLoad,
    CmdCompleteWord, CmdAutoComplete,
    CmdReload, CmdSaveCopy, CmdCloseOthers, CmdCloseLeft, CmdCloseRight, CmdReadOnly, CmdStatistics,
    CmdRemoveEmptyLines, CmdRemoveBlankLines, CmdUniqueAdjacent, CmdReverseLines, CmdSortLengthAsc, CmdSortLengthDesc,
    CmdSortIgnoreCaseAsc, CmdSortIgnoreCaseDesc, CmdTrimLeading, CmdTrimBoth,
    CmdSelectNextOccurrence, CmdSelectAllOccurrences, CmdGotoBrace, CmdSelectBrace,
    CmdMacroRepeat, CmdMacroToEnd, CmdEolsToSpaces, CmdTrimAndJoin, CmdSpacesToTabs, CmdIndentToTabs,
    CmdNextTab, CmdPrevTab, CmdCopyFullPath, CmdCopyFileName, CmdCopyDirectory,
    CmdFullScreen, CmdAlwaysOnTop, CmdFoldCurrent, CmdUnfoldCurrent, CmdHideLines, CmdShowAllLines,
    CmdMonitorFile, CmdReopenClosed,
    CmdFontSizeBase = 200, CmdFontWeightBase = 300,
    CmdLanguageBase = 400, CmdRecentBase = 500, CmdDocumentBase = 600
};

enum class SaveChoice { Save, Discard, Cancel };

enum class FindAction { Next, Previous, ReplaceOne, ReplaceAll, ReplaceAllOpen, Count, FindAllCurrent, FindAllOpen, MarkAll, ClearMarks };

struct FindRequest {
    std::string find;
    std::string replace;
    bool matchCase = false;
    bool wholeWord = false;
    bool regex = false;
    bool extended = false;
    bool wrapAround = true;
    bool backwards = false;
    bool inSelection = false;
    bool dotMatchesNewline = false;
    bool unicode = false; // ICU properties, full case folding and grapheme clusters.
    FindAction action = FindAction::Next;
    bool cancelled = true;
};

// Platform window. The same App drives every backend.
class Host {
public:
    virtual ~Host() = default;
    virtual intptr_t send(int doc, unsigned msg, uintptr_t wParam, intptr_t lParam) = 0;
    virtual int createEditor() = 0;
    virtual void destroyEditor(int doc) = 0;
    virtual void showEditor(int doc) = 0;
    virtual void setWindowTitle(const std::string &title) = 0;
    virtual void setStatus(const std::string &text) = 0;
    virtual void setTabTitle(int doc, const std::string &title) = 0;
    virtual std::string promptOpenPath() = 0;
    virtual std::string promptDirectory() { return {}; }
    virtual std::string promptSavePath(const std::string &suggested) = 0;
    virtual SaveChoice askSave(const std::string &name) = 0;
    virtual void alert(const std::string &message) = 0;
    virtual bool showFindDialog(const FindRequest &, bool, const std::function<void(const FindRequest &)> &) { return false; }
    virtual void findFeedback(const std::string &message) { alert(message); }
    virtual FindRequest promptFind(const FindRequest &seed, bool replaceMode) = 0;
    virtual int promptGoto(int currentLine, int lineCount) = 0;
    virtual int promptNumber(const std::string &, int initial, int maximum) { return promptGoto(initial, maximum); }
    virtual std::string monoFont() const = 0;
    virtual int defaultEol() const = 0;
    virtual std::string stateDirectory() const { return {}; }
    virtual bool askReloadExternal(const std::string &message) { alert(message); return false; }
    virtual bool askOverwriteExternal(const std::string &message) { alert(message); return false; }
    virtual void setFullScreen(bool) {}
    virtual void setAlwaysOnTop(bool) {}
    virtual void quit() = 0;
};

class App {
public:
    explicit App(Host &host);
    ~App();
    App(const App &) = delete;
    App &operator=(const App &) = delete;

    void start();
    void refreshView();
    void checkpoint();
    void pollFiles();
    const std::vector<std::string> &recentFiles() const { return recentFiles_; }
    std::vector<std::string> documentNames() const;
    bool monitoring() const;
    void openPath(const std::string &path);
    void onCommand(Command command);
    void onNotify(int doc, const SCNotification *notification);
    void onTabChanged(int doc);
    bool onCloseRequested();

    int fontSize() const { return fontSize_; }
    int fontWeight() const { return fontWeight_; }
    bool macroRecording() const { return recordingDoc_ != 0; }
    bool autoComplete() const { return autoComplete_; }
    bool fontItalic() const { return fontItalic_; }
    bool fullScreen() const { return fullScreen_; }
    bool alwaysOnTop() const { return alwaysOnTop_; }
    int languageSelection() const;
    bool wrap() const { return wrap_; }
    bool lineNumbers() const { return lineNumbers_; }
    bool whitespace() const { return whitespace_; }
    bool eolVisible() const { return eolVisible_; }

private:
    struct Doc {
        int id = 0;
        std::string path;
        bool modified = false;
        std::string lexer = "text";
        std::string keywords;
        int language = -1;
        bool monitoring = false, monitorReadOnly = false;
        bool diskKnown = false, diskChanged = false;
        int64_t diskTime = 0;
        uintmax_t diskSize = 0;
        std::string encoding = "UTF-8";
        bool transient = false;
        std::string savedEncoding = "UTF-8";
    };

    Host &host_;
    std::vector<Doc> docs_;
    int active_ = -1;
    FindRequest find_{};
    bool fullScreen_ = false;
    bool alwaysOnTop_ = false;
    bool wrap_ = false;
    bool lineNumbers_ = true;
    bool whitespace_ = false;
    bool eolVisible_ = false;
    intptr_t lastZeroMatch_ = -1;
    int lastFindDoc_ = 0;
    int zoom_ = 0;
    int fontSize_ = 12;
    int fontWeight_ = 400;
    bool fontItalic_ = false;

    struct SearchHit { int doc; intptr_t start; intptr_t end; std::string path; size_t contentHash = 0; };
    struct MacroStep { unsigned message; uintptr_t w; intptr_t l; std::string text; };
    std::vector<MacroStep> macro_;
    int recordingDoc_ = 0;
    bool playingMacro_ = false;
    size_t macroBytes_ = 0;
    bool autoComplete_ = true;
    void recordMacro(int doc, const SCNotification &notification);
    void stopMacro();
    void playMacro(int repetitions = 1, bool untilEnd = false);
    void saveMacro();
    void loadMacro();
    void completeWord(bool automatic);
    void findInFolder();
    void navigateResult(bool forward);
    bool referenceCommand(Command command);
    void reloadActive(bool discardConfirmed = false);
    std::vector<std::string> recentFiles_, closedFiles_;
    bool pollingFiles_ = false;
    void rememberFile(const std::string &path);
    void observeDisk(Doc &doc);
    bool diskChanged(const Doc &doc) const;
    void toggleMonitoring();
    void saveCopy();
    void closeGroup(Command command);
    void documentStatistics();
    void showSearchResults(const std::string &report, std::vector<SearchHit> hits);
    std::vector<SearchHit> searchHits_;
    int searchHitIndex_ = -1;
    bool restoring_ = false;
    intptr_t stateLock_ = -1;
    bool checkpointFailed_ = false;
    bool acquireStateLock();
    std::string documentText(int doc);
    bool writeState(const std::string &path, bool snapshot);
    bool readState(const std::string &path, bool confirm);
    void persistCleanState();
    bool extraCommand(Command command);
    void findAllOpen(bool markOnly, bool useRequest = false, bool currentOnly = false);
    void transformLines(Command command);
    Doc *active();
    Doc *findDoc(int id);
    intptr_t send(unsigned msg, uintptr_t w = 0, intptr_t l = 0);
    intptr_t sendTo(int doc, unsigned msg, uintptr_t w = 0, intptr_t l = 0);
    void configure(Doc &doc);
    void applyView(int doc);
    void applyTheme(int doc);
    void applyLexer(Doc &doc);
    void touchTitles();
    void updateStatus();
    bool save(Doc &doc, bool forceDialog);
    bool confirmClose(Doc &doc);
    void closeActive();
    void performFind(const FindRequest &request);
    bool findBounds(intptr_t &low, intptr_t &high);
    void countMatches();
    bool editingSearch_ = false;
    int scopeDoc_ = 0;
    intptr_t scopeStart_ = 0, scopeEnd_ = 0;
    void findAgain(bool forward);
    intptr_t replaceAll(bool quiet = false);
    std::string displayName(const Doc &doc) const;
};

struct ExtraCommand { Command command; const char *label; };
struct LanguageChoice { const char *label; const char *example; };
inline constexpr LanguageChoice languageChoices[] = {
    {"自动（根据扩展名）", ""}, {"普通文本", "file.txt"}, {"C / C++", "file.cpp"},
    {"Python", "file.py"}, {"JavaScript / TypeScript", "file.js"}, {"Rust", "file.rs"},
    {"JSON", "file.json"}, {"XML", "file.xml"}, {"HTML", "file.html"}, {"CSS", "file.css"},
    {"Markdown", "file.md"}, {"Shell", "file.sh"}, {"Lua", "file.lua"}, {"SQL", "file.sql"},
    {"YAML", "file.yaml"}, {"CMake", "CMakeLists.txt"}, {"Makefile", "Makefile"},
    {"Ruby", "file.rb"}, {"Perl", "file.pl"}, {"PHP", "file.php"}
};
inline constexpr int languageChoiceCount = sizeof(languageChoices) / sizeof(languageChoices[0]);
inline constexpr ExtraCommand extraCommands[] = {
    {CmdMonitorFile, "监视文件（只读跟随末尾）"}, {CmdReopenClosed, "重新打开最近关闭的文件"},
    {CmdFullScreen, "切换全屏（F11）"}, {CmdAlwaysOnTop, "总在最前"},
    {CmdFoldCurrent, "折叠当前层次"}, {CmdUnfoldCurrent, "展开当前层次"},
    {CmdHideLines, "隐藏选中行"}, {CmdShowAllLines, "显示所有隐藏行"},
    {CmdMacroRepeat, "重复运行宏..."}, {CmdMacroToEnd, "运行宏到文件尾"},
    {CmdEolsToSpaces, "换行符转空格"}, {CmdTrimAndJoin, "移除行首尾空格并将换行转空格"},
    {CmdSpacesToTabs, "空格转 TAB（全部）"}, {CmdIndentToTabs, "空格转 TAB（行首）"},
    {CmdNextTab, "下一个标签页"}, {CmdPrevTab, "上一个标签页"},
    {CmdCopyFullPath, "复制完整文件路径"}, {CmdCopyFileName, "复制文件名"}, {CmdCopyDirectory, "复制所在目录路径"},
    {CmdReload, "重新读取文件"}, {CmdSaveCopy, "拷贝另存为..."},
    {CmdCloseOthers, "关闭其他文档"}, {CmdCloseLeft, "关闭左侧文档"}, {CmdCloseRight, "关闭右侧文档"},
    {CmdReadOnly, "切换编辑器只读"}, {CmdStatistics, "文档统计..."},
    {CmdRemoveEmptyLines, "移除空行"}, {CmdRemoveBlankLines, "移除空行（包括空白字符）"},
    {CmdUniqueAdjacent, "删除连续的重复行"}, {CmdReverseLines, "逆序排列行"},
    {CmdSortLengthAsc, "按行长升序"}, {CmdSortLengthDesc, "按行长降序"},
    {CmdSortIgnoreCaseAsc, "忽略大小写升序"}, {CmdSortIgnoreCaseDesc, "忽略大小写降序"},
    {CmdTrimLeading, "移除行首空白"}, {CmdTrimBoth, "移除行首及行尾空白"},
    {CmdSelectNextOccurrence, "多重选取：下一处"}, {CmdSelectAllOccurrences, "多重选取：全部"},
    {CmdGotoBrace, "转到匹配括号"}, {CmdSelectBrace, "选中匹配括号之间的内容"},
    {CmdFindInFolder, "在文件夹中查找..."},
    {CmdMacroStart, "开始 / 停止录制宏"}, {CmdMacroStop, "停止录制宏"}, {CmdMacroPlay, "回放宏"},
    {CmdMacroSave, "保存宏..."}, {CmdMacroLoad, "载入宏..."},
    {CmdCompleteWord, "单词补全（Ctrl+Space）"}, {CmdAutoComplete, "切换自动补全"},
    {CmdSaveSession, "保存会话（包含未保存内容）"}, {CmdLoadSession, "载入会话..."},
    {CmdEncodingUtf8, "编码：UTF-8"}, {CmdEncodingUtf8Bom, "编码：UTF-8 BOM"},
    {CmdEncodingUtf16LE, "编码：UTF-16 LE"}, {CmdEncodingUtf16BE, "编码：UTF-16 BE"}, {CmdEncodingLatin1, "编码：Latin-1"},
    {CmdFindAllOpen, "在所有打开文档中查找..."}, {CmdNextResult, "下一个搜索结果"}, {CmdPrevResult, "上一个搜索结果"},
    {CmdMarkAll, "标记当前文档所有匹配..."}, {CmdClearMarks, "清除搜索标记"},
    {CmdSortAsc, "行排序：升序"}, {CmdSortDesc, "行排序：降序"}, {CmdUniqueLines, "删除重复行（保留首次）"},
    {CmdToggleComment, "切换行注释"}, {CmdTabsToSpaces, "制表符转换为空格"},
    {CmdSaveAll, "全部保存"}, {CmdCloseAll, "全部关闭"},
    {CmdDuplicateLine, "复制当前行 / 选区"}, {CmdDeleteLine, "删除当前行"},
    {CmdMoveLineUp, "上移选中行"}, {CmdMoveLineDown, "下移选中行"},
    {CmdUpperCase, "转为大写"}, {CmdLowerCase, "转为小写"},
    {CmdTrimTrailing, "删除行尾空白"},
    {CmdEolCRLF, "转换换行符：Windows (CRLF)"}, {CmdEolLF, "转换换行符：Unix (LF)"},
    {CmdEolCR, "转换换行符：Mac (CR)"},
    {CmdToggleBookmark, "切换当前行书签"}, {CmdNextBookmark, "下一个书签"},
    {CmdPrevBookmark, "上一个书签"}, {CmdClearBookmarks, "清除所有书签"},
    {CmdFoldAll, "折叠全部"}, {CmdUnfoldAll, "展开全部"}
};

inline const char *extraCommandGroup(Command command) {
    if (command == CmdMonitorFile || command == CmdReopenClosed) return "会话与文件";
    if (command >= CmdFullScreen && command <= CmdShowAllLines) return "视图";
    if (command == CmdMacroRepeat || command == CmdMacroToEnd) return "宏";
    if (command >= CmdNextTab && command <= CmdCopyDirectory) return "会话与文件";
    if (command >= CmdReload && command <= CmdCloseRight) return "会话与文件";
    if (command == CmdGotoBrace || command == CmdSelectBrace) return "搜索结果";
    if (command >= CmdMacroStart && command <= CmdMacroLoad) return "宏";
    if (command == CmdCompleteWord || command == CmdAutoComplete) return "代码补全";
    if (command == CmdFindInFolder) return "搜索结果";
    if (command >= CmdEncodingUtf8 && command <= CmdEncodingLatin1) return "编码";
    if (command >= CmdFindAllOpen && command <= CmdClearMarks) return "搜索结果";
    if (command == CmdSaveSession || command == CmdLoadSession || command == CmdSaveAll || command == CmdCloseAll) return "会话与文件";
    if (command >= CmdToggleBookmark && command <= CmdUnfoldAll) return "书签与折叠";
    return "文本处理";
}
