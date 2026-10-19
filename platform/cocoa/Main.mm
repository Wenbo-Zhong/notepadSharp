#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>

#include "App.h"
#include "Scintilla.h"
#include "ScintillaView.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string fromNS(NSString *text) {
    return text ? std::string(text.UTF8String) : std::string();
}

NSString *toNS(const std::string &text) {
    return [NSString stringWithUTF8String:text.c_str()];
}

struct Slot {
    int id = 0;
    ScintillaView *editor = nil;
};

}  // namespace

class MacHost;

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTabViewDelegate, NSMenuDelegate>
@property(nonatomic, assign) MacHost *host;
- (NSInteger)lastDialogTag;
- (void)endDialog:(id)sender;
@end



class MacHost : public Host {
public:
    NSWindow *window = nil;
    NSTabView *tabs = nil;
    NSTextField *status = nil;
    AppDelegate *delegate = nil;
    App *app = nullptr;
    int nextId = 1;
    std::vector<Slot> slots;

    Slot *slot(int id) {
        for (Slot &item : slots) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    NSTabViewItem *itemFor(int id) {
        for (NSTabViewItem *item in tabs.tabViewItems) {
            if (static_cast<int>(reinterpret_cast<intptr_t>(item.identifier)) == id) return item;
        }
        return nil;
    }

    intptr_t send(int doc, unsigned msg, uintptr_t wParam, intptr_t lParam) override {
        Slot *item = slot(doc);
        if (!item) return 0;
        return [item->editor message:msg wParam:wParam lParam:lParam];
    }

    int createEditor() override {
        Slot item;
        item.id = nextId++;
        item.editor = [[ScintillaView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
        const int docId = item.id;
        SciNotifyFunc notify = [](intptr_t windowid, unsigned int message, uintptr_t, uintptr_t lParam) {
            AppDelegate *delegate = (AppDelegate *)NSApp.delegate;
            if (message == WM_NOTIFY && delegate.host && delegate.host->app) {
                delegate.host->app->onNotify(static_cast<int>(windowid), reinterpret_cast<SCNotification *>(lParam));
            }
        };
        [item.editor registerNotifyCallback:docId value:notify];
        NSTabViewItem *tab = [[NSTabViewItem alloc] initWithIdentifier:reinterpret_cast<id>(static_cast<intptr_t>(item.id))];
        tab.label = @"未命名";
        tab.view = item.editor;
        [tabs addTabViewItem:tab];
        [tabs selectTabViewItem:tab];
        slots.push_back(item);
        return item.id;
    }

    void destroyEditor(int doc) override {
        if (NSTabViewItem *item = itemFor(doc)) {
            [tabs removeTabViewItem:item];
        }
        slots.erase(std::remove_if(slots.begin(), slots.end(), [&](const Slot &item) { return item.id == doc; }), slots.end());
    }

    void showEditor(int doc) override {
        if (NSTabViewItem *item = itemFor(doc)) {
            [tabs selectTabViewItem:item];
            if (Slot *editor = slot(doc)) {
                [window makeFirstResponder:editor->editor];
            }
        }
    }

    void setWindowTitle(const std::string &title) override {
        window.title = toNS(title);
    }

    void setStatus(const std::string &text) override {
        status.stringValue = toNS(text);
    }

    void setTabTitle(int doc, const std::string &title) override {
        if (NSTabViewItem *item = itemFor(doc)) {
            item.label = toNS(title);
        }
    }

    std::string choosePath(bool save, const std::string &suggested) {
        NSSavePanel *panel = save ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
        if (!save) {
            ((NSOpenPanel *)panel).canChooseFiles = YES;
            ((NSOpenPanel *)panel).canChooseDirectories = NO;
        }
        if (save && !suggested.empty()) {
            panel.nameFieldStringValue = toNS(suggested);
        }
        if ([panel runModal] != NSModalResponseOK) return {};
        return fromNS(panel.URL.path);
    }

    std::string promptDirectory() override {
        NSOpenPanel *panel = [NSOpenPanel openPanel]; panel.canChooseFiles = NO; panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO; panel.title = @"选择搜索文件夹";
        return [panel runModal] == NSModalResponseOK ? fromNS(panel.URL.path) : std::string();
    }
    std::string promptOpenPath() override { return choosePath(false, ""); }
    std::string promptSavePath(const std::string &suggested) override { return choosePath(true, suggested); }

    SaveChoice askSave(const std::string &name) override {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = toNS("是否保存对“" + name + "”的更改？");
        [alert addButtonWithTitle:@"保存"];
        [alert addButtonWithTitle:@"不保存"];
        [alert addButtonWithTitle:@"取消"];
        const NSModalResponse response = [alert runModal];
        if (response == NSAlertFirstButtonReturn) return SaveChoice::Save;
        if (response == NSAlertSecondButtonReturn) return SaveChoice::Discard;
        return SaveChoice::Cancel;
    }

    void alert(const std::string &message) override {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = toNS(message);
        [alert runModal];
    }

    FindRequest promptFind(const FindRequest &seed, bool replaceMode) override {
        FindRequest request = seed;
        request.cancelled = true;
        NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 380, replaceMode ? 230 : 194)
                                                      styleMask:NSWindowStyleMaskTitled
                                                        backing:NSBackingStoreBuffered
                                                          defer:NO];
        panel.title = replaceMode ? @"替换" : @"查找";
        NSTextField *findLabel = [NSTextField labelWithString:@"查找"];
        findLabel.frame = NSMakeRect(12, replaceMode ? 190 : 154, 50, 22);
        NSTextField *find = [[NSTextField alloc] initWithFrame:NSMakeRect(70, replaceMode ? 190 : 154, 290, 24)];
        find.stringValue = toNS(seed.find);
        NSTextField *repl = nil;
        if (replaceMode) {
            NSTextField *replLabel = [NSTextField labelWithString:@"替换"];
            replLabel.frame = NSMakeRect(12, 158, 50, 22);
            repl = [[NSTextField alloc] initWithFrame:NSMakeRect(70, 158, 290, 24)];
            repl.stringValue = toNS(seed.replace);
            [panel.contentView addSubview:replLabel];
            [panel.contentView addSubview:repl];
        }
        NSButton *matchCase = [NSButton checkboxWithTitle:@"区分大小写" target:nil action:nil];
        matchCase.frame = NSMakeRect(12, replaceMode ? 126 : 118, 140, 22);
        matchCase.state = seed.matchCase ? NSControlStateValueOn : NSControlStateValueOff;
        NSButton *wholeWord = [NSButton checkboxWithTitle:@"全字匹配" target:nil action:nil];
        wholeWord.frame = NSMakeRect(170, replaceMode ? 126 : 118, 140, 22);
        wholeWord.state = seed.wholeWord ? NSControlStateValueOn : NSControlStateValueOff;
        NSButton *regex = [NSButton checkboxWithTitle:@"正则表达式（替换引用：\\1）" target:nil action:nil];
        regex.frame = NSMakeRect(12, 88, 340, 22);
        regex.state = seed.regex ? NSControlStateValueOn : NSControlStateValueOff;
        [panel.contentView addSubview:regex];
        NSButton *unicode = [NSButton checkboxWithTitle:@"完整 Unicode（ICU，需启用正则）" target:nil action:nil];
        unicode.frame = NSMakeRect(12, 52, 350, 22);
        unicode.state = seed.unicode ? NSControlStateValueOn : NSControlStateValueOff;
        [panel.contentView addSubview:unicode];
        auto button = [&](NSString *title, int tag, CGFloat x) {
            NSButton *control = [NSButton buttonWithTitle:title target:delegate action:@selector(endDialog:)];
            control.tag = tag;
            control.frame = NSMakeRect(x, 16, 86, 28);
            [panel.contentView addSubview:control];
        };
        button(@"查找下一个", 1, 12);
        if (replaceMode) {
            button(@"替换", 2, 104);
            button(@"全部替换", 3, 196);
            button(@"取消", 0, 288);
        } else {
            button(@"取消", 0, 110);
        }
        [panel.contentView addSubview:findLabel];
        [panel.contentView addSubview:find];
        [panel.contentView addSubview:matchCase];
        [panel.contentView addSubview:wholeWord];
        delegate.host = this;
        [NSApp runModalForWindow:panel];
        const NSInteger tag = delegate.lastDialogTag;
        if (tag > 0) {
            request.find = fromNS(find.stringValue);
            request.replace = repl ? fromNS(repl.stringValue) : "";
            request.matchCase = matchCase.state == NSControlStateValueOn;
            request.wholeWord = wholeWord.state == NSControlStateValueOn;
            request.regex = regex.state == NSControlStateValueOn;
            request.unicode = unicode.state == NSControlStateValueOn;
            request.action = tag == 3 ? FindAction::ReplaceAll : tag == 2 ? FindAction::ReplaceOne : FindAction::Next;
            request.cancelled = request.find.empty();
        }
        [panel orderOut:nil];
        return request;
    }

    int promptGoto(int currentLine, int lineCount) override { return promptNumber("转到行", currentLine, lineCount); }
    int promptNumber(const std::string &title, int currentLine, int lineCount) override {
        NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 260, 110)
                                                      styleMask:NSWindowStyleMaskTitled
                                                        backing:NSBackingStoreBuffered
                                                          defer:NO];
        panel.title = toNS(title + " (1-" + std::to_string(lineCount) + ")");
        NSTextField *entry = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 58, 230, 24)];
        entry.stringValue = toNS(std::to_string(currentLine));
        NSButton *ok = [NSButton buttonWithTitle:@"确定" target:delegate action:@selector(endDialog:)];
        ok.tag = 1;
        ok.frame = NSMakeRect(12, 16, 90, 28);
        NSButton *cancel = [NSButton buttonWithTitle:@"取消" target:delegate action:@selector(endDialog:)];
        cancel.tag = 0;
        cancel.frame = NSMakeRect(150, 16, 90, 28);
        [panel.contentView addSubview:entry];
        [panel.contentView addSubview:ok];
        [panel.contentView addSubview:cancel];
        [NSApp runModalForWindow:panel];
        int line = 0;
        if (delegate.lastDialogTag == 1) line = entry.integerValue;
        [panel orderOut:nil];
        return line;
    }

    void setFullScreen(bool enabled) override { if (((window.styleMask & NSWindowStyleMaskFullScreen) != 0) != enabled) [window toggleFullScreen:nil]; }
    void setAlwaysOnTop(bool enabled) override { window.level = enabled ? NSFloatingWindowLevel : NSNormalWindowLevel; }
    std::string stateDirectory() const override {
        NSString *base = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
        return fromNS([base stringByAppendingPathComponent:@"NotepadSharp"]);
    }
    bool askReloadExternal(const std::string &message) override {
        NSAlert *alert = [[NSAlert alloc] init]; alert.messageText = toNS(message);
        [alert addButtonWithTitle:@"保留当前内容"]; [alert addButtonWithTitle:@"确认"];
        return [alert runModal] == NSAlertSecondButtonReturn;
    }
    bool askOverwriteExternal(const std::string &message) override { return askReloadExternal(message); }
    std::string monoFont() const override { return "JetBrains Mono"; }
    int defaultEol() const override { return SC_EOL_LF; }

    void quit() override {
        [NSApp terminate:nil];
    }
};

@implementation AppDelegate {
    MacHost *_hostObject;
    App *_app;
    NSInteger _lastDialogTag;
}
@synthesize host = _hostObject;

- (NSInteger)lastDialogTag { return _lastDialogTag; }

- (void)endDialog:(id)sender {
    _lastDialogTag = [sender tag];
    [NSApp stopModal];
}

- (void)doCommand:(id)sender {
    if (_app) _app->onCommand(static_cast<Command>([sender tag]));
}

- (NSMenuItem *)item:(NSString *)title command:(Command)command key:(NSString *)key mods:(NSEventModifierFlags)mods {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(doCommand:) keyEquivalent:key];
    item.keyEquivalentModifierMask = mods;
    item.target = self;
    item.tag = command;
    return item;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    _hostObject = new MacHost();
    _hostObject->delegate = self;
    NSRect frame = NSMakeRect(0, 0, 960, 640);
    _hostObject->window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
    _hostObject->tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 24, 960, 616)];
    _hostObject->tabs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _hostObject->tabs.delegate = self;
    _hostObject->status = [[NSTextField alloc] initWithFrame:NSMakeRect(8, 2, 940, 20)];
    _hostObject->status.editable = NO;
    _hostObject->status.bezeled = NO;
    _hostObject->status.drawsBackground = NO;
    _hostObject->status.autoresizingMask = NSViewWidthSizable;
    [_hostObject->window.contentView addSubview:_hostObject->tabs];
    [_hostObject->window.contentView addSubview:_hostObject->status];

    NSMenu *menubar = [[NSMenu alloc] init];
    auto top = [&](NSString *title, NSMenu *menu) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        item.submenu = menu;
        [menubar addItem:item];
    };
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"NotepadSharp"];
    [appMenu addItem:[self item:@"退出" command:CmdExit key:@"q" mods:NSEventModifierFlagCommand]];
    top(@"NotepadSharp", appMenu);
    NSMenu *file = [[NSMenu alloc] initWithTitle:@"文件"];
    [file addItem:[self item:@"新建" command:CmdNew key:@"n" mods:NSEventModifierFlagCommand]];
    [file addItem:[self item:@"打开..." command:CmdOpen key:@"o" mods:NSEventModifierFlagCommand]];
    [file addItem:[self item:@"保存" command:CmdSave key:@"s" mods:NSEventModifierFlagCommand]];
    [file addItem:[self item:@"另存为..." command:CmdSaveAs key:@"S" mods:NSEventModifierFlagCommand | NSEventModifierFlagShift]];
    [file addItem:[self item:@"关闭" command:CmdClose key:@"w" mods:NSEventModifierFlagCommand]];
    NSMenuItem *recentItem = [[NSMenuItem alloc] initWithTitle:@"最近文件" action:nil keyEquivalent:@""];
    NSMenu *recent = [[NSMenu alloc] initWithTitle:@"最近文件"]; recent.delegate = self; recentItem.submenu = recent; [file addItem:recentItem];
    top(@"文件", file);
    NSMenu *documents = [[NSMenu alloc] initWithTitle:@"窗口"]; documents.delegate = self; top(@"窗口", documents);
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"编辑"];
    [edit addItem:[self item:@"撤销" command:CmdUndo key:@"z" mods:NSEventModifierFlagCommand]];
    [edit addItem:[self item:@"重做" command:CmdRedo key:@"y" mods:NSEventModifierFlagCommand]];
    [edit addItem:[self item:@"剪切" command:CmdCut key:@"x" mods:NSEventModifierFlagCommand]];
    [edit addItem:[self item:@"复制" command:CmdCopy key:@"c" mods:NSEventModifierFlagCommand]];
    [edit addItem:[self item:@"粘贴" command:CmdPaste key:@"v" mods:NSEventModifierFlagCommand]];
    [edit addItem:[self item:@"删除" command:CmdDelete key:@"" mods:0]];
    [edit addItem:[self item:@"全选" command:CmdSelectAll key:@"a" mods:NSEventModifierFlagCommand]];
    top(@"编辑", edit);
    NSMenu *search = [[NSMenu alloc] initWithTitle:@"搜索"];
    [search addItem:[self item:@"查找..." command:CmdFind key:@"f" mods:NSEventModifierFlagCommand]];
    [search addItem:[self item:@"查找下一个" command:CmdFindNext key:@"g" mods:NSEventModifierFlagCommand]];
    [search addItem:[self item:@"查找上一个" command:CmdFindPrev key:@"G" mods:NSEventModifierFlagCommand | NSEventModifierFlagShift]];
    [search addItem:[self item:@"替换..." command:CmdReplace key:@"h" mods:NSEventModifierFlagCommand]];
    [search addItem:[self item:@"转到行..." command:CmdGoto key:@"l" mods:NSEventModifierFlagCommand]];
    top(@"搜索", search);
    NSMenu *view = [[NSMenu alloc] initWithTitle:@"视图"];
    [view addItem:[self item:@"自动换行" command:CmdWrap key:@"" mods:0]];
    [view addItem:[self item:@"行号" command:CmdLineNumbers key:@"" mods:0]];
    [view addItem:[self item:@"空白字符" command:CmdWhitespace key:@"" mods:0]];
    [view addItem:[self item:@"换行符" command:CmdEol key:@"" mods:0]];
    [view addItem:[self item:@"放大" command:CmdZoomIn key:@"=" mods:NSEventModifierFlagCommand]];
    [view addItem:[self item:@"缩小" command:CmdZoomOut key:@"-" mods:NSEventModifierFlagCommand]];
    [view addItem:[self item:@"重置缩放" command:CmdZoomReset key:@"0" mods:NSEventModifierFlagCommand]];
    top(@"视图", view);
    NSMenu *tools = [[NSMenu alloc] initWithTitle:@"代码工具"];
    for (const char *group : {"会话与文件", "编码", "搜索结果", "文本处理", "书签与折叠", "宏", "代码补全", "视图"}) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:toNS(group) action:nil keyEquivalent:@""];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:toNS(group)]; item.submenu = submenu; [tools addItem:item];
        for (const auto &entry : extraCommands) if (std::strcmp(extraCommandGroup(entry.command), group) == 0) {
            NSString *key = entry.command == CmdCompleteWord ? @" " : @"";
            NSEventModifierFlags mods = entry.command == CmdCompleteWord ? NSEventModifierFlagControl : 0;
            [submenu addItem:[self item:toNS(entry.label) command:entry.command key:key mods:mods]];
        }
    }
    top(@"代码工具", tools);
    NSMenu *languages = [[NSMenu alloc] initWithTitle:@"语言"];
    for (int i=0; i<languageChoiceCount; ++i) [languages addItem:[self item:toNS(languageChoices[i].label) command:static_cast<Command>(CmdLanguageBase+i) key:@"" mods:0]];
    top(@"语言", languages);
    NSMenu *font = [[NSMenu alloc] initWithTitle:@"字体"];
    for (int size = 6; size <= 72; ++size) [font addItem:[self item:toNS(std::to_string(size) + " pt") command:static_cast<Command>(CmdFontSizeBase + size) key:@"" mods:0]];
    for (int i = 0; i < 8; ++i) [font addItem:[self item:toNS("Weight " + std::to_string(100 * (i+1))) command:static_cast<Command>(CmdFontWeightBase + i) key:@"" mods:0]];
    [font addItem:[self item:@"切换斜体" command:CmdFontItalic key:@"" mods:0]];
    top(@"字体", font);
    NSApp.mainMenu = menubar;

    _app = new App(*_hostObject);
    _hostObject->app = _app;
    _app->start();
    [NSTimer scheduledTimerWithTimeInterval:2 repeats:YES block:^(NSTimer *) { if (self->_app) self->_app->pollFiles(); }];
    [NSTimer scheduledTimerWithTimeInterval:30 repeats:YES block:^(NSTimer *) { if (self->_app) self->_app->checkpoint(); }];
    NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
    if (args.count > 1) _app->openPath(fromNS(args[1]));
    [_hostObject->window center];
    [_hostObject->window makeKeyAndOrderFront:nil];
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    if (!_app) return;
    const bool recent = [menu.title isEqualToString:@"最近文件"];
    const auto names = recent ? _app->recentFiles() : _app->documentNames();
    [menu removeAllItems];
    for (size_t i=0; i<names.size() && i<1000; ++i) [menu addItem:[self item:toNS(names[i]) command:static_cast<Command>((recent ? CmdRecentBase : CmdDocumentBase)+i) key:@"" mods:0]];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    if (_app && !_app->onCloseRequested()) return NSTerminateCancel;
    return NSTerminateNow;
}

- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)tabViewItem {
    (void)tabView;
    if (_app) _app->onTabChanged(static_cast<int>(reinterpret_cast<intptr_t>(tabViewItem.identifier)));
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app {
    (void)app;
    return YES;
}
@end

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSString *directory = [[NSProcessInfo.processInfo.arguments[0] stringByStandardizingPath] stringByDeletingLastPathComponent];
        NSURL *fonts = [NSURL fileURLWithPath:[directory stringByAppendingPathComponent:@"fonts"]];
        for (NSURL *url in [[NSFileManager defaultManager] contentsOfDirectoryAtURL:fonts includingPropertiesForKeys:nil options:0 error:nil]) {
            if ([url.pathExtension isEqualToString:@"ttf"]) CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url, kCTFontManagerScopeProcess, nullptr);
        }
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        application.delegate = delegate;
        [application activateIgnoringOtherApps:YES];
        [application run];
    }
    return 0;
}
