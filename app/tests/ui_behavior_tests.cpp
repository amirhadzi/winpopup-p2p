// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises production UI state transitions without a profile or network.
#include "../src/main.cpp"
#include <iostream>
#include <stdexcept>

namespace {
int behaviorChecks = 0;

void CheckBehavior(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
    ++behaviorChecks;
    std::cout << "PASS " << description << std::endl;
}

struct BehaviorFixture {
    App app;
    BehaviorFixture() {
        app.preview = true;
        app.window = CreateWindowExW(0, L"STATIC", L"Hidden WinPopup behavior fixture", WS_POPUP,
            0, 0, 460, 330, nullptr, nullptr, instance, nullptr);
        if (!app.window) throw std::runtime_error("Could not create hidden fixture window");
        app.body = Child(app.window, L"EDIT", L"", ES_MULTILINE | ES_READONLY, 1000);
    }
    ~BehaviorFixture() {
        if (app.composeWindow) DestroyWindow(app.composeWindow);
        if (app.window) DestroyWindow(app.window);
    }
    void Contacts() {
        app.contacts = {{12, std::string(64, 'A'), "Alex", popup::Connection::Direct},
            {27, std::string(64, 'B'), "Sam", popup::Connection::Relay}};
    }
    void Composer() {
        HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, L"WinPopupCompose", L"Hidden composer",
            WS_POPUP | WS_CLIPCHILDREN, 0, 0, 308, 298, app.window, nullptr, instance, &app);
        if (!window) throw std::runtime_error("Could not create hidden production composer");
    }
};

void TestInboxNavigation() {
    BehaviorFixture fixture;
    auto& app = fixture.app;
    Navigate(app, 1);
    DeleteCurrent(app);
    CheckBehavior(app.inbox.empty() && app.currentMessage == -1, "empty inbox navigation and deletion are safe");
    app.inbox.resize(3);
    app.currentMessage = 0;
    app.drafts[77] = L"Unsent draft must survive inbox actions";
    Navigate(app, -1);
    CheckBehavior(app.currentMessage == 0, "previous stops at first message");
    Navigate(app, 1);
    CheckBehavior(app.currentMessage == 1, "next opens the following message");
    Navigate(app, 1);
    Navigate(app, 1);
    CheckBehavior(app.currentMessage == 2, "next stops at last message");
    DeleteCurrent(app);
    CheckBehavior(app.inbox.size() == 2 && app.currentMessage == 1, "deleting last message selects remaining predecessor");
    Navigate(app, -1);
    DeleteCurrent(app);
    CheckBehavior(app.inbox.size() == 1 && app.currentMessage == 0, "deleting first message retains a valid selection");
    DeleteCurrent(app);
    CheckBehavior(app.inbox.empty() && app.currentMessage == -1, "deleting final message clears the selection");
    CheckBehavior(app.drafts[77] == L"Unsent draft must survive inbox actions", "inbox deletion does not discard compose drafts");
}

void TestReceiveInbox() {
    BehaviorFixture fixture;
    fixture.Contacts();
    auto& app = fixture.app;
    app.myName = L"Taylor";
    app.popupOnMessage = false;
    popup::Event event;
    event.type = popup::EventType::Message;
    event.contact = 12;
    event.text = u8"Hello from Alex — 你好";
    HandleEvent(app, event);
    CheckBehavior(app.inbox.size() == 1 && app.currentMessage == 0 &&
        app.inbox[0].from == L"Alex" && app.inbox[0].to == L"Taylor" &&
        WindowText(app.body) == Wide(event.text), "received popup shows sender, recipient, and Unicode text");
    event.contact = 27;
    event.text = "Second message";
    HandleEvent(app, event);
    CheckBehavior(app.currentMessage == 0 && app.inbox.size() == 2, "receiving with popup disabled preserves current selection");
    Navigate(app, 1);
    CheckBehavior(WindowText(app.body) == L"Second message", "navigation updates the native message display");
    for (int i = 0; i < 201; ++i) { event.text = "bounded " + std::to_string(i); HandleEvent(app, event); }
    CheckBehavior(app.inbox.size() == 200 && app.currentMessage >= 0 && app.currentMessage < 200,
        "incoming message limit retains a valid selection");
}

void TestReceiptMatching() {
    BehaviorFixture fixture;
    fixture.Contacts();
    auto& app = fixture.app;
    app.outbox.push_back({1, 12, 0, L"Alex", L"First", L"now", Delivery::Pending, std::string(64, 'A')});
    app.outbox.push_back({2, 27, 0, L"Sam", L"Second", L"now", Delivery::Pending, std::string(64, 'B')});
    popup::Event event;
    event.type = popup::EventType::Sent;
    event.contact = 12;
    event.receipt = 0;
    event.text = "First";
    HandleEvent(app, event);
    CheckBehavior(app.outbox[0].delivery == Delivery::Sent && app.outbox[1].delivery == Delivery::Pending,
        "send acceptance changes only the matching peer's outgoing message");
    event.contact = 27;
    event.text = "Second";
    HandleEvent(app, event);
    CheckBehavior(app.outbox[1].delivery == Delivery::Sent && app.inbox.empty(),
        "outgoing messages remain outside the received popup inbox");
    event.type = popup::EventType::Receipt;
    HandleEvent(app, event);
    CheckBehavior(app.outbox[0].delivery == Delivery::Sent && app.outbox[1].delivery == Delivery::Delivered,
        "equal receipt IDs on different peers do not cross-match");
    event.contact = 12;
    event.receipt = 99;
    HandleEvent(app, event);
    CheckBehavior(app.outbox[0].delivery == Delivery::Sent, "unmatched receipt does not claim delivery");
    event.receipt = 0;
    HandleEvent(app, event);
    HandleEvent(app, event);
    CheckBehavior(app.outbox[0].delivery == Delivery::Delivered && app.outbox.size() == 2,
        "receipt zero is valid and duplicate receipts are harmless");
    app.outbox.push_back({3, 12, 4, L"Alex", L"Old identity", L"now", Delivery::Sent, std::string(64, 'A')});
    app.contacts[0].publicKey = std::string(64, 'C');
    event.receipt = 4;
    HandleEvent(app, event);
    CheckBehavior(app.outbox[2].delivery == Delivery::Sent, "reused contact number with a different public key cannot claim old delivery");
}

void TestPendingDrafts() {
    BehaviorFixture fixture;
    fixture.Contacts();
    auto& app = fixture.app;
    app.composeContact = 12;
    app.drafts[12] = L"Keep this draft";
    fixture.Composer();
    app.outbox.push_back({1, 12, 0, L"Alex", L"Keep this draft", L"now", Delivery::Pending, std::string(64, 'A')});
    app.pendingSend = 1;
    SetComposePending(app, true);
    CheckBehavior(!IsWindowEnabled(app.sendOkay) && !IsWindowEnabled(app.toCombo) &&
        (GetWindowLongPtrW(app.messageEdit, GWL_STYLE) & ES_READONLY) != 0,
        "pending send freezes recipient and message until acknowledged");
    popup::Event event;
    event.type = popup::EventType::Error;
    event.contact = 12;
    event.text = "Profile save warning";
    HandleEvent(app, event);
    CheckBehavior(app.outbox[0].delivery == Delivery::Pending && app.pendingSend == 1,
        "unrelated error does not fail a pending message");
    event.key = "send";
    event.text = "The peer disconnected";
    HandleEvent(app, event);
    CheckBehavior(app.outbox[0].delivery == Delivery::Failed && app.pendingSend == 0 &&
        app.drafts[12] == L"Keep this draft" && WindowText(app.messageEdit) == L"Keep this draft" &&
        IsWindow(app.composeWindow), "failed send retains the composer and original draft");
    CheckBehavior(IsWindowEnabled(app.sendOkay) && IsWindowEnabled(app.toCombo) &&
        (GetWindowLongPtrW(app.messageEdit, GWL_STYLE) & ES_READONLY) == 0,
        "failed send enables editing and retry");
    app.outbox.push_back({2, 12, 0, L"Alex", L"Earlier text", L"now", Delivery::Pending, std::string(64, 'A')});
    app.pendingSend = 2;
    app.drafts[12] = L"Newer unsent text";
    SetWindowTextW(app.messageEdit, L"Newer unsent text");
    event.type = popup::EventType::Sent;
    event.key.clear();
    event.receipt = 17;
    event.text = "Earlier text";
    HandleEvent(app, event);
    CheckBehavior(app.outbox[1].delivery == Delivery::Sent && app.pendingSend == 0 &&
        app.drafts[12] == L"Newer unsent text" && WindowText(app.messageEdit) == L"Newer unsent text" &&
        IsWindow(app.composeWindow), "late acceptance does not discard newer draft text");
    app.outbox.push_back({3, 12, 0, L"Alex", L"Newer unsent text", L"now", Delivery::Pending, std::string(64, 'A')});
    app.pendingSend = 3;
    event.receipt = 18;
    event.text = "Newer unsent text";
    HandleEvent(app, event);
    CheckBehavior(app.outbox[2].delivery == Delivery::Sent && !app.composeWindow &&
        app.drafts[12].empty(), "acceptance closes only the matching unchanged composer");
}

void TestRecipientAndDraftSwitching() {
    BehaviorFixture fixture;
    fixture.Contacts();
    auto& app = fixture.app;
    app.composeContact = 12;
    app.drafts[12] = L"Draft for Alex";
    app.drafts[27] = L"Draft for Sam";
    fixture.Composer();
    SetWindowTextW(app.messageEdit, L"Edited Alex draft");
    SendMessageW(app.toCombo, CB_SETCURSEL, 1, 0);
    ChangeRecipient(app);
    CheckBehavior(app.composeContact == 27 && app.drafts[12] == L"Edited Alex draft" &&
        WindowText(app.messageEdit) == L"Draft for Sam", "switching recipients saves and restores their separate drafts");
    SetWindowTextW(app.messageEdit, L"Edited Sam draft");
    SendMessageW(app.toCombo, CB_SETCURSEL, 0, 0);
    ChangeRecipient(app);
    CheckBehavior(app.composeContact == 12 && app.drafts[27] == L"Edited Sam draft" &&
        WindowText(app.messageEdit) == L"Edited Alex draft", "switching back restores the correct edited draft");
    SendMessageW(app.toCombo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SetWindowTextW(app.toCombo, L"tox:invalid-id");
    CheckBehavior(ComposerRecipient(app) == nullptr && WindowText(app.messageEdit) == L"Edited Alex draft",
        "invalid typed invitation is not resolved as a peer and leaves text intact");
    SetWindowTextW(app.toCombo, L"Missing contact");
    CheckBehavior(ComposerRecipient(app) == nullptr && WindowText(app.messageEdit) == L"Edited Alex draft",
        "missing peer leaves composed text intact");
    app.contacts[1].name = "Alex";
    FillRecipients(app);
    SendMessageW(app.toCombo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SetWindowTextW(app.toCombo, L"Alex");
    CheckBehavior(ComposerRecipient(app) == nullptr, "duplicate typed display names are rejected as ambiguous");
    SendMessageW(app.toCombo, CB_SETCURSEL, 1, 0);
    auto selected = ComposerRecipient(app);
    CheckBehavior(selected && selected->number == 27, "explicit combo selection resolves duplicate names by peer number");
    DestroyWindow(app.composeWindow);
    fixture.Composer();
    CheckBehavior(WindowText(app.messageEdit) == L"Edited Alex draft", "closing and reopening the composer retains its draft");
}

void TestTypedRecipientRefresh() {
    BehaviorFixture fixture;
    fixture.Contacts();
    auto& app = fixture.app;
    app.composeContact = 12;
    app.drafts[12] = L"Alex's draft";
    app.drafts[27] = L"Sam's stored draft";
    fixture.Composer();
    SetWindowTextW(app.messageEdit, L"Message being addressed to Sam");
    SetWindowTextW(app.toCombo, L"Sam");
    EditRecipient(app);
    auto recipient = ComposerRecipient(app);
    CheckBehavior(app.typedRecipient && app.composeContact == -1 && recipient && recipient->number == 27,
        "typing another recipient replaces a prior combo selection");
    FillRecipients(app);
    recipient = ComposerRecipient(app);
    CheckBehavior(WindowText(app.toCombo) == L"Sam" && recipient && recipient->number == 27 &&
        WindowText(app.messageEdit) == L"Message being addressed to Sam",
        "contact refresh preserves typed recipient and message");
    SetWindowTextW(app.toCombo, L"Not yet a contact");
    EditRecipient(app);
    SetWindowTextW(app.messageEdit, L"Independent typed draft");
    DestroyWindow(app.composeWindow);
    fixture.Composer();
    CheckBehavior(app.typedRecipient && app.composeContact == -1 &&
        WindowText(app.toCombo) == L"Not yet a contact" && WindowText(app.messageEdit) == L"Independent typed draft",
        "cancelled typed recipient and its draft survive reopening");
    app.contacts[1].name = "Alex";
    SetWindowTextW(app.toCombo, L"Alex");
    EditRecipient(app);
    FillRecipients(app);
    CheckBehavior(ComposerRecipient(app) == nullptr, "typed duplicate name remains ambiguous after refresh");
    SendMessageW(app.toCombo, CB_SETCURSEL, 1, 0);
    ChangeRecipient(app);
    recipient = ComposerRecipient(app);
    CheckBehavior(!app.typedRecipient && app.composeContact == 27 && recipient && recipient->number == 27 &&
        WindowText(app.messageEdit) == L"Sam's stored draft",
        "explicit selection leaves typed mode and restores that peer's draft");
}
} // namespace

int wmain() {
    try {
        instance = GetModuleHandleW(nullptr);
        dpi = 96;
        MakeFonts();
        faceBrush = CreateSolidBrush(FaceColor);
        whiteBrush = CreateSolidBrush(White);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        RegisterClasses();
        TestInboxNavigation();
        TestReceiveInbox();
        TestReceiptMatching();
        TestPendingDrafts();
        TestRecipientAndDraftSwitching();
        TestTypedRecipientRefresh();
        DeleteObject(font);
        DeleteObject(boldFont);
        DeleteObject(faceBrush);
        DeleteObject(whiteBrush);
        std::cout << "ALL " << behaviorChecks << " UI BEHAVIOR CHECKS PASSED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
