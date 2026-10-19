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

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTabViewDelegate, NSMenuDelegate, NSWindowDelegate>
@property(nonatomic, assign) MacHost *host;
- (NSInteger)lastDialogTag;
- (void)endDialog:(id)sender;
- (void)performSearchAction:(id)sender;
- (void)doCommand:(id)sender;
- (void)showTabMenu:(NSEvent *)event;
- (void)toolbarCommand:(id)sender;
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
    bool closeApproved = false;
    NSStackView *toolbar = nil;
    NSPanel *searchPanel = nil;
    NSPopUpButton *searchMode = nil, *searchSyntax = nil;
    NSTextField *findField = nil, *replaceField = nil, *searchStatus = nil;
    NSButton *matchCase = nil, *wholeWord = nil, *wrap = nil, *backwards = nil;
    NSButton *selection = nil, *dot = nil, *unicode = nil;
    FindRequest searchRequest{};
    std::function<void(const FindRequest &)> performSearch;

    Slot *slot(int id) {
        for (Slot &item : slots) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    NSTabViewItem *itemFor(int id) {
        for (NSTabViewItem *item in tabs.tabViewItems) {
            if ([item.identifier isKindOfClass:NSNumber.class] && [item.identifier intValue] == id) return item;
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
        NSTabViewItem *tab = [[NSTabViewItem alloc] initWithIdentifier:@(item.id)];
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

    bool showFindDialog(const FindRequest &seed, bool replaceMode,
                        const std::function<void(const FindRequest &)> &perform) override {
        performSearch = perform;
        if (!searchPanel) {
            searchPanel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 570, 340)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                backing:NSBackingStoreBuffered defer:NO];
            searchPanel.title = @"查找与替换";
            searchPanel.hidesOnDeactivate = NO;
            searchPanel.level = NSFloatingWindowLevel;
            NSView *content = searchPanel.contentView;
            searchMode = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(16, 300, 180, 26) pullsDown:NO];
            [searchMode addItemsWithTitles:@[@"查找", @"替换", @"标记"]];
            [content addSubview:searchMode];
            NSTextField *findLabel = [NSTextField labelWithString:@"查找目标："];
            findLabel.frame = NSMakeRect(16, 263, 76, 22); [content addSubview:findLabel];
            findField = [[NSTextField alloc] initWithFrame:NSMakeRect(96, 262, 458, 24)]; [content addSubview:findField];
            NSTextField *replaceLabel = [NSTextField labelWithString:@"替换为："];
            replaceLabel.frame = NSMakeRect(16, 229, 76, 22); [content addSubview:replaceLabel];
            replaceField = [[NSTextField alloc] initWithFrame:NSMakeRect(96, 228, 458, 24)]; [content addSubview:replaceField];
            auto box = [&](NSString *title, CGFloat x, CGFloat y, CGFloat width) {
                NSButton *button = [NSButton checkboxWithTitle:title target:nil action:nil];
                button.frame = NSMakeRect(x, y, width, 22); [content addSubview:button]; return button;
            };
            matchCase = box(@"匹配大小写", 16, 190, 140);
            wholeWord = box(@"全词匹配", 170, 190, 120);
            wrap = box(@"循环查找", 310, 190, 120);
            backwards = box(@"反向查找", 16, 160, 140);
            selection = box(@"选区范围", 170, 160, 120);
            dot = box(@". 匹配换行", 310, 160, 130);
            searchSyntax = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(16, 120, 190, 26) pullsDown:NO];
            [searchSyntax addItemsWithTitles:@[@"普通", @"扩展", @"正则"]];
            searchSyntax.target = delegate; searchSyntax.action = @selector(doCommand:); searchSyntax.tag = 990;
            [content addSubview:searchSyntax];
            unicode = box(@"完整 Unicode（ICU 属性、折叠、字素簇）", 220, 120, 330);
            searchStatus = [NSTextField labelWithString:@"正则替换支持 \\1、$1、$+{name}。"];
            searchStatus.frame = NSMakeRect(16, 88, 538, 20); [content addSubview:searchStatus];
            const struct { NSString *title; NSInteger tag; CGFloat x; CGFloat y; } actions[] = {
                {@"查找下一个", 1, 16, 48}, {@"查找上一个", 2, 112, 48}, {@"计数", 3, 208, 48},
                {@"查找当前文档", 4, 304, 48}, {@"查找所有文档", 5, 424, 48},
                {@"替换", 6, 16, 12}, {@"全部替换", 7, 112, 12},
                {@"替换打开文档", 8, 208, 12}, {@"标记全部", 9, 328, 12}, {@"清除标记", 10, 424, 12}};
            for (const auto &entry : actions) {
                NSButton *button = [NSButton buttonWithTitle:entry.title target:delegate action:@selector(performSearchAction:)];
                button.tag = entry.tag; button.frame = NSMakeRect(entry.x, entry.y, 116, 28); [content addSubview:button];
            }
            [searchPanel center];
        }
        if (!searchPanel.isVisible || seed.find != searchRequest.find) {
            searchRequest = seed;
            findField.stringValue = toNS(seed.find); replaceField.stringValue = toNS(seed.replace);
            matchCase.state = seed.matchCase ? NSControlStateValueOn : NSControlStateValueOff;
            wholeWord.state = seed.wholeWord ? NSControlStateValueOn : NSControlStateValueOff;
            wrap.state = seed.wrapAround ? NSControlStateValueOn : NSControlStateValueOff;
            backwards.state = seed.backwards ? NSControlStateValueOn : NSControlStateValueOff;
            selection.state = seed.inSelection ? NSControlStateValueOn : NSControlStateValueOff;
            dot.state = seed.dotMatchesNewline ? NSControlStateValueOn : NSControlStateValueOff;
            unicode.state = seed.unicode ? NSControlStateValueOn : NSControlStateValueOff;
            [searchSyntax selectItemAtIndex:seed.regex ? 2 : seed.extended ? 1 : 0];
        }
        [searchMode selectItemAtIndex:replaceMode ? 1 : 0];
        [NSApp activateIgnoringOtherApps:YES]; [searchPanel makeKeyAndOrderFront:nil];
        [searchPanel makeFirstResponder:findField];
        return true;
    }

    void performFindAction(NSInteger tag) {
        searchRequest.find = fromNS(findField.stringValue); searchRequest.replace = fromNS(replaceField.stringValue);
        searchRequest.matchCase = matchCase.state == NSControlStateValueOn;
        searchRequest.wholeWord = wholeWord.state == NSControlStateValueOn;
        searchRequest.wrapAround = wrap.state == NSControlStateValueOn;
        searchRequest.backwards = backwards.state == NSControlStateValueOn;
        searchRequest.inSelection = selection.state == NSControlStateValueOn;
        searchRequest.dotMatchesNewline = dot.state == NSControlStateValueOn;
        searchRequest.unicode = unicode.state == NSControlStateValueOn;
        searchRequest.cancelled = searchRequest.find.empty();
        const FindAction actions[] = {FindAction::Next, FindAction::Previous, FindAction::Count,
            FindAction::FindAllCurrent, FindAction::FindAllOpen, FindAction::ReplaceOne,
            FindAction::ReplaceAll, FindAction::ReplaceAllOpen, FindAction::MarkAll, FindAction::ClearMarks};
        if (tag >= 1 && tag <= 10) {
            searchRequest.action = actions[tag - 1];
            if (performSearch && !searchRequest.cancelled) performSearch(searchRequest);
        }
    }

    void selectSearchSyntax() {
        const NSInteger mode = searchSyntax.indexOfSelectedItem;
        searchRequest.regex = mode == 2; searchRequest.extended = mode == 1;
        dot.enabled = searchRequest.regex; unicode.enabled = searchRequest.regex;
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

- (void)performSearchAction:(id)sender {
    if (_hostObject) _hostObject->performFindAction([sender tag]);
}

- (void)toolbarCommand:(id)sender {
    if (_app) _app->onCommand(static_cast<Command>([sender tag]));
}

- (void)doCommand:(id)sender {
    if ([sender tag] == 990) { if (_hostObject) _hostObject->selectSearchSyntax(); return; }
    if (_app) _app->onCommand(static_cast<Command>([sender tag]));
}

- (void)showTabMenu:(NSEvent *)event {
    if (!_hostObject || !_app) return;
    NSView *view = event.window.contentView;
    NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
    NSTabViewItem *tab = [_hostObject->tabs tabViewItemAtPoint:[_hostObject->tabs convertPoint:point fromView:view]];
    if (tab && [tab.identifier isKindOfClass:NSNumber.class]) _app->onTabChanged([tab.identifier intValue]);
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"文档"];
    const struct { NSString *title; Command command; } entries[] = {
        {@"保存", CmdSave}, {@"另存为...", CmdSaveAs}, {@"拷贝另存为...", CmdSaveCopy},
        {@"重新读取文件", CmdReload}, {@"编辑器只读", CmdReadOnly}, {@"关闭", CmdClose},
        {@"关闭其他文档", CmdCloseOthers}, {@"关闭左侧文档", CmdCloseLeft}, {@"关闭右侧文档", CmdCloseRight},
        {@"复制完整文件路径", CmdCopyFullPath}, {@"复制文件名", CmdCopyFileName}, {@"复制所在目录路径", CmdCopyDirectory}};
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        if (i == 5 || i == 9) [menu addItem:NSMenuItem.separatorItem];
        [menu addItem:[self item:entries[i].title command:entries[i].command key:@"" mods:0]];
    }
    [NSMenu popUpContextMenu:menu withEvent:event forView:view];
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
    _hostObject->window.delegate = self;
    NSView *content = _hostObject->window.contentView;
    _hostObject->toolbar = [[NSStackView alloc] initWithFrame:NSZeroRect];
    _hostObject->toolbar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    _hostObject->toolbar.alignment = NSLayoutAttributeCenterY;
    _hostObject->toolbar.distribution = NSStackViewDistributionFill;
    _hostObject->toolbar.spacing = 6;
    _hostObject->toolbar.edgeInsets = NSEdgeInsetsMake(4, 8, 4, 8);
    const struct { NSString *title; Command command; } toolbarItems[] = {
        {@"新建", CmdNew}, {@"打开", CmdOpen}, {@"保存", CmdSave}, {@"全部保存", CmdSaveAll},
        {@"撤销", CmdUndo}, {@"重做", CmdRedo}, {@"查找", CmdFind}, {@"替换", CmdReplace},
        {@"文件夹搜索", CmdFindInFolder}, {@"只读", CmdReadOnly}};
    for (const auto &entry : toolbarItems) {
        NSButton *button = [NSButton buttonWithTitle:entry.title target:self action:@selector(toolbarCommand:)];
        button.tag = entry.command;
        button.bezelStyle = NSBezelStyleToolbar;
        [button setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
        [_hostObject->toolbar addArrangedSubview:button];
    }
    _hostObject->toolbar.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:_hostObject->toolbar];
    _hostObject->tabs = [[NSTabView alloc] initWithFrame:NSZeroRect];
    _hostObject->tabs.translatesAutoresizingMaskIntoConstraints = NO;
    _hostObject->tabs.delegate = self;
    NSClickGestureRecognizer *rightClick = [[NSClickGestureRecognizer alloc] initWithTarget:self action:@selector(showTabMenu:)];
    rightClick.buttonMask = 0x2;
    [_hostObject->tabs addGestureRecognizer:rightClick];
    _hostObject->status = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _hostObject->status.translatesAutoresizingMaskIntoConstraints = NO;
    _hostObject->status.editable = NO;
    _hostObject->status.bezeled = NO;
    _hostObject->status.drawsBackground = NO;
    [content addSubview:_hostObject->tabs];
    [content addSubview:_hostObject->status];
    [NSLayoutConstraint activateConstraints:@[
        [_hostObject->toolbar.topAnchor constraintEqualToAnchor:content.topAnchor],
        [_hostObject->toolbar.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [_hostObject->toolbar.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [_hostObject->toolbar.heightAnchor constraintEqualToConstant:42],
        [_hostObject->tabs.topAnchor constraintEqualToAnchor:_hostObject->toolbar.bottomAnchor],
        [_hostObject->tabs.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [_hostObject->tabs.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [_hostObject->tabs.bottomAnchor constraintEqualToAnchor:_hostObject->status.topAnchor],
        [_hostObject->status.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:8],
        [_hostObject->status.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-8],
        [_hostObject->status.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-2],
        [_hostObject->status.heightAnchor constraintEqualToConstant:20]]];

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
    const bool documents = [menu.title isEqualToString:@"窗口"];
    const auto names = recent ? _app->recentFiles() : _app->documentNames();
    [menu removeAllItems];
    for (size_t i=0; i<names.size() && i<1000; ++i)
        [menu addItem:[self item:toNS(names[i]) command:static_cast<Command>((recent ? CmdRecentBase : CmdDocumentBase)+i) key:@"" mods:0]];
    (void)documents;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    if (_hostObject && _hostObject->closeApproved) return NSTerminateNow;
    if (_app && !_app->onCloseRequested()) return NSTerminateCancel;
    return NSTerminateNow;
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    if (sender != _hostObject->window || !_app) return YES;
    if (!_app->onCloseRequested()) return NO;
    _hostObject->closeApproved = true;
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    if (notification.object == _hostObject->window) [NSApp terminate:nil];
}

- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)tabViewItem {
    (void)tabView;
    if (_app && [tabViewItem.identifier isKindOfClass:NSNumber.class]) _app->onTabChanged([tabViewItem.identifier intValue]);
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
        NSURL *fonts = [[NSBundle mainBundle].resourceURL URLByAppendingPathComponent:@"fonts"];
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
