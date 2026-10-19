#include <gtk/gtk.h>
#include <fontconfig/fontconfig.h>
#include <filesystem>

#include "App.h"
#include "Scintilla.h"
#include "ScintillaWidget.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string fromUtf8(const char *text) {
    return text ? text : "";
}

struct Slot {
    int id = 0;
    GtkWidget *editor = nullptr;
    GtkWidget *label = nullptr;
};

class GtkHost : public Host {
public:
    GtkWidget *window = nullptr;
    GtkWidget *notebook = nullptr;
    GtkWidget *status = nullptr;
    App *app = nullptr;
    int nextId = 1;
    std::vector<Slot> slots;
    bool quitting = false;

    Slot *slot(int id) {
        for (Slot &item : slots) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    int pageOf(int id) const {
        for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)); ++i) {
            if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i)), "doc")) == id) {
                return i;
            }
        }
        return -1;
    }

    intptr_t send(int doc, unsigned msg, uintptr_t wParam, intptr_t lParam) override {
        Slot *item = slot(doc);
        if (!item) return 0;
        return scintilla_send_message(SCINTILLA(item->editor), msg, wParam, lParam);
    }

    int createEditor() override {
        Slot item;
        item.id = nextId++;
        item.editor = scintilla_new();
        item.label = gtk_label_new("未命名");
        g_object_set_data(G_OBJECT(item.editor), "doc", GINT_TO_POINTER(item.id));
        gtk_widget_show(item.editor);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), item.editor, item.label);
        g_signal_connect(item.editor, SCINTILLA_NOTIFY, G_CALLBACK(onNotify), this);
        slots.push_back(item);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), pageOf(item.id));
        return item.id;
    }

    void destroyEditor(int doc) override {
        const int page = pageOf(doc);
        if (page >= 0) {
            gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), page);
        }
        slots.erase(std::remove_if(slots.begin(), slots.end(), [&](const Slot &item) { return item.id == doc; }), slots.end());
    }

    void showEditor(int doc) override {
        const int page = pageOf(doc);
        if (page >= 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page);
        }
        if (Slot *item = slot(doc)) {
            gtk_widget_grab_focus(item.editor);
        }
    }

    void setWindowTitle(const std::string &title) override {
        gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    }

    void setStatus(const std::string &text) override {
        gtk_statusbar_pop(GTK_STATUSBAR(status), 0);
        gtk_statusbar_push(GTK_STATUSBAR(status), 0, text.c_str());
    }

    void setTabTitle(int doc, const std::string &title) override {
        if (Slot *item = slot(doc)) {
            gtk_label_set_text(GTK_LABEL(item->label), title.c_str());
        }
    }

    std::string promptDirectory() override {
        GtkWidget *dialog = gtk_file_chooser_dialog_new("选择搜索文件夹", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "取消", GTK_RESPONSE_CANCEL, "选择", GTK_RESPONSE_ACCEPT, nullptr);
        std::string path;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *chosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); path = fromUtf8(chosen); g_free(chosen);
        }
        gtk_widget_destroy(dialog); return path;
    }
    std::string promptOpenPath() override {
        GtkWidget *dialog = gtk_file_chooser_dialog_new("打开", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN,
                                                         "取消", GTK_RESPONSE_CANCEL, "打开", GTK_RESPONSE_ACCEPT, nullptr);
        std::string path;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *chosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            path = fromUtf8(chosen);
            g_free(chosen);
        }
        gtk_widget_destroy(dialog);
        return path;
    }

    std::string promptSavePath(const std::string &suggested) override {
        GtkWidget *dialog = gtk_file_chooser_dialog_new("另存为", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                         "取消", GTK_RESPONSE_CANCEL, "保存", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested.c_str());
        std::string path;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *chosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            path = fromUtf8(chosen);
            g_free(chosen);
        }
        gtk_widget_destroy(dialog);
        return path;
    }

    SaveChoice askSave(const std::string &name) override {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                                                    "是否保存对“%s”的更改？", name.c_str());
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "保存", GTK_RESPONSE_YES, "不保存", GTK_RESPONSE_NO, "取消", GTK_RESPONSE_CANCEL, nullptr);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        if (response == GTK_RESPONSE_YES) return SaveChoice::Save;
        if (response == GTK_RESPONSE_NO) return SaveChoice::Discard;
        return SaveChoice::Cancel;
    }

    void alert(const std::string &message) override {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                    "%s", message.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    FindRequest promptFind(const FindRequest &seed, bool replaceMode) override {
        FindRequest request = seed;
        request.cancelled = true;
        GtkWidget *dialog = gtk_dialog_new_with_buttons(replaceMode ? "替换" : "查找", GTK_WINDOW(window),
                                                         static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                                         nullptr, nullptr);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "查找下一个", GTK_RESPONSE_OK);
        if (replaceMode) {
            gtk_dialog_add_button(GTK_DIALOG(dialog), "替换", 1);
            gtk_dialog_add_button(GTK_DIALOG(dialog), "全部替换", 2);
        }
        gtk_dialog_add_button(GTK_DIALOG(dialog), "取消", GTK_RESPONSE_CANCEL);
        GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
        gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
        GtkWidget *find = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(find), seed.find.c_str());
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("查找"), 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), find, 1, 0, 1, 1);
        GtkWidget *repl = gtk_entry_new();
        if (replaceMode) {
            gtk_entry_set_text(GTK_ENTRY(repl), seed.replace.c_str());
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new("替换"), 0, 1, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), repl, 1, 1, 1, 1);
        }
        GtkWidget *matchCase = gtk_check_button_new_with_label("区分大小写");
        GtkWidget *wholeWord = gtk_check_button_new_with_label("全字匹配");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(matchCase), seed.matchCase);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wholeWord), seed.wholeWord);
        gtk_grid_attach(GTK_GRID(grid), matchCase, 0, 2, 2, 1);
        gtk_grid_attach(GTK_GRID(grid), wholeWord, 0, 3, 2, 1);
        GtkWidget *regex = gtk_check_button_new_with_label("正则表达式（替换引用：\\1）");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(regex), seed.regex);
        gtk_grid_attach(GTK_GRID(grid), regex, 0, 4, 2, 1);
        GtkWidget *unicode = gtk_check_button_new_with_label("完整 Unicode（ICU，需启用正则）");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(unicode), seed.unicode);
        gtk_grid_attach(GTK_GRID(grid), unicode, 0, 5, 2, 1);
        gtk_container_add(GTK_CONTAINER(box), grid);
        gtk_widget_show_all(dialog);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_OK || response == 1 || response == 2) {
            request.find = gtk_entry_get_text(GTK_ENTRY(find));
            request.replace = replaceMode ? gtk_entry_get_text(GTK_ENTRY(repl)) : "";
            request.matchCase = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(matchCase));
            request.wholeWord = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wholeWord));
            request.regex = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(regex));
            request.unicode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(unicode));
            request.action = response == 2 ? FindAction::ReplaceAll : response == 1 ? FindAction::ReplaceOne : FindAction::Next;
            request.cancelled = request.find.empty();
        }
        gtk_widget_destroy(dialog);
        return request;
    }

    int promptGoto(int currentLine, int lineCount) override { return promptNumber("转到行", currentLine, lineCount); }
    int promptNumber(const std::string &title, int currentLine, int lineCount) override {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(title.c_str(), GTK_WINDOW(window),
                                                         static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                                         "取消", GTK_RESPONSE_CANCEL, "确定", GTK_RESPONSE_OK, nullptr);
        GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        const std::string label = title + " (1-" + std::to_string(lineCount) + ")";
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), std::to_string(currentLine).c_str());
        gtk_container_add(GTK_CONTAINER(box), gtk_label_new(label.c_str()));
        gtk_container_add(GTK_CONTAINER(box), entry);
        gtk_widget_show_all(dialog);
        int line = 0;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            line = std::atoi(gtk_entry_get_text(GTK_ENTRY(entry)));
        }
        gtk_widget_destroy(dialog);
        return line;
    }

    void setFullScreen(bool enabled) override { if (enabled) gtk_window_fullscreen(GTK_WINDOW(window)); else gtk_window_unfullscreen(GTK_WINDOW(window)); }
    void setAlwaysOnTop(bool enabled) override { gtk_window_set_keep_above(GTK_WINDOW(window), enabled); }
    std::string stateDirectory() const override { return (std::filesystem::path(g_get_user_config_dir()) / "NotepadSharp").string(); }
    bool askReloadExternal(const std::string &message) override {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message.c_str());
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO);
        const bool yes = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(dialog); return yes;
    }
    bool askOverwriteExternal(const std::string &message) override { return askReloadExternal(message); }
    std::string monoFont() const override { return "JetBrains Mono"; }
    int defaultEol() const override { return SC_EOL_LF; }

    void quit() override {
        if (!quitting) {
            gtk_window_close(GTK_WINDOW(window));
        }
    }

    static void onNotify(ScintillaObject *sci, int, SCNotification *notification, GtkHost *self) {
        if (!self->app || !notification) return;
        const int doc = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sci), "doc"));
        self->app->onNotify(doc, notification);
    }
};

void addItem(GtkWidget *menu, const char *label, Command command, GtkAccelGroup *accels, guint key, GdkModifierType mods) {
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    g_object_set_data(G_OBJECT(item), "cmd", GINT_TO_POINTER(command));
    if (key) {
        gtk_widget_add_accelerator(item, "activate", accels, key, mods, GTK_ACCEL_VISIBLE);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

GtkWidget *menuBar(GtkAccelGroup *accels, GtkHost *host) {
    GtkWidget *bar = gtk_menu_bar_new();
    auto section = [&](const char *title) {
        GtkWidget *item = gtk_menu_item_new_with_mnemonic(title);
        GtkWidget *menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
        return menu;
    };
    auto bind = [&](GtkWidget *menu) {
        g_signal_connect(menu, "selection-done", G_CALLBACK(+[](GtkWidget *, GtkHost *) {}), host);
        const int count = g_list_length(gtk_container_get_children(GTK_CONTAINER(menu)));
        (void)count;
    };
    (void)bind;
    GtkWidget *file = section("文件(_F)");
    addItem(file, "新建(_N)", CmdNew, accels, GDK_KEY_n, GDK_CONTROL_MASK);
    addItem(file, "打开(_O)...", CmdOpen, accels, GDK_KEY_o, GDK_CONTROL_MASK);
    addItem(file, "保存(_S)", CmdSave, accels, GDK_KEY_s, GDK_CONTROL_MASK);
    addItem(file, "另存为(_A)...", CmdSaveAs, accels, 0, static_cast<GdkModifierType>(0));
    addItem(file, "关闭(_W)", CmdClose, accels, GDK_KEY_w, GDK_CONTROL_MASK);
    addItem(file, "退出(_X)", CmdExit, accels, GDK_KEY_q, GDK_CONTROL_MASK);
    GtkWidget *edit = section("编辑(_E)");
    addItem(edit, "撤销(_U)", CmdUndo, accels, GDK_KEY_z, GDK_CONTROL_MASK);
    addItem(edit, "重做(_R)", CmdRedo, accels, GDK_KEY_y, GDK_CONTROL_MASK);
    addItem(edit, "剪切(_T)", CmdCut, accels, GDK_KEY_x, GDK_CONTROL_MASK);
    addItem(edit, "复制(_C)", CmdCopy, accels, GDK_KEY_c, GDK_CONTROL_MASK);
    addItem(edit, "粘贴(_P)", CmdPaste, accels, GDK_KEY_v, GDK_CONTROL_MASK);
    addItem(edit, "删除(_D)", CmdDelete, accels, GDK_KEY_Delete, static_cast<GdkModifierType>(0));
    addItem(edit, "全选(_A)", CmdSelectAll, accels, GDK_KEY_a, GDK_CONTROL_MASK);
    GtkWidget *search = section("搜索(_S)");
    addItem(search, "查找(_F)...", CmdFind, accels, GDK_KEY_f, GDK_CONTROL_MASK);
    addItem(search, "查找下一个(_N)", CmdFindNext, accels, GDK_KEY_F3, static_cast<GdkModifierType>(0));
    addItem(search, "查找上一个(_P)", CmdFindPrev, accels, GDK_KEY_F3, GDK_SHIFT_MASK);
    addItem(search, "替换(_R)...", CmdReplace, accels, GDK_KEY_h, GDK_CONTROL_MASK);
    addItem(search, "转到行(_G)...", CmdGoto, accels, GDK_KEY_g, GDK_CONTROL_MASK);
    GtkWidget *view = section("视图(_V)");
    addItem(view, "自动换行(_W)", CmdWrap, accels, 0, static_cast<GdkModifierType>(0));
    addItem(view, "行号(_L)", CmdLineNumbers, accels, 0, static_cast<GdkModifierType>(0));
    addItem(view, "空白字符(_S)", CmdWhitespace, accels, 0, static_cast<GdkModifierType>(0));
    addItem(view, "换行符(_E)", CmdEol, accels, 0, static_cast<GdkModifierType>(0));
    addItem(view, "放大(_I)", CmdZoomIn, accels, GDK_KEY_equal, GDK_CONTROL_MASK);
    addItem(view, "缩小(_O)", CmdZoomOut, accels, GDK_KEY_minus, GDK_CONTROL_MASK);
    addItem(view, "重置缩放(_R)", CmdZoomReset, accels, GDK_KEY_0, GDK_CONTROL_MASK);
    GtkWidget *tools = section("代码工具");
    std::vector<GtkWidget *> commandMenus{file, edit, search, view};
    for (const char *group : {"会话与文件", "编码", "搜索结果", "文本处理", "书签与折叠", "宏", "代码补全", "视图"}) {
        GtkWidget *item = gtk_menu_item_new_with_label(group);
        GtkWidget *submenu = gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(tools), item); commandMenus.push_back(submenu);
        for (const auto &entry : extraCommands) if (std::strcmp(extraCommandGroup(entry.command), group) == 0) {
            const guint key = entry.command == CmdCompleteWord ? GDK_KEY_space : entry.command == CmdNextResult || entry.command == CmdPrevResult ? GDK_KEY_F4 : 0;
            const auto mods = entry.command == CmdCompleteWord ? GDK_CONTROL_MASK : entry.command == CmdPrevResult ? GDK_SHIFT_MASK : static_cast<GdkModifierType>(0);
            addItem(submenu, entry.label, entry.command, accels, key, mods);
        }
    }
    GtkWidget *font = section("字体");
    for (int size = 6; size <= 72; ++size) addItem(font, (std::to_string(size) + " pt").c_str(), static_cast<Command>(CmdFontSizeBase + size), accels, 0, static_cast<GdkModifierType>(0));
    for (int i = 0; i < 8; ++i) addItem(font, ("Weight " + std::to_string(100 * (i+1))).c_str(), static_cast<Command>(CmdFontWeightBase + i), accels, 0, static_cast<GdkModifierType>(0));
    addItem(font, "切换斜体", CmdFontItalic, accels, 0, static_cast<GdkModifierType>(0));
    g_signal_connect(bar, "button-press-event", G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer) { return FALSE; }), nullptr);
    auto activate = +[](GtkWidget *item, GtkHost *self) {
        const Command command = static_cast<Command>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "cmd")));
        if (self->app && command) self->app->onCommand(command);
    };
    GtkWidget *languages = section("语言");
    for (int i=0; i<languageChoiceCount; ++i) addItem(languages, languageChoices[i].label, static_cast<Command>(CmdLanguageBase+i), accels, 0, static_cast<GdkModifierType>(0));
    commandMenus.push_back(languages);
    commandMenus.push_back(font);
    for (GtkWidget *menu : commandMenus) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
        for (GList *node = children; node; node = node->next) {
            g_signal_connect(node->data, "activate", G_CALLBACK(activate), host);
        }
        g_list_free(children);
    }
    auto dynamicMenu = [&](GtkWidget *parent, const char *title, bool recent) {
        GtkWidget *item = gtk_menu_item_new_with_label(title), *menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu); gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
        g_object_set_data(G_OBJECT(menu), "recent", GINT_TO_POINTER(recent));
        g_signal_connect(menu, "show", G_CALLBACK(+[](GtkWidget *menu, GtkHost *self) {
            GList *old = gtk_container_get_children(GTK_CONTAINER(menu));
            for (GList *node=old; node; node=node->next) gtk_widget_destroy(GTK_WIDGET(node->data)); g_list_free(old);
            const bool recent = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu), "recent"));
            const auto names = recent ? self->app->recentFiles() : self->app->documentNames();
            for (size_t i=0; i<names.size() && i<1000; ++i) {
                GtkWidget *entry = gtk_menu_item_new_with_label(names[i].c_str());
                g_object_set_data(G_OBJECT(entry), "cmd", GINT_TO_POINTER((recent ? CmdRecentBase : CmdDocumentBase)+static_cast<int>(i)));
                g_signal_connect(entry, "activate", G_CALLBACK(+[](GtkWidget *entry, GtkHost *self) { self->app->onCommand(static_cast<Command>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "cmd")))); }), self);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry); gtk_widget_show(entry);
            }
        }), host);
    };
    dynamicMenu(file, "最近文件", true); dynamicMenu(bar, "窗口", false);
    return bar;
}

}  // namespace

int main(int argc, char **argv) {
    const auto fonts = std::filesystem::read_symlink("/proc/self/exe").parent_path() / "fonts";
    if (std::filesystem::exists(fonts)) {
        for (const auto &entry : std::filesystem::directory_iterator(fonts))
            if (entry.path().extension() == ".ttf") FcConfigAppFontAddFile(FcConfigGetCurrent(), reinterpret_cast<const FcChar8 *>(entry.path().c_str()));
        FcConfigBuildFonts(FcConfigGetCurrent());
    }
    gtk_init(&argc, &argv);
    GtkHost host;
    host.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(host.window), 960, 640);
    GtkAccelGroup *accels = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(host.window), accels);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    host.notebook = gtk_notebook_new();
    host.status = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(box), menuBar(accels, &host), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), host.notebook, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), host.status, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(host.window), box);
    App app(host);
    host.app = &app;
    g_signal_connect(host.notebook, "switch-page", G_CALLBACK(+[](GtkNotebook *, GtkWidget *page, guint, GtkHost *self) {
        if (!self->app) return;
        self->app->onTabChanged(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "doc")));
    }), &host);
    g_signal_connect(host.window, "delete-event", G_CALLBACK(+[](GtkWidget *, GdkEvent *, GtkHost *self) -> gboolean {
        self->quitting = true;
        const bool ok = self->app->onCloseRequested();
        self->quitting = false;
        return ok ? FALSE : TRUE;
    }), &host);
    g_signal_connect(host.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    app.start();
    const guint fileTimer = g_timeout_add_seconds(2, +[](gpointer data) -> gboolean { static_cast<App *>(data)->pollFiles(); return G_SOURCE_CONTINUE; }, &app);
    const guint recoveryTimer = g_timeout_add_seconds(30, +[](gpointer data) -> gboolean { static_cast<App *>(data)->checkpoint(); return G_SOURCE_CONTINUE; }, &app);
    if (argc > 1) app.openPath(argv[1]);
    gtk_widget_show_all(host.window);
    gtk_main();
    g_source_remove(recoveryTimer);
    g_source_remove(fileTimer);
    return 0;
}
