/*
 * PoorMansTSqlFormatter.mm — macOS (Nextpad++) port of the Poor Man's T-SQL
 * Formatter Notepad++ plugin.
 *
 * Original plugin & engine: (C) 2011-2017 Tao Klerks — AGPL v3.
 * macOS port: 2026. This file is the host glue: the two menu commands
 * ("Format T-SQL Code" / "T-SQL Formating Options…"), the format-selection-or-
 * document command, the Settings dialog (faithful to the Windows SettingsForm)
 * and the About box (faithful to the Windows AboutBox). The formatting engine
 * itself is the faithful C++ reimplementation under src/engine/ (validated
 * byte-for-byte against the Windows test corpus).
 *
 * Pure text transform — no host changes required.
 */
#include "NppPluginInterfaceMac.h"
#include "Scintilla.h"
#import <Cocoa/Cocoa.h>

#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>

#include "engine/Engine.h"
#include "engine/Options.h"
#include "AboutLogoData.h"
#include "LicenseData.h"

using namespace pmsf;

// ── plugin state ────────────────────────────────────────────────────────────
static const char* PLUGIN_NAME = "Poor Man's T-Sql Formatter";
static const int NB_FUNC = 2;
static FuncItem funcItem[NB_FUNC];
static NppData nppData;
static TSqlStandardFormatterOptions g_options;
static std::string g_configPath;

// ── Scintilla / host helpers ────────────────────────────────────────────────
static NppHandle getCurScintilla() {
    int which = -1;
    nppData._sendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (intptr_t)&which);
    if (which == -1) return 0;
    return (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}
static intptr_t sci(NppHandle h, uint32_t msg, uintptr_t w = 0, intptr_t l = 0) {
    return nppData._sendMessage(h, msg, w, l);
}

// ── settings persistence (host plugins-config dir, serialized option string) ─
static std::string pluginsConfigDir() {
    char buf[2048]; buf[0] = '\0';
    nppData._sendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, sizeof(buf), (intptr_t)buf);
    if (buf[0] != '\0') return std::string(buf);
    // Fallback for older hosts.
    @autoreleasepool {
        NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent:@".nextpad++/plugins/Config"];
        return std::string([dir UTF8String]);
    }
}

static void loadSettings() {
    @autoreleasepool {
        std::string dir = pluginsConfigDir();
        [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:dir.c_str()]
                                  withIntermediateDirectories:YES attributes:nil error:nil];
        g_configPath = dir + "/PoorMansTSqlFormatter.ini";
    }
    std::ifstream f(g_configPath);
    if (!f.good()) return;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // trim trailing whitespace/newlines
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' '))
        content.pop_back();
    try {
        g_options = TSqlStandardFormatterOptions(content);
    } catch (...) {
        g_options = TSqlStandardFormatterOptions();  // corrupt config → defaults
    }
}

static void saveSettings() {
    std::ofstream f(g_configPath);
    if (f.good()) f << g_options.toSerializedString();
}

// ── parse-error "continue?" warning (matches the Windows MessageBox) ─────────
static bool warnContinueOnError() {
    __block bool cont = false;
    void (^show)(void) = ^{
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Errors found. Continue?";
        alert.informativeText = @"Errors found during SQL parsing. Would you like to apply formatting anyway?";
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];
        cont = ([alert runModal] == NSAlertFirstButtonReturn);
    };
    if ([NSThread isMainThread]) show();
    else dispatch_sync(dispatch_get_main_queue(), show);
    return cont;
}

// ── the "Format T-SQL Code" command ──────────────────────────────────────────
static void formatSqlCommand() {
    @autoreleasepool {
        NppHandle h = getCurScintilla();
        if (!h) return;

        intptr_t selLen = sci(h, SCI_GETSELTEXT, 0, 0);
        if (selLen > 1) {
            std::string buf((size_t)selLen, '\0');
            sci(h, SCI_GETSELTEXT, 0, (intptr_t)buf.data());
            buf.resize(std::strlen(buf.c_str()));

            bool err = false;
            std::string out = formatSql(buf, g_options, &err);
            if (err && !warnContinueOnError()) return;
            sci(h, SCI_REPLACESEL, 0, (intptr_t)out.c_str());
        } else {
            intptr_t docLen = sci(h, SCI_GETLENGTH, 0, 0);
            intptr_t curPos = sci(h, SCI_GETCURRENTPOS, 0, 0);
            std::string buf((size_t)docLen + 1, '\0');
            sci(h, SCI_GETTEXT, docLen + 1, (intptr_t)buf.data());
            buf.resize((size_t)docLen);

            bool err = false;
            std::string out = formatSql(buf, g_options, &err);
            if (err && !warnContinueOnError()) return;

            intptr_t newPos = 0;
            if (docLen > 0)
                newPos = (intptr_t)std::llround(1.0 * (double)curPos * (double)out.size() / (double)docLen);
            sci(h, SCI_SETTEXT, 0, (intptr_t)out.c_str());
            sci(h, SCI_SETSEL, newPos, newPos);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  About box (faithful to the Windows AboutBox: rings logo + version +
//  copyright + AGPL description/license, OK button)
// ════════════════════════════════════════════════════════════════════════════
@interface PMSFAboutController : NSObject <NSWindowDelegate>
@end
@implementation PMSFAboutController
- (void)onOK:(id)sender { (void)sender; [NSApp stopModal]; }
- (void)windowWillClose:(NSNotification*)n { (void)n; [NSApp stopModal]; }
@end

static void showAboutBox(NSWindow* parent) {
    @autoreleasepool {
        const CGFloat W = 560, H = 420, pad = 16;
        NSRect frame = NSMakeRect(0, 0, W, H);
        NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                          styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                            backing:NSBackingStoreBuffered defer:NO];
        win.title = @"About Poor Man's T-Sql Formatter";
        NSView* cv = win.contentView;

        // logo (left column)
        NSData* png = [NSData dataWithBytes:kAboutLogoPng length:kAboutLogoPngLen];
        NSImage* logo = [[NSImage alloc] initWithData:png];
        NSImageView* iv = [[NSImageView alloc] initWithFrame:NSMakeRect(pad, H - 262 - pad, 120, 262)];
        iv.image = logo;
        iv.imageScaling = NSImageScaleProportionallyUpOrDown;
        [cv addSubview:iv];

        CGFloat rx = pad + 120 + pad;
        CGFloat rw = W - rx - pad;

        NSTextField* title = [NSTextField labelWithString:@"Poor Man's T-Sql Formatter"];
        title.font = [NSFont boldSystemFontOfSize:15];
        title.frame = NSMakeRect(rx, H - pad - 24, rw, 22);
        [cv addSubview:title];

        NSTextField* ver = [NSTextField labelWithString:@"PoorMansTSqlFormatterNppPlugin, v1.0.0"];
        ver.frame = NSMakeRect(rx, H - pad - 48, rw, 18);
        [cv addSubview:ver];

        NSTextField* copy = [NSTextField labelWithString:@"Copyright © 2011-2017 Tao Klerks"];
        copy.frame = NSMakeRect(rx, H - pad - 70, rw, 18);
        [cv addSubview:copy];

        // description + full AGPL license in a scrollable, read-only text view
        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(rx, pad + 40, rw, H - pad - 80 - 40)];
        scroll.hasVerticalScroller = YES;
        scroll.borderType = NSBezelBorder;
        NSTextView* tv = [[NSTextView alloc] initWithFrame:scroll.bounds];
        tv.editable = NO;
        tv.font = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont systemFontOfSize:10];
        std::string body = std::string("A simple free (AGPL) T-SQL Formatting Plugin for Notepad++.\n\n") + kAgplLicenseText;
        tv.string = [NSString stringWithUTF8String:body.c_str()];
        scroll.documentView = tv;
        [cv addSubview:scroll];

        PMSFAboutController* ac = [[PMSFAboutController alloc] init];
        win.delegate = ac;

        NSButton* ok = [NSButton buttonWithTitle:@"OK" target:ac action:@selector(onOK:)];
        ok.frame = NSMakeRect(W - pad - 90, pad, 90, 28);
        ok.bezelStyle = NSBezelStyleRounded;
        ok.keyEquivalent = @"\r";
        [cv addSubview:ok];

        [win center];
        [NSApp runModalForWindow:win];   // nested modal; onOK / close ends it
        [win orderOut:nil];
        win.delegate = nil;
        (void)parent;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Settings dialog (faithful to the Windows SettingsForm — Image #24)
// ════════════════════════════════════════════════════════════════════════════
@interface PMSFSettingsController : NSObject {
@public
    NSWindow* window;
    NSTextField* txtIndent;
    NSTextField* txtSpacesPerTab;
    NSTextField* txtMaxWidth;
    NSTextField* txtStatementBreaks;
    NSTextField* txtClauseBreaks;
    NSButton* chkExpandCommaLists;
    NSButton* chkTrailingCommas;
    NSButton* chkExpandBooleanExpressions;
    NSButton* chkExpandCaseStatements;
    NSButton* chkExpandBetweenConditions;
    NSButton* chkExpandInLists;
    NSButton* chkUppercaseKeywords;
    NSButton* chkSpaceAfterExpandedComma;
    NSButton* chkBreakJoinOnSections;
    NSButton* chkStandardizeKeywords;
}
- (void)loadFrom:(const TSqlStandardFormatterOptions&)o;
@end

@implementation PMSFSettingsController

- (void)loadFrom:(const TSqlStandardFormatterOptions&)o {
    // Show the indent string with the same \t / \s escapes the Windows form uses.
    std::string ind = o.indentString;
    ind = replaceAll(ind, "\t", "\\t");
    ind = replaceAll(ind, " ", "\\s");
    txtIndent.stringValue = [NSString stringWithUTF8String:ind.c_str()];
    txtSpacesPerTab.intValue = o.spacesPerTab;
    txtMaxWidth.intValue = o.maxLineWidth;
    txtStatementBreaks.intValue = o.newStatementLineBreaks;
    txtClauseBreaks.intValue = o.newClauseLineBreaks;
    chkExpandCommaLists.state         = o.expandCommaLists ? NSControlStateValueOn : NSControlStateValueOff;
    chkTrailingCommas.state           = o.trailingCommas ? NSControlStateValueOn : NSControlStateValueOff;
    chkExpandBooleanExpressions.state = o.expandBooleanExpressions ? NSControlStateValueOn : NSControlStateValueOff;
    chkExpandCaseStatements.state     = o.expandCaseStatements ? NSControlStateValueOn : NSControlStateValueOff;
    chkExpandBetweenConditions.state  = o.expandBetweenConditions ? NSControlStateValueOn : NSControlStateValueOff;
    chkExpandInLists.state            = o.expandInLists ? NSControlStateValueOn : NSControlStateValueOff;
    chkUppercaseKeywords.state        = o.uppercaseKeywords ? NSControlStateValueOn : NSControlStateValueOff;
    chkSpaceAfterExpandedComma.state  = o.spaceAfterExpandedComma ? NSControlStateValueOn : NSControlStateValueOff;
    chkBreakJoinOnSections.state      = o.breakJoinOnSections ? NSControlStateValueOn : NSControlStateValueOff;
    chkStandardizeKeywords.state      = o.keywordStandardization ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)onReset:(id)sender {
    (void)sender;
    TSqlStandardFormatterOptions def;
    [self loadFrom:def];
}
- (void)onAbout:(id)sender {
    (void)sender;
    showAboutBox(window);
}
- (void)onSave:(id)sender {
    (void)sender;
    [NSApp stopModalWithCode:NSModalResponseOK];
}
- (void)onCancel:(id)sender {
    (void)sender;
    [NSApp stopModalWithCode:NSModalResponseCancel];
}
@end

static NSButton* makeCheckbox(NSView* cv, NSString* title, NSRect frame) {
    NSButton* b = [NSButton checkboxWithTitle:title target:nil action:nil];
    b.frame = frame;
    [cv addSubview:b];
    return b;
}
static NSTextField* makeLabel(NSView* cv, NSString* s, NSRect frame) {
    NSTextField* l = [NSTextField labelWithString:s];
    l.frame = frame;
    [cv addSubview:l];
    return l;
}
static NSTextField* makeField(NSView* cv, NSRect frame) {
    NSTextField* t = [[NSTextField alloc] initWithFrame:frame];
    [cv addSubview:t];
    return t;
}

static void showOptionsDialog() {
    @autoreleasepool {
        const CGFloat W = 470, H = 420, pad = 16;
        NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, W, H)
                          styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                            backing:NSBackingStoreBuffered defer:NO];
        win.title = @"Poor Man's T-Sql Formatter - Settings";
        NSView* cv = win.contentView;

        PMSFSettingsController* c = [[PMSFSettingsController alloc] init];
        c->window = win;

        CGFloat y = H - pad - 22;
        const CGFloat rowH = 28, labelW = 110, fieldX = pad + labelW + 6;

        // Indent String + hint
        makeLabel(cv, @"Indent String:", NSMakeRect(pad, y, labelW, 18));
        c->txtIndent = makeField(cv, NSMakeRect(fieldX, y - 3, 70, 22));
        makeLabel(cv, @"(use \\t for tab and \\s for space)",
                  NSMakeRect(fieldX + 78, y, W - fieldX - 78 - pad, 18));
        y -= rowH;
        // Spaces Per Tab + extra hint
        makeLabel(cv, @"Spaces Per Tab:", NSMakeRect(pad, y, labelW, 18));
        c->txtSpacesPerTab = makeField(cv, NSMakeRect(fieldX, y - 3, 50, 22));
        makeLabel(cv, @"(for Max Width feature)", NSMakeRect(fieldX + 58, y, W - fieldX - 58 - pad, 18));
        y -= rowH;
        // Max Line Width
        makeLabel(cv, @"Max Line Width:", NSMakeRect(pad, y, labelW, 18));
        c->txtMaxWidth = makeField(cv, NSMakeRect(fieldX, y - 3, 60, 22));
        y -= rowH;
        // Statement Breaks
        makeLabel(cv, @"Statement Breaks:", NSMakeRect(pad, y, labelW, 18));
        c->txtStatementBreaks = makeField(cv, NSMakeRect(fieldX, y - 3, 50, 22));
        y -= rowH;
        // Clause Breaks
        makeLabel(cv, @"Clause Breaks:", NSMakeRect(pad, y, labelW, 18));
        c->txtClauseBreaks = makeField(cv, NSMakeRect(fieldX, y - 3, 50, 22));
        y -= rowH + 6;

        // Two columns of checkboxes
        const CGFloat colL = pad, colR = pad + (W - 2 * pad) / 2.0;
        const CGFloat colW = (W - 2 * pad) / 2.0;
        CGFloat cy = y;
        c->chkExpandCommaLists         = makeCheckbox(cv, @"Expand Comma Lists", NSMakeRect(colL, cy, colW, 20));
        c->chkTrailingCommas           = makeCheckbox(cv, @"Trailing Commas",    NSMakeRect(colR, cy, colW, 20));
        cy -= 24;
        c->chkExpandBooleanExpressions = makeCheckbox(cv, @"Expand Boolean Expressions", NSMakeRect(colL, cy, colW, 20));
        c->chkExpandCaseStatements     = makeCheckbox(cv, @"Expand Case Statements",     NSMakeRect(colR, cy, colW, 20));
        cy -= 24;
        c->chkExpandBetweenConditions  = makeCheckbox(cv, @"Expand Between Conditions", NSMakeRect(colL, cy, colW, 20));
        c->chkExpandInLists            = makeCheckbox(cv, @"Expand IN Lists",           NSMakeRect(colR, cy, colW, 20));
        cy -= 24;
        c->chkUppercaseKeywords        = makeCheckbox(cv, @"Uppercase Keywords",         NSMakeRect(colL, cy, colW, 20));
        c->chkSpaceAfterExpandedComma  = makeCheckbox(cv, @"Space after Expanded Comma", NSMakeRect(colR, cy, colW, 20));
        cy -= 24;
        c->chkBreakJoinOnSections      = makeCheckbox(cv, @"Break Join ON Sections", NSMakeRect(colL, cy, colW, 20));
        c->chkStandardizeKeywords      = makeCheckbox(cv, @"Standardize Keywords",   NSMakeRect(colR, cy, colW, 20));

        // Buttons: About... / Reset (left), Save / Cancel (right)
        NSButton* about = [NSButton buttonWithTitle:@"About..." target:c action:@selector(onAbout:)];
        about.frame = NSMakeRect(pad, pad, 84, 28);
        about.bezelStyle = NSBezelStyleRounded;
        [cv addSubview:about];
        NSButton* reset = [NSButton buttonWithTitle:@"Reset" target:c action:@selector(onReset:)];
        reset.frame = NSMakeRect(pad + 90, pad, 72, 28);
        reset.bezelStyle = NSBezelStyleRounded;
        [cv addSubview:reset];

        NSButton* save = [NSButton buttonWithTitle:@"Save" target:c action:@selector(onSave:)];
        save.frame = NSMakeRect(W - pad - 90, pad, 90, 28);
        save.bezelStyle = NSBezelStyleRounded;
        save.keyEquivalent = @"\r";
        [cv addSubview:save];
        NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:c action:@selector(onCancel:)];
        cancel.frame = NSMakeRect(W - pad - 90 - 96, pad, 90, 28);
        cancel.bezelStyle = NSBezelStyleRounded;
        cancel.keyEquivalent = @"\033";  // Esc
        [cv addSubview:cancel];

        [c loadFrom:g_options];
        [win center];

        NSModalResponse resp = [NSApp runModalForWindow:win];
        [win orderOut:nil];

        if (resp == NSModalResponseOK) {
            // Apply fields back into g_options.
            std::string ind = std::string([c->txtIndent.stringValue UTF8String]);
            g_options.setIndentString(ind);
            int spt = c->txtSpacesPerTab.intValue;  if (spt < 1) spt = 4;
            g_options.spacesPerTab = spt;
            int mlw = c->txtMaxWidth.intValue;       if (mlw < 1) mlw = 999;
            g_options.maxLineWidth = mlw;
            int sb = c->txtStatementBreaks.intValue; if (sb < 0) sb = 2;
            g_options.newStatementLineBreaks = sb;
            int clb = c->txtClauseBreaks.intValue;   if (clb < 0) clb = 1;
            g_options.newClauseLineBreaks = clb;
            g_options.expandCommaLists         = (c->chkExpandCommaLists.state == NSControlStateValueOn);
            g_options.trailingCommas           = (c->chkTrailingCommas.state == NSControlStateValueOn);
            g_options.expandBooleanExpressions = (c->chkExpandBooleanExpressions.state == NSControlStateValueOn);
            g_options.expandCaseStatements     = (c->chkExpandCaseStatements.state == NSControlStateValueOn);
            g_options.expandBetweenConditions  = (c->chkExpandBetweenConditions.state == NSControlStateValueOn);
            g_options.expandInLists            = (c->chkExpandInLists.state == NSControlStateValueOn);
            g_options.uppercaseKeywords        = (c->chkUppercaseKeywords.state == NSControlStateValueOn);
            g_options.spaceAfterExpandedComma  = (c->chkSpaceAfterExpandedComma.state == NSControlStateValueOn);
            g_options.breakJoinOnSections      = (c->chkBreakJoinOnSections.state == NSControlStateValueOn);
            g_options.keywordStandardization   = (c->chkStandardizeKeywords.state == NSControlStateValueOn);
            saveSettings();
        }
    }
}

// ── plugin exports ───────────────────────────────────────────────────────────
extern "C" NPP_EXPORT void setInfo(NppData data) {
    nppData = data;
    loadSettings();

    strlcpy(funcItem[0]._itemName, "Format T-SQL Code", NPP_MENU_ITEM_SIZE);
    funcItem[0]._pFunc = formatSqlCommand;
    funcItem[0]._init2Check = false;
    funcItem[0]._pShKey = nullptr;

    strlcpy(funcItem[1]._itemName, "T-SQL Formating Options...", NPP_MENU_ITEM_SIZE);
    funcItem[1]._pFunc = showOptionsDialog;
    funcItem[1]._init2Check = false;
    funcItem[1]._pShKey = nullptr;
}

extern "C" NPP_EXPORT const char* getName() { return PLUGIN_NAME; }

extern "C" NPP_EXPORT FuncItem* getFuncsArray(int* nbF) {
    *nbF = NB_FUNC;
    return funcItem;
}

extern "C" NPP_EXPORT void beNotified(SCNotification* n) { (void)n; }

extern "C" NPP_EXPORT intptr_t messageProc(uint32_t msg, uintptr_t wParam, intptr_t lParam) {
    (void)msg; (void)wParam; (void)lParam;
    return 1;
}
