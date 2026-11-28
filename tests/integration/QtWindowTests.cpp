// The Qt window, used through its own widgets the way a person would, without
// a screen (QT_QPA_PLATFORM=offscreen): what the document became is checked.

#include <QAction>
#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QScrollBar>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <random>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Application.h"
#include "AttributeEditor.h"
#include "AvatarDialog.h"
#include "Expect.h"
#include "MainWindow.h"
#include "Outliner.h"
#include "ParametersPanel.h"
#include "SpaceView.h"
#include "TableView.h"
#include "Theme.h"
#include "ValueField.h"
#include "twintailor/core/Fields.h"
#include "twintailor/core/File.h"
#include "twintailor/core/Text.h"
#include "twintailor/session/Samples.h"
#include "twintailor/sim/AvatarImport.h"
#include "twintailor/sim/Layout.h"

using namespace twintailor;
using namespace twintailor::ui;

// How the system's wheel comes into Qt: declared here as Qt's own test headers
// declare it (QtTest/qtestwheel.h).
QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleWheelEvent(QWindow* window, const QPointF& local, const QPointF& global, QPoint pixelDelta, QPoint angleDelta,
                                      Qt::KeyboardModifiers mods, Qt::ScrollPhase phase);
QT_END_NAMESPACE

namespace {

// One application for the whole run, as Qt wants. It is never deleted: on
// the off-screen platform, closing it after mouse events were sent crashes
// as the program ends (seen with Qt 6.2), after every test has passed.
Application& application() {
    static int argc = 1;
    static char name[] = "twintailor_qt_window_tests";
    static char* argv[] = {name, nullptr};
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static Application& app = *new Application(argc, argv);
    static const bool themed = (theme::apply(app), true);
    (void)themed;
    return app;
}

// The window with the T-shirt body open, the cloth stepping once a tick.
struct Window {
    QApplication& app = application();  // first: members are made in this order
    MainWindow window{session::Threading::OneStepPerAdvance};
    Window() {
        window.openSample(5);
        window.show();
        waitForTheBody();
    }
    // An avatar is there at once; working out how to collide with it takes a
    // moment, and longer in a build that is not optimised.
    void waitForTheBody() {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!window.session().bodyReady() && std::chrono::steady_clock::now() < limit) {
            window.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(window.session().bodyReady());
        window.tick();
    }
    const Document& doc() const { return window.session().document(); }
    PanelId panel(const std::string& name) const {
        for (PanelId p : doc().panels())
            if (doc().get(p).name == name) return p;
        FAIL("no panel " << name);
    }
    Param parameter(const std::string& name) const {
        for (Param p : doc().parameters())
            if (doc().get(p).name == name) return p;
        FAIL("no parameter " << name);
    }
};

// Types into a field and presses Enter, as a person does.
void type(QLineEdit* field, const QString& text) {
    REQUIRE(field != nullptr);
    field->setFocus();
    field->setText(text);
    emit field->editingFinished();
}

// Types into the box that opens on the table over an edge's length, and presses Enter.
void typeOnTable(TableView* table, const QString& text) {
    REQUIRE(table->lengthEditor() != nullptr);
    type(table->lengthEditor(), text);
    emit table->lengthEditor()->returnPressed();
}

template <class T> OpId first(const Document& doc, PanelId panel) {
    for (OpId op : doc.operations(panel))
        if (std::holds_alternative<T>(doc.get(op))) return op;
    FAIL("no such operation");
    return {};
}

} // namespace

TEST_CASE("the window opens with its shelf, docks and two views", "[qt]")
{
    Window w;
    EXPECT((w.window.shelf()->tabCount() == 3), "the shelf has its Draft, Sew and Simulate tabs");
    for (const char* name : {"Outliner", "Parameters", "Attribute Editor"}) EXPECT((w.window.dock(name) != nullptr), std::string("there is a dock ") + name);
    EXPECT((w.window.outliner()->panelsShown().size() == 2), "the Outliner lists the sample's two panels");
    EXPECT((w.window.parameters()->field(w.parameter("chest"))->text() == "96 cm"), "a parameter shows its unit");
    EXPECT((w.window.space()->isVisible() && w.window.table()->isVisible()), "both views are shown, side by side");
}

TEST_CASE("undo, play and reset sit in a row under the menu; the tools and view layouts in a toolbox at the left; no button is shown twice", "[qt]")
{
    Window w;
    Shelf& shelf = *w.window.shelf();
    const auto shownIn = [&](QAction* action) {
        int bars = 0;
        for (QToolBar* bar : w.window.findChildren<QToolBar*>())
            if (bar->actions().contains(action)) ++bars;
        return bars;
    };
    EXPECT((w.window.toolBarArea(shelf.actionRow()) == Qt::TopToolBarArea), "the action row is at the top, above the shelf");
    EXPECT((w.window.toolBarArea(shelf.toolbox()) == Qt::LeftToolBarArea), "the toolbox is at the left edge, beside the docks");
    EXPECT((shelf.actionRow()->geometry().top() < shelf.parentWidget()->geometry().top()), "and the action row is above the shelf");
    EXPECT((shelf.toolbox()->geometry().right() <= w.window.dock("Outliner")->geometry().left()), "the toolbox is left of the Outliner");
    for (QAction* action : {shelf.undo(), shelf.redo(), shelf.simulate(), shelf.reset()})
        EXPECT((shelf.actionRow()->actions().contains(action) && shownIn(action) == 1), "undo, redo, simulate and reset are in the action row only");
    for (Tool tool : {Tool::Select, Tool::Move, Tool::Rotate, Tool::Scale})
        EXPECT((shelf.toolbox()->actions().contains(shelf.action(tool)) && shownIn(shelf.action(tool)) == 1), std::string(nameOf(tool)) + " is in the toolbox only");
    for (ViewLayout layout : {ViewLayout::SpaceOnly, ViewLayout::TableOnly, ViewLayout::SideBySide, ViewLayout::Stacked})
        EXPECT((shelf.toolbox()->actions().contains(shelf.layout(layout))), "the toolbox chooses how the views share the window");
    for (int tool = 0; tool <= static_cast<int>(Tool::Pull); ++tool)
        EXPECT((shownIn(shelf.action(static_cast<Tool>(tool))) == 1), std::string(nameOf(static_cast<Tool>(tool))) + " is shown in one place");
}

TEST_CASE("the toolbox keeps the tools on top and the view layouts at the bottom; the two views share the middle in half", "[qt]")
{
    Window w;
    Shelf& shelf = *w.window.shelf();
    QWidget* scale = shelf.toolbox()->widgetForAction(shelf.action(Tool::Scale));
    QWidget* first = shelf.toolbox()->widgetForAction(shelf.layout(ViewLayout::SpaceOnly));
    QWidget* last = shelf.toolbox()->widgetForAction(shelf.layout(ViewLayout::Stacked));
    REQUIRE((scale && first && last));
    EXPECT((scale->geometry().bottom() < shelf.toolbox()->height() / 2), "the tools are in the top half of the toolbox");
    EXPECT((first->geometry().top() > shelf.toolbox()->height() / 2), "the view layouts are in the bottom half");
    EXPECT((shelf.toolbox()->height() - last->geometry().bottom() < 120), "and the last one is close to the bottom");

    const auto half = [&] {
        const double space = w.window.space()->width(), table = w.window.table()->width();
        return std::abs(space - table) < 0.05 * (space + table);
    };
    EXPECT((half()), "side by side: the 3D view and the table are each half");
    w.window.shelf()->layout(ViewLayout::Stacked)->trigger();
    const double up = w.window.space()->height(), down = w.window.table()->height();
    EXPECT((std::abs(up - down) < 0.05 * (up + down)), "stacked: each is half the height");
    w.window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    EXPECT((half()), "and half again when put side by side again");
}

TEST_CASE("the Attribute Editor opens without a horizontal scroll bar, in it or in its list of layers", "[qt]")
{
    Window w;
    AttributeEditor& editor = *w.window.attributes();
    const auto scrolls = [&](QScrollArea* area) {
        return area->horizontalScrollBar()->isVisible() || area->widget()->minimumSizeHint().width() > area->viewport()->width();
    };
    QScrollArea* layers = editor.findChild<QScrollArea*>("layerScroll");
    REQUIRE(layers != nullptr);
    for (const char* name : {"front", "back"}) {
        w.window.select(w.panel(name));
        w.window.tick();
        QApplication::processEvents();
        EXPECT((!scrolls(&editor)), std::string("the editor of ") + name + " fits its width as it opens");
        EXPECT((!scrolls(layers)), std::string("and so does its list of layers (") + name + ")");
    }
    w.window.select(std::nullopt);
    w.window.tick();
    EXPECT((!scrolls(&editor)), "nor does it with nothing selected");
}

TEST_CASE("Space starts and stops the simulation, from the 3D view as well; it keeps its state until Reset", "[qt]")
{
    Window w;
    QAction* simulate = w.window.shelf()->simulate();
    EXPECT((simulate->shortcut() == QKeySequence("Space")), "Space is the simulation's key");
    w.window.space()->key(QKeyCombination(Qt::Key_Space));
    EXPECT((simulate->isChecked() && simulate->text() == "Pause"), "Space starts it, and the button now says Pause");
    for (int i = 0; i < 10; ++i) w.window.tick();
    const double time = w.window.session().cloth().time;
    EXPECT((time > 0.0), "the cloth moves");
    w.window.space()->key(QKeyCombination(Qt::Key_Space));
    EXPECT((!simulate->isChecked() && simulate->text() == "Simulate"), "Space again pauses it");
    for (int i = 0; i < 10; ++i) w.window.tick();
    EXPECT((w.window.session().cloth().time == time), "paused, the cloth stands still");
    w.window.space()->key(QKeyCombination(Qt::Key_Space));
    for (int i = 0; i < 10; ++i) w.window.tick();
    EXPECT((w.window.session().cloth().time > time), "and starts again where it was");
}

TEST_CASE("Reset is undone from the window, and a redone one stops the cloth", "[qt]")
{
    Window w;
    const session::Session& session = w.window.session();
    QAction* simulate = w.window.shelf()->simulate();
    simulate->setChecked(true);
    for (int i = 0; i < 20; ++i) w.window.tick();
    simulate->setChecked(false);
    w.window.tick();
    const double time = session.cloth().time;
    w.window.shelf()->reset()->trigger();
    w.window.tick();
    EXPECT((!session.clothShown() && w.window.helpLine().contains("Ctrl+Z")), "Reset takes the cloth off, and says how to get it back");
    EXPECT(w.window.shelf()->undo()->isEnabled(), "there is something to undo");
    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((session.clothShown() && session.cloth().time == time && !simulate->isChecked()), "undo brings the drape back, standing still");

    simulate->setChecked(true);
    w.window.tick();
    w.window.shelf()->redo()->trigger();
    w.window.tick();
    EXPECT((!session.clothShown() && !session.simulating() && !simulate->isChecked() && simulate->text() == "Simulate"),
           "redone, the cloth is off and the button offers to play");
}

TEST_CASE("a chosen panel's operations show as layers, newest on top", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.outliner()->choose(front);
    EXPECT((w.window.selected() == front), "choosing a panel in the Outliner selects it");
    const std::vector<OpId> layers = w.window.attributes()->layersShown();
    REQUIRE(layers.size() == w.doc().operations(front).size());
    EXPECT((std::holds_alternative<Mirror>(w.doc().get(layers.front()))), "a mirror applied last is the top layer");
    EXPECT((std::holds_alternative<Rect>(w.doc().get(layers.back()))), "the base rectangle is at the bottom");
    EXPECT((std::holds_alternative<Bezier>(w.doc().get(layers[1]))), "below the mirror, the newest step");
}

TEST_CASE("every layer shows its fields until it is closed with a click on its name", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    AttributeEditor& editor = *w.window.attributes();
    const OpId rect = first<Rect>(w.doc(), front);
    const OpId armhole = first<Curve>(w.doc(), front);
    EXPECT((editor.isOpen(rect) && editor.isOpen(armhole)), "layers are open by default");
    EXPECT((editor.field(rect, FieldId{0}) != nullptr && editor.field(armhole, FieldId{0}) != nullptr), "and show their fields together");
    editor.toggleLayer(armhole);
    EXPECT((!editor.isOpen(armhole) && editor.field(armhole, FieldId{0}) == nullptr && editor.isOpen(rect)), "a click closes that layer only");
    editor.toggleLayer(armhole);
    EXPECT((editor.isOpen(armhole)), "a second click opens it again");
}

TEST_CASE("a layer is switched off with its eye, and undo brings it back", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    const OpId armhole = first<Curve>(w.doc(), front);
    w.window.attributes()->switchLayer(armhole, false);
    EXPECT((!w.doc().enabled(armhole)), "the eye switches the layer off");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().enabled(armhole)), "undo switches it on again");
}

TEST_CASE("values are typed with or without their unit; what does not read is refused", "[qt]")
{
    Window w;
    const Param chest = w.parameter("chest");
    ValueField* field = w.window.parameters()->field(chest);
    type(field, "104");
    EXPECT((asNumber(w.doc().get(chest).value) == 104.0), "a bare number is centimetres");
    EXPECT((field->text() == "104 cm"), "and is shown with its unit");
    type(field, "1 m");
    EXPECT((asNumber(w.doc().get(chest).value) == 104.0 && field->property("refused").toBool()), "what does not read changes nothing and is marked");
    EXPECT((!w.window.helpLine().isEmpty()), "the help line says why");
    type(field, "= length + 30");
    EXPECT((!asNumber(w.doc().get(chest).value).has_value()), "a formula is taken");
    EXPECT((field->text().startsWith("=")), "and shown as one");
}

TEST_CASE("an open layer's fields show their units; a change that breaks the panel is refused", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    OpId underarm;
    for (OpId op : w.doc().operations(front))
        if (const AddPoint* point = std::get_if<AddPoint>(&w.doc().get(op)); point && point->at.edge.role == static_cast<uint16_t>(Rect::Edge::Right)) underarm = op;
    w.window.attributes()->openLayer(underarm);
    REQUIRE(w.window.attributes()->isOpen(underarm));
    ValueField* position = w.window.attributes()->field(underarm, FieldId{0});
    REQUIRE(position != nullptr);
    std::printf("      the underarm point's position shows as \"%s\"\n", position->text().toStdString().c_str());
    EXPECT((position->text().startsWith("=")), "it is a formula, shown as one");
    type(position, "50");
    EXPECT((asNumber(std::get<AddPoint>(w.doc().get(underarm)).at.along) == 0.5), "50 in a fraction is 50 %");
    EXPECT((w.window.attributes()->field(underarm, FieldId{0})->text() == "50 %"), "and is shown so");

    const OpId rect = first<Rect>(w.doc(), front);
    w.window.attributes()->openLayer(rect);
    type(w.window.attributes()->field(rect, FieldId{0}), "-5");
    EXPECT((asNumber(std::get<Rect>(w.doc().get(rect)).width) != -5.0), "a width below nothing is refused");
    EXPECT((w.window.helpLine().startsWith("Not changed")), "and the help line says so");
}

TEST_CASE("placement is typed in space; the views can be laid out", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    type(w.window.attributes()->spaceField(session::SpaceField::Y), "120");
    EXPECT((w.doc().get(front).arrangement.at.y == 120.0), "y in space is typed in cm");
    type(w.window.attributes()->spaceField(session::SpaceField::Wrap), "160");
    EXPECT((w.doc().get(front).arrangement.wrap == 160.0), "wrap in degrees");

    w.window.shelf()->layout(ViewLayout::Stacked)->trigger();
    EXPECT((w.window.viewLayout() == ViewLayout::Stacked && w.window.space()->isVisible() && w.window.table()->isVisible()), "stacked shows both");
    w.window.shelf()->layout(ViewLayout::SpaceOnly)->trigger();
    EXPECT((w.window.space()->isVisible() && !w.window.table()->isVisible()), "3D view only shows the 3D view");
    w.window.shelf()->layout(ViewLayout::TableOnly)->trigger();
    EXPECT((!w.window.space()->isVisible() && w.window.table()->isVisible()), "table only shows the table");
    w.window.shelf()->layout(ViewLayout::SpaceOnly)->trigger();
    EXPECT((w.window.viewLayout() == ViewLayout::SpaceOnly && !w.window.table()->isVisible()), "and each stays, whichever view was used last");
    w.window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    EXPECT((w.window.space()->isVisible() && w.window.table()->isVisible()), "side by side brings both back");
}

TEST_CASE("simulate runs the cloth; copy and instance add a panel beside", "[qt]")
{
    Window w;
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 20; ++i) w.window.tick();
    EXPECT((w.window.session().clothShown() && w.window.session().cloth().time > 0.2), "Simulate lets the cloth fall");
    w.window.shelf()->reset()->trigger();
    w.window.tick();
    EXPECT((!w.window.shelf()->simulate()->isChecked()), "Reset stops it");

    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.outliner()->copy(front);
    EXPECT((w.doc().panels().size() == 3 && w.window.selected() != front), "Copy adds a panel and selects it");
    w.window.select(front);
    w.window.shelf()->instance()->trigger();
    EXPECT((w.doc().panels().size() == 4 && std::holds_alternative<Instance>(w.doc().get(w.doc().operations(*w.window.selected()).front()))), "Instance adds one that follows");
    EXPECT((w.window.outliner()->panelsShown().size() == 4), "and the Outliner shows them");
}

// ---- The table's gestures ----

namespace {

// A plain rectangle of numbers, and one whose width is a formula.
struct Table : Window {
    Param widthParameter;
    Table() {
        Document doc;
        widthParameter = doc.add(Parameter{.name = "width", .value = 20_cm});
        const PanelId plain = doc.add(Panel{.name = "plain"});
        doc.add(plain, Rect{.width = 30_cm, .height = 10_cm});
        const PanelId driven = doc.add(Panel{.name = "driven"});
        doc.add(driven, Rect{.width = widthParameter, .height = 10_cm});
        window.open(std::move(doc), "table");
        window.tick();
    }
    TableView* table() const { return window.table(); }
    // Where a panel's corner is on the table: the one farthest along (x, y).
    Vec2 corner(PanelId panel, double x, double y) const {
        const Pattern& shape = window.session().editor().shapes()[panel];
        const Panel& placed = doc().get(panel);
        Vec2 best{};
        double most = -1e300;
        for (const Vertex& v : shape.vertices)
            if (const double along = v.position.x * x + v.position.y * y; along > most) most = along, best = v.position;
        const Vec2 on = turned(best, placed.angle);
        return on + placed.at;
    }
    Vec2 middle(PanelId panel) const {
        const std::optional<Bounds> box = bounds(window.session().editor().shapes()[panel]);
        const Panel& placed = doc().get(panel);
        return {placed.at.x + (box->min.x + box->max.x) / 2, placed.at.y + (box->min.y + box->max.y) / 2};
    }
    double width(PanelId panel) const {
        const std::optional<Bounds> box = bounds(window.session().editor().shapes()[panel]);
        return box->max.x - box->min.x;
    }
};

void send(QWidget* widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons held, Qt::KeyboardModifiers keys) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, held, keys);
    QApplication::sendEvent(widget, &event);
}

void click(TableView* table, QPointF at, Qt::KeyboardModifiers keys = {}) {
    send(table, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, keys);
    send(table, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, keys);
}

// Presses, moves there in a few steps, and lets go, as a hand does.
void drag(TableView* table, QPointF from, QPointF to, Qt::KeyboardModifiers keys = {}) {
    send(table, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, keys);
    for (int i = 1; i <= 5; ++i) send(table, QEvent::MouseMove, from + (to - from) * (i / 5.0), Qt::NoButton, Qt::LeftButton, keys);
    send(table, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, keys);
}

QPointF by(QPointF at, TableView* table, double xCm, double yCm) { return at + QPointF(xCm * table->scale(), -yCm * table->scale()); }

} // namespace

TEST_CASE("a panel is dragged on the table as one undo step; Ctrl puts it on the grid", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    const Panel was = w.doc().get(plain);
    const QPointF at = w.table()->toScreen(w.middle(plain));
    drag(w.table(), at, by(at, w.table(), 12, -4));
    EXPECT((w.window.selected() == plain), "pressing a panel selects it");
    EXPECT((std::abs(w.doc().get(plain).at.x - (was.at.x + 12)) < 0.2 && std::abs(w.doc().get(plain).at.y - (was.at.y - 4)) < 0.2), "it moves with the pointer");
    EXPECT((w.width(plain) == 30.0), "placement is not shape");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(plain).at.x == was.at.x && w.doc().get(plain).at.y == was.at.y), "one undo puts it back");

    drag(w.table(), at, by(at, w.table(), 7.3, 2.1), Qt::ControlModifier);
    // The grid's step at this zoom, as the table draws it.
    double step = 0.1;
    while (step * w.table()->scale() < 12.0) step *= 10.0;
    const double x = w.doc().get(plain).at.x, y = w.doc().get(plain).at.y;
    EXPECT((std::abs(x / step - std::round(x / step)) < 1e-6 && std::abs(y / step - std::round(y / step)) < 1e-6), "Ctrl puts its place on the grid");

    click(w.table(), w.table()->toScreen({-1000, -1000}));
    EXPECT((!w.window.selected()), "a click on the empty table selects nothing");
}

TEST_CASE("a corner is dragged on the table; one held by a formula stays and says why", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const QPointF corner = w.table()->toScreen(w.corner(plain, 1, 1));
    drag(w.table(), corner, by(corner, w.table(), 5, 0));
    EXPECT((std::abs(w.width(plain) - 35.0) < 0.2), "dragging the far corner widens the panel");
    EXPECT((std::holds_alternative<AdjustPoint>(w.doc().get(w.doc().operations(plain).back()))), "as a layer that moves the corner");
    EXPECT((w.window.selectedCorners().size() == 1), "and the corner is selected");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.width(plain) == 30.0 && w.doc().operations(plain).size() == 1), "one undo takes the whole drag back");

    const PanelId driven = w.panel("driven");
    w.window.select(driven);
    const QPointF held = w.table()->toScreen(w.corner(driven, 1, 1));
    const std::size_t layers = w.doc().operations(driven).size();
    drag(w.table(), held, by(held, w.table(), 5, 0));
    EXPECT((w.doc().operations(driven).size() == layers && w.width(driven) == 20.0), "a corner a formula places does not move");
    EXPECT((w.window.helpLine().contains("formula")), "and the help line says why");
}

TEST_CASE("Shift held while a corner is dragged keeps it on the axis it first moved along; Ctrl snaps the other", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const Vec2 top = w.corner(plain, 1, 1);
    const QPointF from = w.table()->toScreen(top);

    // The hand drifts upwards as well, but the corner only goes sideways.
    drag(w.table(), from, by(from, w.table(), 6, 2.5), Qt::ShiftModifier);
    EXPECT((std::abs(w.width(plain) - 36.0) < 0.2), "the corner followed the pointer along x");
    EXPECT((std::abs(w.corner(plain, 1, 1).y - top.y) < 1e-9), "its y did not change by even 1e-9 cm");
    EXPECT((w.window.selectedCorners().size() == 1), "and it is selected, as after any drag of a corner");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.width(plain) == 30.0 && w.doc().operations(plain).size() == 1), "one undo takes the whole drag back");

    // Mostly upwards, it goes only up.
    drag(w.table(), from, by(from, w.table(), 1.5, 5), Qt::ShiftModifier);
    EXPECT((std::abs(w.corner(plain, 1, 1).x - top.x) < 1e-9 && std::abs(w.corner(plain, 1, 1).y - top.y - 5.0) < 0.2), "dragged mostly upwards, it only goes up");
    w.window.shelf()->undo()->trigger();

    // With Ctrl as well the free coordinate is on the grid and the other stays exactly where it was.
    drag(w.table(), from, by(from, w.table(), 7.3, 2.1), Qt::ShiftModifier | Qt::ControlModifier);
    double step = 0.1;
    while (step * w.table()->scale() < 12.0) step *= 10.0;
    const double x = w.corner(plain, 1, 1).x;
    EXPECT((std::abs(x / step - std::round(x / step)) < 1e-6 && std::abs(w.corner(plain, 1, 1).y - top.y) < 1e-9), "Ctrl snaps x to the grid; y stays");
}

TEST_CASE("Shift-drag selects corners in a box; dragging one moves them together", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    // A box around the right side's two corners.
    const Vec2 top = w.corner(plain, 1, 1), bottom = w.corner(plain, 1, -1);
    drag(w.table(), w.table()->toScreen({top.x - 2, top.y + 2}), w.table()->toScreen({bottom.x + 2, bottom.y - 2}), Qt::ShiftModifier);
    EXPECT((w.window.selectedCorners().size() == 2), "the box selects the two corners inside it");
    const QPointF from = w.table()->toScreen(top);
    drag(w.table(), from, by(from, w.table(), 6, 0));
    EXPECT((std::abs(w.width(plain) - 36.0) < 0.2), "both move: the panel is wider all the way down");
    click(w.table(), w.table()->toScreen(w.corner(plain, -1, 1)), Qt::ShiftModifier);
    EXPECT((w.window.selectedCorners().size() == 3), "Shift-click adds a corner");
}

TEST_CASE("a box dragged on empty table selects the chosen panel's corners when it touches only that panel; Shift adds; a click chooses nothing", "[qt]")
{
    Table w;
    Document alone;
    alone.add(alone.add(Panel{.name = "plain"}), Rect{.width = 30_cm, .height = 10_cm});
    w.window.open(std::move(alone), "alone");
    w.window.tick();
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const Vec2 topRight = w.corner(plain, 1, 1), bottomRight = w.corner(plain, 1, -1);
    const Vec2 topLeft = w.corner(plain, -1, 1);
    // Pressed outside the panel, so it is a box and not a drag of the panel.
    drag(w.table(), w.table()->toScreen({topRight.x + 6, topRight.y + 8}), w.table()->toScreen({bottomRight.x - 2, bottomRight.y - 8}));
    EXPECT((w.window.selectedCorners().size() == 2 && w.window.selected() == plain), "a box over the right side selects its two corners, with no Shift");
    const std::size_t layers = w.doc().operations(plain).size();
    EXPECT((layers == 1), "and changes nothing in the document");

    drag(w.table(), w.table()->toScreen({topLeft.x - 6, topLeft.y + 6}), w.table()->toScreen({topLeft.x + 2, topLeft.y - 2}), Qt::ShiftModifier);
    EXPECT((w.window.selectedCorners().size() == 3), "with Shift a box adds its corner to the ones selected");
    drag(w.table(), w.table()->toScreen({topLeft.x - 6, topLeft.y + 6}), w.table()->toScreen({topLeft.x + 2, topLeft.y - 2}), Qt::ShiftModifier);
    EXPECT((w.window.selectedCorners().size() == 3), "and a box over one already selected does not take it out");

    drag(w.table(), w.table()->toScreen({topLeft.x - 6, topLeft.y + 6}), w.table()->toScreen({topLeft.x + 2, topLeft.y - 2}));
    EXPECT((w.window.selectedCorners().size() == 1), "without Shift a box replaces them");
    click(w.table(), w.table()->toScreen({-1000, -1000}));
    EXPECT((!w.window.selected() && w.window.selectedCorners().empty()), "a click on empty table chooses nothing");
}

TEST_CASE("a double-click on an edge opens its length for typing", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    // The bottom edge, halfway along.
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    const QPointF at = w.table()->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2});
    send(w.table(), QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(w.table()->lengthEditor() != nullptr);
    EXPECT((w.table()->lengthEditor()->text() == "30 cm"), "a box opens on the table, with the length in it");
    EXPECT((!w.window.attributes()->hasFocus() && !w.window.attributes()->isAncestorOf(QApplication::focusWidget())), "and the Attribute Editor is not where it is typed");
    typeOnTable(w.table(), "42");
    EXPECT((w.width(plain) == 42.0), "and the typed length is the edge's");
    EXPECT((w.table()->lengthEditor() == nullptr), "the box closes");

    send(w.table(), QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(w.table()->lengthEditor() != nullptr);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(w.table()->lengthEditor(), &escape);
    EXPECT((w.table()->lengthEditor() == nullptr && w.width(plain) == 42.0), "Esc leaves it as it was");
    send(w.table(), QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(w.table()->lengthEditor() != nullptr);
    typeOnTable(w.table(), "no length");
    EXPECT((w.width(plain) == 42.0 && w.table()->lengthEditor() != nullptr && !w.table()->lengthCard()->problem().isEmpty()), "a length that does not read changes nothing, is said on the bar, and the bar stays");
}

TEST_CASE("a double-click on an edge nothing sets gives it a length of its own; as a formula it holds the edge's corners", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    // The top-right corner dragged in: the top edge is no longer the rectangle's width.
    const QPointF corner = w.table()->toScreen(w.corner(plain, 1, 1));
    drag(w.table(), corner, by(corner, w.table(), -5, 0));
    const std::size_t layers = w.doc().operations(plain).size();
    REQUIRE(layers == 2);

    const Vec2 left = w.corner(plain, -1, 1), right = w.corner(plain, 1, 1);
    send(w.table(), QEvent::MouseButtonDblClick, w.table()->toScreen({(left.x + right.x) / 2, (left.y + right.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(w.doc().operations(plain).size() == layers + 1);
    const OpId set = w.doc().operations(plain).back();
    REQUIRE(std::holds_alternative<SetLength>(w.doc().get(set)));
    const EdgeRef top = std::get<SetLength>(w.doc().get(set)).edge;
    const auto topLength = [&] { return w.window.session().editor().shapes()[plain].length(top); };
    EXPECT((std::abs(topLength() - (right.x - left.x)) < 1e-6), "the new length is as long as the edge was, so the shape did not change");
    typeOnTable(w.table(), "12");
    EXPECT_NEAR((topLength()), (12.0), "the typed length is the edge's");

    w.table()->grab();  // painted again, so that the labels are where they are now
    const auto topLabel = std::find_if(w.table()->lengthLabels().begin(), w.table()->lengthLabels().end(), [&](const auto& l) { return l.second == top; });
    REQUIRE(topLabel != w.table()->lengthLabels().end());
    send(w.table(), QEvent::MouseButtonDblClick, topLabel->first.center(), Qt::LeftButton, Qt::LeftButton, {});
    typeOnTable(w.table(), "= width");
    EXPECT_NEAR((topLength()), (20.0), "a formula gives it as well");
    const Vec2 held = w.corner(plain, 1, 3);  // the top-right corner: the top is now shorter than the bottom
    const QPointF where = w.table()->toScreen(held);
    drag(w.table(), where, by(where, w.table(), 3, 0));
    EXPECT((w.doc().operations(plain).size() == layers + 1 && std::abs(w.corner(plain, 1, 3).x - held.x) < 1e-9), "an end of an edge a formula sets is not dragged");
    EXPECT((w.window.helpLine().contains("formula")), "and the help line says why");
}

// ---- The drawing tools on the table ----

namespace {

void choose(Table& w, Tool tool) { w.window.shelf()->action(tool)->trigger(); }

Vec2 between(Vec2 a, Vec2 b, double along) { return {a.x + (b.x - a.x) * along, a.y + (b.y - a.y) * along}; }

template <class T> bool lastIs(const Document& doc, PanelId panel) {
    const std::vector<OpId> operations = doc.operations(panel);
    return !operations.empty() && std::holds_alternative<T>(doc.get(operations.back()));
}

} // namespace

TEST_CASE("Q, W, E and R choose Select, Move, Rotate and Scale; only the last three show a gizmo, in both views", "[qt]")
{
    Table w;
    w.window.select(w.panel("plain"));
    for (const auto& [tool, key] : {std::pair{Tool::Select, "Q"}, std::pair{Tool::Move, "W"}, std::pair{Tool::Rotate, "E"}, std::pair{Tool::Scale, "R"}})
        EXPECT((w.window.shelf()->action(tool)->shortcut() == QKeySequence(key)), std::string(nameOf(tool)) + " is on " + key + ", as in Maya");
    EXPECT((w.window.shelf()->action(Tool::Rectangle)->shortcut().isEmpty()), "Rectangle has no key: R scales");
    w.window.tick();
    EXPECT((!w.table()->gizmo() && !w.window.space()->gizmo()), "Select shows no gizmo");

    // Keys typed in the 3D view reach the shelf, whichever surface it draws on.
    SpaceView& space = *w.window.space();
    space.key(QKeyCombination(Qt::Key_E));
    w.window.tick();
    EXPECT((w.window.shelf()->tool() == Tool::Rotate), "E in the 3D view chooses Rotate");
    EXPECT((w.table()->gizmo() && w.table()->gizmo()->mode == session::GizmoMode::Rotate), "the table shows the rotate gizmo");
    EXPECT((space.gizmo() && space.gizmo()->mode == session::GizmoMode::Rotate), "and so does the 3D view");
    space.key(QKeyCombination(Qt::Key_R));
    EXPECT((w.window.shelf()->tool() == Tool::Scale && space.gizmo()->mode == session::GizmoMode::Scale), "R: Scale");
    space.key(QKeyCombination(Qt::Key_W));
    EXPECT((w.window.shelf()->tool() == Tool::Move && w.table()->gizmo()->mode == session::GizmoMode::Move), "W: Move");
    space.key(QKeyCombination(Qt::Key_Q));
    EXPECT((w.window.shelf()->tool() == Tool::Select && !space.gizmo() && !w.table()->gizmo()), "Q: back to Select, no gizmo");
}

TEST_CASE("Shift held while a panel is dragged keeps it on the axis it first moved along; Ctrl snaps the other", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();
    const Panel was = w.doc().get(plain);
    const QPointF at = table->toScreen(w.middle(plain));

    // The hand drifts upwards as well, but the panel only goes sideways.
    drag(table, at, by(at, table, 12, 2.5), Qt::ShiftModifier);
    EXPECT((std::abs(w.doc().get(plain).at.x - (was.at.x + 12)) < 0.2), "the panel followed the pointer along x");
    EXPECT((w.doc().get(plain).at.y == was.at.y), "and its y did not change at all");
    EXPECT((w.window.selected() == plain), "it is still selected: Shift on a selected panel is not a click");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(plain).at.x == was.at.x), "one undo puts it back");

    drag(table, at, by(at, table, 2, -9), Qt::ShiftModifier);
    EXPECT((w.doc().get(plain).at.x == was.at.x && std::abs(w.doc().get(plain).at.y - (was.at.y - 9)) < 0.2), "dragged mostly down, it only goes down");
    w.window.shelf()->undo()->trigger();

    drag(table, at, by(at, table, 7.3, 2.1), Qt::ShiftModifier | Qt::ControlModifier);
    double step = 0.1;
    while (step * table->scale() < 12.0) step *= 10.0;
    const double x = w.doc().get(plain).at.x;
    EXPECT((std::abs(x / step - std::round(x / step)) < 1e-6 && w.doc().get(plain).at.y == was.at.y), "with Ctrl as well, x is on the grid and y stays");
    w.window.shelf()->undo()->trigger();

    // The gizmo goes with the panel while it is dragged, not only when the button is let go.
    choose(w, Tool::Select);
    choose(w, Tool::Move);
    const Vec2 centreWas = table->gizmo()->centre;
    const QPointF arrow = table->toScreen(session::handleEnd(*table->gizmo(), session::TableHandle::AlongX)) - QPointF(25.0, 0.0);
    send(table, QEvent::MouseButtonPress, arrow, Qt::LeftButton, Qt::LeftButton, {});
    send(table, QEvent::MouseMove, arrow + QPointF(60.0, 0.0), Qt::NoButton, Qt::LeftButton, {});
    send(table, QEvent::MouseMove, arrow + QPointF(120.0, 0.0), Qt::NoButton, Qt::LeftButton, {});
    REQUIRE(table->gizmoDrawn());
    EXPECT((std::abs(table->gizmoDrawn()->centre.x - centreWas.x - 120.0 / table->scale()) < 0.06 && table->gizmoDrawn()->centre.y == centreWas.y),
           "mid-drag, the gizmo has moved with the pointer along x");
    EXPECT((std::abs(table->gizmoDrawn()->centre.x - table->gizmo()->centre.x) < 1e-9), "and it stands where the panel's gizmo does");
    send(table, QEvent::MouseButtonRelease, arrow + QPointF(120.0, 0.0), Qt::LeftButton, Qt::NoButton, {});
    w.window.shelf()->undo()->trigger();

    // A ring does not move while it turns the panel: the turn is measured about it.
    choose(w, Tool::Rotate);
    const QPointF pivotNow = table->toScreen(table->gizmo()->centre);
    send(table, QEvent::MouseButtonPress, pivotNow + QPointF(session::tableRingPixels, 0.0), Qt::LeftButton, Qt::LeftButton, {});
    send(table, QEvent::MouseMove, pivotNow + QPointF(0.0, -session::tableRingPixels), Qt::NoButton, Qt::LeftButton, {});
    EXPECT((table->gizmoDrawn() && std::abs(table->gizmoDrawn()->centre.x - table->gizmo()->centre.x) < 1e-9), "the ring stays at the middle of the panel mid-turn");
    send(table, QEvent::MouseButtonRelease, pivotNow + QPointF(0.0, -session::tableRingPixels), Qt::LeftButton, Qt::NoButton, {});
    w.window.shelf()->undo()->trigger();
    choose(w, Tool::Move);

    // The middle of the move gizmo is a free move: Shift holds it too.
    choose(w, Tool::Move);
    REQUIRE(table->gizmo());
    const QPointF middle = table->toScreen(table->gizmo()->centre);
    drag(table, middle, by(middle, table, 3, 9), Qt::ShiftModifier);
    EXPECT((w.doc().get(plain).at.x == was.at.x && std::abs(w.doc().get(plain).at.y - (was.at.y + 9)) < 0.2), "the gizmo's middle goes up only");
}

TEST_CASE("on the table the move gizmo's arrow moves a panel along x only, its ring turns it, its handle scales it", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();

    choose(w, Tool::Move);
    REQUIRE(table->gizmo());
    const Panel was = w.doc().get(plain);
    const QPointF onArrow = table->toScreen(session::handleEnd(*table->gizmo(), session::TableHandle::AlongX)) - QPointF(25.0, 0.0);
    drag(table, onArrow, onArrow + QPointF(40.0, -30.0));
    EXPECT((std::abs(w.doc().get(plain).at.x - (was.at.x + 40.0 / table->scale())) < 0.06), "the x arrow moves the panel along x");
    EXPECT((w.doc().get(plain).at.y == was.at.y), "and not along y");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(plain).at.x == was.at.x), "the drag is one step to undo");

    // A quarter turn, from the ring's right to its top, in steps of 15° with Ctrl.
    choose(w, Tool::Rotate);
    REQUIRE(table->gizmo());
    const Vec2 middle = w.middle(plain);
    const QPointF pivot = table->toScreen(middle);
    drag(table, pivot + QPointF(session::tableRingPixels, 0.0), pivot + QPointF(0.0, -session::tableRingPixels), Qt::ControlModifier);
    EXPECT((std::abs(w.doc().get(plain).angle - 90.0) < 1e-6), "the ring turns the panel by the angle swept");
    const Vec2 turnedMiddle = [&] {
        const std::optional<Bounds> box = bounds(w.window.session().editor().shapes()[plain]);
        const Vec2 own = turned(Vec2{(box->min.x + box->max.x) / 2, (box->min.y + box->max.y) / 2}, w.doc().get(plain).angle);
        return w.doc().get(plain).at + own;
    }();
    EXPECT((std::abs(turnedMiddle.x - middle.x) < 1e-6 && std::abs(turnedMiddle.y - middle.y) < 1e-6), "about its middle, which stays where it was");
    EXPECT((w.width(plain) == 30.0), "turning is not shape");

    // Turned a quarter, the panel's own x points up the table: so does the scale gizmo's x handle.
    choose(w, Tool::Scale);
    REQUIRE(table->gizmo());
    const QPointF centre = table->toScreen(table->gizmo()->centre);
    const QPointF end = table->toScreen(session::handleEnd(*table->gizmo(), session::TableHandle::AlongX));
    EXPECT((std::abs(end.x() - centre.x()) < 1e-6 && end.y() < centre.y()), "the x handle points up the screen, along the panel's own x");
    const QPointF unit = (end - centre) / session::tableArrowPixels;
    const std::size_t layers = w.doc().operations(plain).size();
    const Rect* shape = std::get_if<Rect>(&w.doc().get(w.doc().operations(plain).front()));
    REQUIRE(shape != nullptr);
    const std::optional<double> heightBefore = asNumber(shape->height);
    drag(table, centre + unit * 60.0, centre + unit * 90.0);
    EXPECT((w.doc().operations(plain).size() == layers), "a rectangle of plain numbers is scaled itself: no layer is added");
    shape = std::get_if<Rect>(&w.doc().get(w.doc().operations(plain).front()));
    REQUIRE(shape != nullptr);
    EXPECT((asNumber(shape->width) == 45.0 && asNumber(shape->height) == heightBefore), "half as far again from the middle: the width is 150 % along x, the height as it was");
    EXPECT((std::abs(w.width(plain) - 45.0) < 1e-6), "the panel is half as wide again");
    const std::optional<Bounds> scaledBox = bounds(w.window.session().editor().shapes()[plain]);
    const Vec2 scaledMiddle = turned(Vec2{(scaledBox->min.x + scaledBox->max.x) / 2, (scaledBox->min.y + scaledBox->max.y) / 2}, w.doc().get(plain).angle);
    EXPECT((std::abs(w.doc().get(plain).at.x + scaledMiddle.x - middle.x) < 1e-6 && std::abs(w.doc().get(plain).at.y + scaledMiddle.y - middle.y) < 1e-6),
           "scaled about its middle, which stays where it was");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().operations(plain).size() == layers && w.width(plain) == 30.0), "undo takes the layer away");
}

TEST_CASE("Rectangle and Circle are dragged out and Ring is a click; each new panel is selected", "[qt]")
{
    Table w;
    TableView* table = w.table();
    choose(w, Tool::Rectangle);
    drag(table, table->toScreen({300, 300}), table->toScreen({320, 315}));
    REQUIRE(w.doc().panels().size() == 3);
    const PanelId drawn = w.doc().panels().back();
    EXPECT((w.window.selected() == drawn && std::abs(w.width(drawn) - 20.0) < 0.6), "a dragged rectangle is a new panel of that size, selected");
    EXPECT((w.window.shelf()->tool() == Tool::Select), "and the shelf goes back to Select, to move it straight away");

    choose(w, Tool::Rectangle);
    click(table, table->toScreen({400, 400}));
    EXPECT((w.doc().panels().size() == 3), "a click without a drag makes nothing");

    choose(w, Tool::Circle);
    drag(table, table->toScreen({400, 300}), table->toScreen({410, 300}));
    REQUIRE(w.doc().panels().size() == 4);
    EXPECT((std::abs(w.width(w.doc().panels().back()) - 20.0) < 1.1), "a circle is dragged from its centre out to its radius");

    choose(w, Tool::Ring);
    click(table, table->toScreen({500, 300}));
    EXPECT((w.doc().panels().size() == 5 && first<RingSector>(w.doc(), w.doc().panels().back()) == w.doc().operations(w.doc().panels().back()).front()), "Ring places a skirt with a click");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().panels().size() == 4), "each is one undo step");
}

TEST_CASE("Point puts a corner on an edge; Curve makes an edge a curve with handles", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    const Vec2 bottomLeft = w.corner(plain, -1, -1), bottomRight = w.corner(plain, 1, -1), topRight = w.corner(plain, 1, 1);
    const std::size_t corners = w.window.session().editor().shapes()[plain].vertices.size();
    choose(w, Tool::Point);
    click(table, table->toScreen(between(bottomLeft, bottomRight, 0.4)));
    EXPECT((lastIs<AddPoint>(w.doc(), plain) && w.window.session().editor().shapes()[plain].vertices.size() == corners + 1), "a click on an edge adds a corner there");
    choose(w, Tool::Point);
    click(table, table->toScreen(bottomRight));
    EXPECT((w.window.session().editor().shapes()[plain].vertices.size() == corners + 1 && w.window.helpLine().contains("corner")), "on a corner it adds nothing and says why");

    choose(w, Tool::Curve);
    click(table, table->toScreen(between(bottomRight, topRight, 0.5)));
    EXPECT((lastIs<Bezier>(w.doc(), plain) && w.window.shelf()->tool() == Tool::Select), "a click on an edge makes it a curve, ready to drag its handles");
}

TEST_CASE("Line joins two points of one outline; Cut takes away the side clicked; Split makes two panels", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    const Vec2 bottomLeft = w.corner(plain, -1, -1), bottomRight = w.corner(plain, 1, -1);
    const Vec2 topLeft = w.corner(plain, -1, 1), topRight = w.corner(plain, 1, 1);
    choose(w, Tool::Line);
    click(table, table->toScreen(between(bottomLeft, bottomRight, 1.0 / 3.0)));
    EXPECT((w.window.helpLine().contains("second point")), "after the first point the help line says what comes next");
    click(table, table->toScreen(between(topLeft, topRight, 1.0 / 3.0)));
    REQUIRE(lastIs<LineBetween>(w.doc(), plain));

    choose(w, Tool::Cut);
    click(table, table->toScreen(between(between(bottomLeft, topLeft, 0.5), between(bottomRight, topRight, 0.5), 0.1)));
    EXPECT((lastIs<Cut>(w.doc(), plain) && std::abs(w.width(plain) - 20.0) < 0.1), "a click left of the line cuts the left third away");
    w.window.shelf()->undo()->trigger();

    choose(w, Tool::Split);
    click(table, table->toScreen(between(between(bottomLeft, bottomRight, 1.0 / 3.0), between(topLeft, topRight, 1.0 / 3.0), 0.5)));
    EXPECT((w.doc().panels().size() == 3), "a click on the line splits the panel in two");
    EXPECT((std::abs(w.width(plain) + w.width(w.doc().panels().back()) - 30.0) < 0.1), "the two parts make the whole");
}

TEST_CASE("Sew joins two edges once; Mirror reflects a panel across a straight edge", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain"), driven = w.panel("driven");
    const Vec2 top = between(w.corner(plain, -1, 1), w.corner(plain, 1, 1), 0.5);
    const Vec2 otherTop = between(w.corner(driven, -1, 1), w.corner(driven, 1, 1), 0.5);
    choose(w, Tool::Sew);
    click(table, table->toScreen(top));
    EXPECT((w.window.helpLine().contains("sew it to")), "after the first edge the help line asks for the second");
    click(table, table->toScreen(otherTop));
    EXPECT((w.doc().seams().size() == 1), "the second click sews them");
    click(table, table->toScreen(top));
    click(table, table->toScreen(otherTop));
    EXPECT((w.doc().seams().size() == 1 && w.window.helpLine().contains("already sewn")), "the same pair is not sewn twice");

    choose(w, Tool::Mirror);
    click(table, table->toScreen(between(w.corner(plain, 1, -1), w.corner(plain, 1, 1), 0.5)));
    EXPECT((lastIs<Mirror>(w.doc(), plain) && std::abs(w.width(plain) - 60.0) < 0.1), "a click on the right edge mirrors the panel across it");
}

TEST_CASE("a length label is double-clicked as a person does it: the first click keeps the panel chosen, so the label is still there", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    w.table()->grab();  // painted once, so that the labels have places
    REQUIRE(!w.table()->lengthLabels().empty());
    const QPointF at = w.table()->lengthLabels().front().first.center();
    send(w.table(), QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, {});
    send(w.table(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
    EXPECT((w.window.selectedPanels() == std::vector<PanelId>{plain} && !w.table()->lengthLabels().empty()), "one click on a label leaves the panel chosen, and its labels shown");
    w.table()->grab();
    send(w.table(), QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton, {});
    send(w.table(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
    EXPECT((w.table()->lengthEditor() != nullptr), "the second click then opens a box on the table to type that length");
}

TEST_CASE("the Length tool on the shelf gives a clicked edge a length of its own; what it keeps is chosen in its layer", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    choose(w, Tool::Length);
    EXPECT((w.window.helpLine().contains("Click an edge")), "the tool says what to do");
    // The top edge of the 30 by 10 rectangle, which its width shares with the bottom.
    const Vec2 left = w.corner(plain, -1, 1), right = w.corner(plain, 1, 1);
    click(w.table(), w.table()->toScreen({(left.x + right.x) / 2, (left.y + right.y) / 2}));
    REQUIRE(lastIs<SetLength>(w.doc(), plain));
    const OpId set = w.doc().operations(plain).back();
    EXPECT((w.table()->lengthEditor() != nullptr), "a box opens on the table to type its length");
    EXPECT((w.window.shelf()->action(Tool::Select)->isChecked()), "the tool is done, and Select is back");
    typeOnTable(w.table(), "40");
    const EdgeRef top = std::get<SetLength>(w.doc().get(set)).edge;
    EXPECT_NEAR((w.window.session().editor().shapes()[plain].length(top)), (40.0), "the top has the typed length");
    EXPECT((w.width(plain) == 40.0), "and the panel is wider at the top than the rectangle was");

    QComboBox* keeps = w.window.attributes()->findChild<QComboBox*>("keeps");
    REQUIRE(keeps != nullptr);
    EXPECT((keeps->count() == 3 && keeps->currentIndex() == 0 && keeps->itemText(1).endsWith(" stays") &&
            keeps->itemData(1, Qt::ToolTipRole).toString().contains("corner")), "the layer offers what to keep, in words of corners, and starts with both ends moving");
    const OpRef<Rect> rect(first<Rect>(w.doc(), plain));
    const auto topRightX = [&] { return w.window.session().editor().shapes()[plain].position(rect.topRight()).x; };
    EXPECT_NEAR((topRightX()), (35.0), "with both ends moving, the top-right corner went out 5 cm");
    keeps->setCurrentIndex(1);
    emit keeps->activated(1);
    EXPECT((std::get<SetLength>(w.doc().get(set)).keeping == SetLength::Keeping::Start), "choosing one keeps that end");
    EXPECT_NEAR((topRightX()), (30.0), "and the corner of that end stays where the rectangle had it");
    EXPECT_NEAR((w.window.session().editor().shapes()[plain].length(top)), (40.0), "while the top keeps its length");
    w.window.shelf()->undo()->trigger();
    EXPECT((std::get<SetLength>(w.doc().get(set)).keeping == SetLength::Keeping::Middle), "undo takes the choice back");
}

TEST_CASE("a length is typed on the table with a live preview; Enter keeps it and Escape takes all of it back", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const OpRef<Rect> rect(first<Rect>(w.doc(), plain));
    TableView* table = w.table();
    const auto shape = [&]() -> const Pattern& { return w.window.session().editor().shapes()[plain]; };
    const auto layers = [&] { return w.doc().operations(plain).size(); };
    const auto bar = [&] { return table->lengthCard(); };
    const auto scope = [&] { return bar()->findChild<QComboBox*>("lengthScope"); };
    const auto doubleClickBottom = [&] {
        const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
        send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    };
    const auto typeLive = [&](const QString& text) {
        REQUIRE(table->lengthEditor() != nullptr);
        table->lengthEditor()->setText(text);  // each edit is reported at once
    };
    const auto chooseScope = [&](int index) {
        REQUIRE(scope() != nullptr);
        scope()->setCurrentIndex(index);
        emit scope()->activated(index);
    };
    const auto keeping = [&] { return std::get<SetLength>(w.doc().get(w.doc().operations(plain).back())).keeping; };
    const auto clickAnchor = [&](SetLength::Keeping which) {
        const QPointF at = table->anchorPosition(which);
        send(table, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, {});
        send(table, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
    };

    // The shape's own setting is typed at first, and it shows at once.
    doubleClickBottom();
    REQUIRE(table->lengthEditor() != nullptr);
    EXPECT((scope()->isVisible() && scope()->currentIndex() == 1 && scope()->itemText(1).contains("width")), "a rectangle's edge offers 'Edge only' or the shape's width, and the width is chosen");
    EXPECT((bar()->findChild<QLabel*>("lengthCaption")->text() == "Width of bottom edge"), "the caption names what is typed and the edge");
    EXPECT((table->anchorWord(SetLength::Keeping::Middle).isEmpty()), "the shape's setting has no end to keep, so there are no anchors");
    typeLive("26");
    EXPECT_NEAR((shape().length(rect.bottom())), (26.0), "what is typed is shown before Enter");
    EXPECT((layers() == 1), "and it is the rectangle's own width, no layer is added");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(table->lengthEditor(), &escape);
    EXPECT((table->lengthEditor() == nullptr && std::abs(shape().length(rect.bottom()) - 30.0) < 1e-9), "Escape closes the bar and the width is as it was");
    EXPECT((!w.window.shelf()->undo()->isEnabled() && !w.window.shelf()->redo()->isEnabled()), "with nothing left to undo, and nothing to redo: what Escape took back is forgotten");

    // This edge only: a length of its own, with anchors on the edge.
    doubleClickBottom();
    chooseScope(0);
    EXPECT((layers() == 2 && keeping() == SetLength::Keeping::Middle), "'Edge only' gives it a length of its own, both ends moving");
    EXPECT((table->anchorWord(SetLength::Keeping::Start) == "Left" && table->anchorWord(SetLength::Keeping::End) == "Right" && table->anchorWord(SetLength::Keeping::Middle) == "Midpoint"),
           "three anchors on the edge, named as the screen shows them: the bottom runs left to right");
    typeLive("20");
    EXPECT_NEAR((shape().length(rect.bottom())), (20.0), "the typed length is shown at once");
    EXPECT_NEAR((shape().length(rect.top())), (30.0), "and the top is the rectangle's width, so the panel is a trapezoid");
    EXPECT_NEAR((shape().position(rect.bottomLeft()).x), (5.0), "both ends moved in by the same amount");
    clickAnchor(SetLength::Keeping::Start);
    EXPECT((keeping() == SetLength::Keeping::Start && table->lengthEditor() != nullptr), "a click on the left anchor keeps that end, and the bar stays open");
    EXPECT_NEAR((shape().position(rect.bottomLeft()).x), (0.0), "the left corner is where it was");
    EXPECT_NEAR((shape().position(rect.bottomRight()).x), (20.0), "the other end took all of it");
    clickAnchor(SetLength::Keeping::End);
    EXPECT((keeping() == SetLength::Keeping::End), "another anchor keeps the other end instead");
    clickAnchor(SetLength::Keeping::Middle);
    EXPECT((keeping() == SetLength::Keeping::Middle), "and the midpoint lets both ends move");

    // Escape takes all of it back: the preview, the anchors and the length that was made.
    QApplication::sendEvent(table->lengthEditor(), &escape);
    EXPECT((table->lengthEditor() == nullptr && layers() == 1 && std::abs(shape().length(rect.bottom()) - 30.0) < 1e-9), "Escape takes it all back, the layer that was made too");
    EXPECT((!w.window.shelf()->undo()->isEnabled()), "so nothing is left to undo");

    // Something wrong is said under the field, and Enter waits.
    doubleClickBottom();
    chooseScope(0);
    typeLive("-5");
    EXPECT((!bar()->problem().isEmpty() && std::abs(shape().length(rect.bottom()) - 30.0) < 1e-9), "a length that cannot be is said on the bar, and the shape is as it was");
    emit table->lengthEditor()->returnPressed();
    EXPECT((table->lengthEditor() != nullptr), "Enter does not close it while that is so");
    typeLive("no length");
    EXPECT((!bar()->problem().isEmpty()), "a text that is no length is said too");
    typeLive("30 cm");
    EXPECT((bar()->problem().isEmpty()), "typing back what is applied now clears the words, and Enter works again");
    typeLive("-5");
    EXPECT((!bar()->problem().isEmpty()), "and a length that cannot be is said again");
    typeLive("22");
    EXPECT((bar()->problem().isEmpty()), "and the words go when it works");

    // Enter keeps it, as one undo step with the length that was made.
    emit table->lengthEditor()->returnPressed();
    EXPECT((table->lengthEditor() == nullptr && layers() == 2 && std::abs(shape().length(rect.bottom()) - 22.0) < 1e-9), "Enter keeps what was typed");
    w.window.shelf()->undo()->trigger();
    EXPECT((layers() == 1 && std::abs(shape().length(rect.bottom()) - 30.0) < 1e-9), "one undo takes the length and its layer back");
    w.window.shelf()->redo()->trigger();
    EXPECT((layers() == 2 && std::abs(shape().length(rect.bottom()) - 22.0) < 1e-9), "and redo brings them");

    // Back to the shape's setting, from the bar.
    doubleClickBottom();
    REQUIRE(table->lengthEditor() != nullptr);
    EXPECT((scope()->currentIndex() == 0), "an edge with a length of its own opens on it");
    chooseScope(1);
    EXPECT((layers() == 1 && scope()->currentIndex() == 1 && table->lengthEditor()->text() == "30 cm"), "the shape's setting again: the edge's own length is gone, and the width is in the field");
    QApplication::sendEvent(table->lengthEditor(), &escape);
    EXPECT((layers() == 2 && std::abs(shape().length(rect.bottom()) - 22.0) < 1e-9), "and Escape takes even that back");

    // A click on any part of the bar stays on the bar: the caption, the gaps, the words under the field.
    // Qt hands a click that a part has no use for to what is under it, which is the table: it would
    // leave the bar and choose nothing, and the panel would be deselected.
    doubleClickBottom();
    typeLive("-1");
    REQUIRE(!bar()->problem().isEmpty());
    const std::vector<QWidget*> parts = {bar(), bar()->findChild<QLabel*>("lengthCaption"), bar()->findChild<QLabel*>("lengthProblem"), bar()->findChild<QFrame*>("lengthNumber")};
    for (QWidget* part : parts) {
        REQUIRE(part != nullptr);
        const QPointF inside = part->rect().center();
        const QPoint corner(2, 2);  // a gap at the edge of the bar
        for (const QPointF at : {inside, QPointF(corner)}) {
            QMouseEvent press(QEvent::MouseButtonPress, at, part->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, {});
            QApplication::sendEvent(part, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, at, part->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, {});
            QApplication::sendEvent(part, &release);
        }
    }
    EXPECT((table->lengthEditor() != nullptr && w.window.selectedPanels() == std::vector<PanelId>{plain}), "a click on the bar keeps the bar open and the panel chosen");
    typeLive("25");
    emit table->lengthEditor()->returnPressed();
    doubleClickBottom();
    typeLive("26");
    EXPECT_NEAR((shape().length(rect.bottom())), (26.0), "typed");

    // A click elsewhere on the table leaves the bar like Escape.
    click(table, table->toScreen({-1000, -1000}));
    EXPECT((table->lengthEditor() == nullptr && std::abs(shape().length(rect.bottom()) - 25.0) < 1e-9), "a click elsewhere leaves it, as it was");
}

TEST_CASE("something else done while a length is typed closes the bar, and each stays an undo step of its own", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const OpRef<Rect> rect(first<Rect>(w.doc(), plain));
    TableView* table = w.table();
    const auto bottom = [&] { return w.window.session().editor().shapes()[plain].length(rect.bottom()); };
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(table->lengthEditor() != nullptr);
    table->lengthEditor()->setText("22");
    const std::size_t panels = w.doc().panels().size();

    w.window.shelf()->mirroredInstance()->trigger();  // a button that leaves the focus on the bar
    EXPECT((table->lengthEditor() == nullptr), "the bar closes");
    EXPECT((bottom() == 22.0 && w.doc().panels().size() == panels + 1), "what was typed stays, and the instance is made");
    w.window.shelf()->undo()->trigger();
    EXPECT((bottom() == 22.0 && w.doc().panels().size() == panels), "one undo for the instance");
    w.window.shelf()->undo()->trigger();
    EXPECT((bottom() == 30.0 && !w.window.shelf()->undo()->isEnabled()), "and one for the length");
}

TEST_CASE("an anchor chosen on an edge that has its own length is part of the edit, and Escape takes it back", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();
    const auto layers = [&] { return w.doc().operations(plain).size(); };
    const auto keeping = [&] { return std::get<SetLength>(w.doc().get(w.doc().operations(plain).back())).keeping; };
    const auto doubleClickBottom = [&] {
        const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
        send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    };
    // An own length is made and kept first.
    doubleClickBottom();
    QComboBox* scope = table->lengthCard()->findChild<QComboBox*>("lengthScope");
    scope->setCurrentIndex(0);
    emit scope->activated(0);
    table->lengthEditor()->setText("22");
    emit table->lengthEditor()->returnPressed();
    REQUIRE((layers() == 2 && table->lengthEditor() == nullptr));

    // Opened again, the double-click changes nothing: the anchor is the first change of this edit.
    doubleClickBottom();
    REQUIRE(table->lengthEditor() != nullptr);
    const QPointF at = table->anchorPosition(SetLength::Keeping::Start);
    send(table, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, {});
    send(table, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
    EXPECT((keeping() == SetLength::Keeping::Start), "the anchor is taken");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(table->lengthEditor(), &escape);
    EXPECT((keeping() == SetLength::Keeping::Middle && layers() == 2), "Escape takes the anchor back");
    EXPECT((w.window.shelf()->undo()->isEnabled()), "and not the edit before it, which is still there to undo");
    w.window.shelf()->undo()->trigger();
    EXPECT((layers() == 1), "one undo takes back that earlier edit alone, with nothing of the anchor mixed in");
}

TEST_CASE("giving up an edge's own length is refused on the table where the shape would give it none, and the bar stays on the own length", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();
    const OpRef<Rect> rect(first<Rect>(w.doc(), plain));
    const auto layers = [&] { return w.doc().operations(plain).size(); };
    // The top-right corner dragged in: the shape no longer gives the top its length.
    const QPointF corner = table->toScreen(w.corner(plain, 1, 1));
    drag(table, corner, by(corner, table, -5, 0));
    REQUIRE(layers() == 2);
    table->grab();
    const auto label = std::find_if(table->lengthLabels().begin(), table->lengthLabels().end(), [&](const auto& l) { return l.second == rect.top(); });
    REQUIRE(label != table->lengthLabels().end());
    send(table, QEvent::MouseButtonDblClick, label->first.center(), Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(table->lengthEditor() != nullptr);
    EXPECT((layers() == 3), "the double-click gives the top a length of its own");
    QComboBox* scope = table->lengthCard()->findChild<QComboBox*>("lengthScope");
    REQUIRE((scope != nullptr && scope->isVisible() && scope->currentIndex() == 0));
    scope->setCurrentIndex(1);
    emit scope->activated(1);
    EXPECT((layers() == 3), "the shape's width is refused: it would not give the top a length, and nothing is taken away");
    EXPECT((table->lengthEditor() != nullptr && scope->currentIndex() == 0), "the bar stays, on the edge's own length, and shows it");
    EXPECT((w.window.helpLine().contains("Not changed")), "and the help line says why");
    table->lengthEditor()->setText("12");
    EXPECT_NEAR((w.window.session().editor().shapes()[plain].length(rect.top())), (12.0), "typing still works on it");
}

TEST_CASE("the bar closes when the length it is for is undone", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    QComboBox* scope = table->lengthCard()->findChild<QComboBox*>("lengthScope");
    scope->setCurrentIndex(0);
    emit scope->activated(0);
    REQUIRE((w.doc().operations(plain).size() == 2 && table->lengthEditor() != nullptr));
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().operations(plain).size() == 1 && table->lengthEditor() == nullptr), "undoing the length it is for closes the bar, which has nothing left to type into");
}

TEST_CASE("a longer length moves the end that is not kept, not the whole drawing", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();
    const OpRef<Rect> rect(first<Rect>(w.doc(), plain));
    // Where a corner is on the table: the panel's place and the corner in the panel.
    const auto onTable = [&](const CornerRef& corner) {
        const Vec2 at = w.window.session().editor().shapes()[plain].position(corner);
        return w.doc().get(plain).at + at;
    };
    const Vec2 leftWas = onTable(rect.bottomLeft()), rightWas = onTable(rect.bottomRight());
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    QComboBox* scope = table->lengthCard()->findChild<QComboBox*>("lengthScope");
    scope->setCurrentIndex(0);
    emit scope->activated(0);
    const QPointF anchor = table->anchorPosition(SetLength::Keeping::Start);  // the left end
    send(table, QEvent::MouseButtonPress, anchor, Qt::LeftButton, Qt::LeftButton, {});
    send(table, QEvent::MouseButtonRelease, anchor, Qt::LeftButton, Qt::NoButton, {});

    table->lengthEditor()->setText("40");  // longer than the 30 it was
    EXPECT((std::abs(onTable(rect.bottomLeft()).x - leftWas.x) < 1e-9 && std::abs(onTable(rect.bottomLeft()).y - leftWas.y) < 1e-9), "the kept left end has not moved on the table");
    EXPECT_NEAR((onTable(rect.bottomRight()).x), (rightWas.x + 10.0), "only the other end moved, out by the 10 cm more");

    // The same with the right end kept, and with the middle.
    const QPointF right = table->anchorPosition(SetLength::Keeping::End);
    send(table, QEvent::MouseButtonPress, right, Qt::LeftButton, Qt::LeftButton, {});
    send(table, QEvent::MouseButtonRelease, right, Qt::LeftButton, Qt::NoButton, {});
    EXPECT((std::abs(onTable(rect.bottomRight()).x - rightWas.x) < 1e-9), "keeping the right end, that one stays on the table");
    EXPECT_NEAR((onTable(rect.bottomLeft()).x), (leftWas.x - 10.0), "and the left end goes out");
    const QPointF middle = table->anchorPosition(SetLength::Keeping::Middle);
    send(table, QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::LeftButton, {});
    send(table, QEvent::MouseButtonRelease, middle, Qt::LeftButton, Qt::NoButton, {});
    EXPECT_NEAR((onTable(rect.bottomLeft()).x), (leftWas.x - 5.0), "with the midpoint kept, both go out by half");
    EXPECT_NEAR((onTable(rect.bottomRight()).x), (rightWas.x + 5.0), "as much");
}

TEST_CASE("a length typed for a sewn panel grows it away from its seam; a new document closes the bar", "[qt]")
{
    Table w;
    {
        Document doc;
        const PanelId a = doc.add(Panel{.name = "A"});
        const OpRef<Rect> ra = doc.add(a, Rect{.width = 30_cm, .height = 10_cm});
        const PanelId b = doc.add(Panel{.name = "B", .at = {40, 0}});
        const OpRef<Rect> rb = doc.add(b, Rect{.width = 30_cm, .height = 10_cm});
        doc.add(Seam{.a = {a, ra.right()}, .b = {b, rb.left()}});
        w.window.open(std::move(doc), "sewn");
        w.window.tick();
    }
    const PanelId a = w.panel("A");
    w.window.select(a);
    TableView* table = w.table();
    const OpRef<Rect> ra(first<Rect>(w.doc(), a));
    // The sewn edge, as the table has it: the middle of the right edge.
    const auto sewnMiddle = [&] {
        const Pattern& shape = w.window.session().editor().shapes()[a];
        const Vec2 low = shape.position(ra.bottomRight()), high = shape.position(ra.topRight());
        return Vec2{w.doc().get(a).at.x + (low.x + high.x) / 2.0, w.doc().get(a).at.y + (low.y + high.y) / 2.0};
    };
    const Vec2 before = sewnMiddle();
    const Vec2 left = w.corner(a, -1, 3), right = w.corner(a, 1, 3);  // the top edge
    send(table, QEvent::MouseButtonDblClick, table->toScreen({(left.x + right.x) / 2, (left.y + right.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(table->lengthEditor() != nullptr);
    QComboBox* scope = table->lengthCard()->findChild<QComboBox*>("lengthScope");
    scope->setCurrentIndex(0);
    emit scope->activated(0);
    table->lengthEditor()->setText("40");  // both ends of the top go out, the midpoint kept
    EXPECT_NEAR((sewnMiddle().x), (before.x), "the sewn edge is where it was on the table: the panel grows away from its seam, as it does for a formula");

    // A new document closes the bar, though the new document has a layer with the number of the one the bar
    // was for: the bar would otherwise type into that other layer.
    REQUIRE(table->lengthEditor() != nullptr);
    const OpId typedInto = table->lengthBeingTyped()->operation;
    Document other;
    const PanelId p = other.add(Panel{.name = "other"});
    const OpRef<Rect> otherRect = other.add(p, Rect{.width = 10_cm, .height = 10_cm});
    while (!other.contains(typedInto)) other.add(p, AdjustPoint{.corner = otherRect.topRight(), .dx = 0_cm, .dy = 0_cm});
    w.window.open(std::move(other), "other");
    EXPECT((table->lengthEditor() == nullptr), "a new document closes the bar");
}

TEST_CASE("a layer's edge is chosen again by clicking it on the table", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    choose(w, Tool::Mirror);
    click(table, table->toScreen(between(w.corner(plain, 1, -1), w.corner(plain, 1, 1), 0.5)));
    REQUIRE(lastIs<Mirror>(w.doc(), plain));
    const OpId mirror = w.doc().operations(plain).back();
    const Reference across = references(w.doc().get(mirror))[0].value;
    const double leftBefore = w.corner(plain, -1, 0).x;  // the rectangle's left edge, which the mirror kept

    emit w.window.attributes()->referenceClicked(mirror, references(w.doc().get(mirror))[0].id);
    EXPECT((table->choosing() && w.window.helpLine().contains("Click an edge")), "clicking the edge's name asks for one on the table");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(table, &escape);
    EXPECT((!table->choosing() && references(w.doc().get(mirror))[0].value == across), "Esc drops it and changes nothing");

    emit w.window.attributes()->referenceClicked(mirror, references(w.doc().get(mirror))[0].id);
    // The rectangle as it was before the mirror: its left edge is where the panel's left still is.
    click(table, table->toScreen({leftBefore, w.corner(plain, 1, -1).y + 5.0}));
    EXPECT((!table->choosing() && !(references(w.doc().get(mirror))[0].value == across)), "a click on another edge makes the layer use it");
    EXPECT((std::abs(w.width(plain) - 60.0) < 0.1), "the panel is mirrored across that edge instead");
    w.window.shelf()->undo()->trigger();
    EXPECT((references(w.doc().get(mirror))[0].value == across), "and undo takes the choice back");
}

TEST_CASE("the cloth mesh can be drawn over the panels", "[qt]")
{
    Table w;
    EXPECT((!w.table()->meshShown()), "it is not shown at first");
    w.window.shelf()->mesh()->setChecked(true);
    for (int i = 0; i < 3 && !w.table()->meshShown(); ++i) w.window.tick();
    EXPECT((w.table()->meshShown()), "the switch on the shelf shows it");
    w.window.shelf()->mesh()->setChecked(false);
    w.window.tick();
    EXPECT((!w.table()->meshShown()), "and hides it again");
}

// ---- The 3D view ----

TEST_CASE("the 3D view draws the body and the cloth with Vulkan, or says why it cannot", "[qt]")
{
    Window w;
    if (w.window.space()->drawing() == SpaceView::Drawing::Nothing) {
        EXPECT((!w.window.space()->problem().isEmpty()), "without Vulkan the view says why");
        SKIP("No Vulkan on this machine: " << w.window.space()->problem().toStdString());
    }
    for (int i = 0; i < 3; ++i) w.window.tick();
    const render::Picture* picture = w.window.space()->picture();
    REQUIRE(picture != nullptr);
    REQUIRE(picture->width > 0);
    const render::Colour middle = picture->at(picture->width / 2, picture->height / 2), corner = picture->at(1, 1);
    EXPECT((std::abs(middle.red - corner.red) + std::abs(middle.green - corner.green) + std::abs(middle.blue - corner.blue) > 0.1f),
           "something is drawn in the middle: the body, framed when the view first shows");

    // Selecting a panel colours it amber in the picture somewhere.
    const auto amberPixels = [&] {
        int count = 0;
        const render::Picture* now = w.window.space()->picture();
        for (int y = 0; y < now->height; y += 2)
            for (int x = 0; x < now->width; x += 2) {
                const render::Colour c = now->at(x, y);
                count += c.red > 0.5f && c.red > c.blue * 2.0f && c.green > c.blue * 1.3f;
            }
        return count;
    };
    const int before = amberPixels();
    w.window.select(w.panel("front"));
    w.window.tick();
    EXPECT((amberPixels() > before + 20), "the selected panel is drawn amber");
}

TEST_CASE("Alt with the mouse turns, pans and dollies the camera; F frames everything", "[qt]")
{
    Window w;
    SpaceView* space = w.window.space();
    if (space->drawing() == SpaceView::Drawing::Nothing) SKIP("No Vulkan on this machine: " << space->problem().toStdString());
    w.window.tick();
    const render::OrbitCamera framed = space->camera();

    space->press({100, 100}, Qt::LeftButton, Qt::AltModifier);
    space->moveTo({160, 100}, Qt::LeftButton, Qt::AltModifier);
    space->release();
    EXPECT((std::abs(space->camera().yaw - (framed.yaw - 18.0)) < 1e-9), "Alt and the left button orbit");

    space->press({100, 100}, Qt::LeftButton, {});
    space->moveTo({160, 100}, Qt::LeftButton, {});
    space->release();
    EXPECT((std::abs(space->camera().yaw - (framed.yaw - 18.0)) < 1e-9), "the left button alone leaves the camera: it is for the cloth");

    const Vec3 target = space->camera().target;
    space->press({100, 100}, Qt::MiddleButton, Qt::AltModifier);
    space->moveTo({100, 140}, Qt::MiddleButton, Qt::AltModifier);
    space->release();
    EXPECT((space->camera().target.y > target.y), "Alt and the middle button pan: dragging down moves the view's middle up");

    const double distance = space->camera().distance;
    space->wheel(2.0);
    EXPECT((space->camera().distance < distance), "the wheel comes nearer");
    space->press({100, 100}, Qt::RightButton, Qt::AltModifier);
    space->moveTo({60, 100}, Qt::RightButton, Qt::AltModifier);
    space->release();
    EXPECT((space->camera().distance > distance * 0.88 * 0.88), "Alt and the right button dragged left go farther");

    space->key(Qt::Key_F);
    EXPECT((std::abs(space->camera().distance - framed.distance) < 1e-6 && std::abs(space->camera().target.y - framed.target.y) < 1e-6),
           "F frames everything again, from where the camera looks");
}

namespace {

// Where a point of the picture is in the view's own coordinates, as press() takes them.
QPointF onView(const SpaceView& space, render::PicturePoint at) {
    const double scale = space.inPicture({1.0, 1.0}).x;
    return {at.x / scale, at.y / scale};
}

void click(SpaceView& space, QPointF at) {
    space.press(at, Qt::LeftButton, {});
    space.release();
}

} // namespace

TEST_CASE("a click in the 3D view selects the panel under it; its gizmo carries it along one axis", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    w.window.select(std::nullopt);
    w.window.tick();
    EXPECT((!space.gizmo().has_value()), "with nothing selected there is no gizmo");

    // The panel nearest the camera, which looks at the body from the front.
    PanelId front = w.doc().panels().front();
    for (PanelId p : w.doc().panels())
        if (w.doc().get(p).arrangement.at.z > w.doc().get(front).arrangement.at.z) front = p;
    const Arrangement before = w.doc().get(front).arrangement;
    const Vec3 centre = before.at;
    const std::optional<render::PicturePoint> middle = render::pictureOf(space.camera(), space.pictureSize(), centre);
    REQUIRE(middle.has_value());

    click(space, onView(space, *middle));
    w.window.tick();
    EXPECT((w.window.selected() == front), "clicking the panel in 3D selects it");
    EXPECT((!space.gizmo().has_value()), "with Select, the selected panel shows no gizmo");
    w.window.shelf()->action(Tool::Move)->trigger();
    const std::optional<render::Gizmo> gizmo = space.gizmo();
    REQUIRE(gizmo.has_value());

    // Take the x arrow a little way out from the middle and pull it outwards.
    const render::PicturePoint tip = *render::pictureOf(space.camera(), space.pictureSize(), centre + Vec3{1.0, 0.0, 0.0});
    const double dx = tip.x - middle->x, dy = tip.y - middle->y, length = std::hypot(dx, dy);
    REQUIRE(length > 0.0);
    const auto out = [&](double pixels) { return render::PicturePoint{middle->x + dx / length * pixels * gizmo->pixel, middle->y + dy / length * pixels * gizmo->pixel}; };
    REQUIRE(render::handleAt(*gizmo, space.camera(), space.pictureSize(), out(40.0)) == render::GizmoHandle::AlongX);
    const double yaw = space.camera().yaw;
    space.press(onView(space, out(40.0)), Qt::LeftButton, {});
    for (int step = 1; step <= 5; ++step) {
        space.moveTo(onView(space, out(40.0 + 20.0 * step)), Qt::LeftButton, {});
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const Arrangement after = w.doc().get(front).arrangement;
    EXPECT((after.at.x > before.at.x + 1.0), "pulling the x arrow moves the panel along x");
    EXPECT((after.at.y == before.at.y && after.at.z == before.at.z), "and not along y or z");
    EXPECT((w.window.selected() == front), "the panel stays selected");
    EXPECT((space.camera().yaw == yaw), "the camera stays where it was");

    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement.at.x == before.at.x), "the whole drag is one step to undo");

    click(space, {2.0, 2.0});
    w.window.tick();
    EXPECT((!w.window.selected().has_value()), "a click on nothing clears the selection");
}

namespace {

// The panel nearest the camera, which looks at the body from the front, and
// where the middle of it is drawn.
std::pair<PanelId, render::PicturePoint> frontPanel(const Window& w) {
    const SpaceView& space = *w.window.space();
    PanelId front = w.doc().panels().front();
    for (PanelId p : w.doc().panels())
        if (w.doc().get(p).arrangement.at.z > w.doc().get(front).arrangement.at.z) front = p;
    const Vec3 at = w.doc().get(front).arrangement.at;
    const std::optional<render::PicturePoint> middle = render::pictureOf(space.camera(), space.pictureSize(), at);
    REQUIRE(middle.has_value());
    return {front, *middle};
}

void dragBy(Window& w, QPointF from, QPointF by) {
    SpaceView& space = *w.window.space();
    space.press(from, Qt::LeftButton, {});
    for (int step = 1; step <= 5; ++step) {
        space.moveTo(from + by * (step / 5.0), Qt::LeftButton, {});
        w.window.tick();
    }
}

} // namespace

TEST_CASE("in 3D the rotate gizmo turns the selected panel in space; the scale gizmo scales its shape", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    const auto [front, middle] = frontPanel(w);
    w.window.select(front);
    w.window.shelf()->action(Tool::Rotate)->trigger();
    w.window.tick();
    REQUIRE((space.gizmo() && space.gizmo()->mode == session::GizmoMode::Rotate));
    const double pixel = space.gizmo()->pixel;
    const Arrangement before = w.doc().get(front).arrangement;

    // The outer ring, from its right a quarter of the way round clockwise, in steps of 15° with Ctrl.
    const auto onRing = [&](double degrees) {
        const double radians = degrees * std::numbers::pi / 180.0;
        return render::PicturePoint{middle.x + render::gizmoViewRingPixels * pixel * std::cos(radians), middle.y - render::gizmoViewRingPixels * pixel * std::sin(radians)};
    };
    REQUIRE(render::handleAt(*space.gizmo(), space.camera(), space.pictureSize(), onRing(0.0)) == render::GizmoHandle::AboutView);
    space.press(onView(space, onRing(0.0)), Qt::LeftButton, {});
    for (int step = 1; step <= 6; ++step) {
        space.moveTo(onView(space, onRing(-15.0 * step)), Qt::LeftButton, Qt::ControlModifier);
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const Arrangement after = w.doc().get(front).arrangement;
    // The line of sight through the panel's anchor, from it to the eye.
    const Vec3 sight = normalized(space.camera().eye() - before.at);
    const Vec3 expected = sim::inSpace(sim::turnedAbout(before, sight, -90.0), {1.0, 0.0, 0.0});
    EXPECT((length(sim::inSpace(after, {1.0, 0.0, 0.0}) - expected) < 1e-6), "the panel turns a quarter about the line of sight");
    EXPECT((after.at.x == before.at.x && after.at.y == before.at.y && after.at.z == before.at.z), "about its anchor, which stays where it is");
    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement == before), "the whole drag is one step to undo");

    // The ball between the rings: dragged to the right, the panel turns about the screen's up, freely.
    const render::PicturePoint ball{middle.x + 40.0 * pixel, middle.y - 40.0 * pixel};
    REQUIRE(render::handleAt(*space.gizmo(), space.camera(), space.pictureSize(), ball) == render::GizmoHandle::Free);
    space.press(onView(space, ball), Qt::LeftButton, {});
    for (int step = 1; step <= 5; ++step) {
        space.moveTo(onView(space, {ball.x + 12.0 * pixel * step, ball.y}), Qt::LeftButton, {});
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const Arrangement rolled = w.doc().get(front).arrangement;
    EXPECT((std::abs(rolled.turn - before.turn) > 20.0), "dragged 60 pixels to the right, the panel turns about the vertical, freely");
    EXPECT((rolled.at.x == before.at.x && rolled.at.y == before.at.y && rolled.at.z == before.at.z), "about its anchor");
    w.window.shelf()->undo()->trigger();
    w.window.tick();

    // The scale gizmo's x handle, along the panel's own x, pulled out half as far again.
    w.window.shelf()->action(Tool::Scale)->trigger();
    w.window.tick();
    REQUIRE((space.gizmo() && space.gizmo()->mode == session::GizmoMode::Scale));
    const render::Gizmo scale = *space.gizmo();
    const render::PicturePoint tip = *render::pictureOf(space.camera(), space.pictureSize(), scale.centre + scale.ownX);
    const double dx = tip.x - middle.x, dy = tip.y - middle.y, length = std::hypot(dx, dy);
    const auto out = [&](double pixels) { return render::PicturePoint{middle.x + dx / length * pixels * pixel, middle.y + dy / length * pixels * pixel}; };
    REQUIRE(render::handleAt(scale, space.camera(), space.pictureSize(), out(40.0)) == render::GizmoHandle::AlongX);
    const std::size_t layers = w.doc().operations(front).size();
    space.press(onView(space, out(40.0)), Qt::LeftButton, {});
    for (int step = 1; step <= 4; ++step) {
        space.moveTo(onView(space, out(40.0 + 5.0 * step)), Qt::LeftButton, {});
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const std::vector<OpId> operations = w.doc().operations(front);
    REQUIRE(operations.size() == layers + 1);
    const Scale* added = nullptr;
    for (OpId op : operations)
        if (const Scale* s = std::get_if<Scale>(&w.doc().get(op))) added = s;
    REQUIRE(added != nullptr);
    EXPECT((std::abs(*asNumber(added->x) - 1.5) < 0.03 && asNumber(added->y) == 1.0), "a Scale layer of about 150 % along the panel's own x");
    EXPECT((w.doc().get(front).arrangement.at.x == before.at.x), "scaling is shape: the panel's place stays");
}

TEST_CASE("in 3D the selected panel is carried by dragging it; draped cloth is pulled instead", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    const auto [front, middle] = frontPanel(w);
    // A spot on the panel a little off its middle, away from the gizmo's square.
    const QPointF onPanel = onView(space, {middle.x - 30.0 * space.inPicture({1, 1}).x, middle.y + 30.0 * space.inPicture({1, 1}).x});
    click(space, onPanel);
    w.window.tick();
    REQUIRE(w.window.selected() == front);

    const Arrangement before = w.doc().get(front).arrangement;
    dragBy(w, onPanel, {40.0, 0.0});
    space.release();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement == before), "with Select, dragging the panel only selects it");
    w.window.shelf()->action(Tool::Move)->trigger();
    dragBy(w, onPanel, {40.0, 0.0});
    space.release();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement.at.x != before.at.x), "with Move, dragging the selected panel carries it");
    EXPECT((w.window.selected() == front), "and it stays selected");

    // Drape it, then drag the playing cloth: the cloth is held, the document stays.
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 5; ++i) w.window.tick();
    REQUIRE(w.window.session().clothShown());
    const std::string pattern = toText(w.doc());
    // Off the middle again, so that the press is on the cloth and not on the gizmo.
    const render::PicturePoint nowMiddle = frontPanel(w).second;
    const double scale = space.inPicture({1, 1}).x;
    dragBy(w, onView(space, {nowMiddle.x - 30.0 * scale, nowMiddle.y + 30.0 * scale}), {0.0, -40.0});
    EXPECT((w.window.session().pulling()), "while the button is held the cloth is held");
    space.release();
    w.window.tick();
    EXPECT((!w.window.session().pulling()), "let go, it is not");
    EXPECT((toText(w.doc()) == pattern), "pulling the cloth changes nothing in the document");
}

TEST_CASE("with the cloth paused a drag in 3D selects or carries; it is pulled while it plays, or with the Pull tool", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    const PanelId front = frontPanel(w).first;
    w.window.select(front);
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 5; ++i) w.window.tick();
    w.window.shelf()->simulate()->setChecked(false);
    w.window.tick();
    const session::Session& session = w.window.session();
    REQUIRE((session.clothShown() && !session.simulating()));
    EXPECT((w.window.helpLine().contains("Paused") && w.window.helpLine().contains("(H)")), "pausing says what a drag does now");

    // Real mouse events on the surface the picture is drawn on. Without Vulkan
    // (as in CI) there is no surface: the view is then told what it would pass on.
    QWidget* surface = space.findChild<QWidget*>("spacePicture");
    REQUIRE((surface != nullptr) == (space.drawing() == SpaceView::Drawing::Picture));
    bool held = false;  // whether the cloth was held at some point of the last drag
    const auto drag = [&] {
        // Off the middle, so that the press is on the cloth and not on a gizmo.
        const render::PicturePoint middle = frontPanel(w).second;
        const double scale = space.inPicture({1, 1}).x;
        const QPointF from = onView(space, {middle.x - 30.0 * scale, middle.y + 30.0 * scale});
        held = false;
        if (surface) send(surface, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, {});
        else space.press(from, Qt::LeftButton, {});
        for (int step = 1; step <= 5; ++step) {
            const QPointF to = from + QPointF(8.0 * step, -4.0 * step);
            if (surface) send(surface, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, {});
            else space.moveTo(to, Qt::LeftButton, {});
            w.window.tick();
            held = held || session.pulling();
        }
        if (surface) send(surface, QEvent::MouseButtonRelease, from + QPointF(40.0, -20.0), Qt::LeftButton, Qt::NoButton, {});
        else space.release();
        w.window.tick();
    };
    const auto moved = [&](const sim::Placement& was) {
        double most = 0.0;
        for (std::size_t p = 0; p < was.size(); ++p)
            for (std::size_t i = 0; i < was[p].size(); ++i) most = std::max(most, length(session.cloth().positions[p][i] - was[p][i]));
        return most;
    };

    // Select (Q): the drag changes nothing.
    sim::Placement positions = session.cloth().positions;
    double time = session.cloth().time;
    std::uint64_t version = session.editor().version();
    drag();
    EXPECT((!held && !session.simulating() && session.cloth().time == time), "with Select the paused cloth is not held and stays paused");
    EXPECT((moved(positions) == 0.0 && session.editor().version() == version), "and nothing moved");
    EXPECT((w.window.selected() == front), "the panel stays selected");
    EXPECT((w.window.helpLine().contains("paused")), "and the help line says why");

    // Move (W): the selected panel is carried, drape and all.
    w.window.shelf()->action(Tool::Move)->trigger();
    w.window.tick();
    const Arrangement before = w.doc().get(front).arrangement;
    drag();
    const Arrangement after = w.doc().get(front).arrangement;
    EXPECT((!held && !session.simulating() && session.cloth().time == time), "with Move the cloth stays paused too");
    EXPECT((after.at.x > before.at.x + 1.0 && after.at.y > before.at.y), "and the panel moved in space as it was dragged, right and up");
    EXPECT((moved(positions) > 1.0), "its drape went with it");
    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement == before && moved(positions) < 1e-9), "one undo puts panel and drape back");

    // Pull (H): the paused cloth is pulled, and runs while it is held.
    w.window.shelf()->action(Tool::Pull)->trigger();
    w.window.tick();
    const std::string pattern = toText(w.doc());
    const sim::Placement unpulled = session.cloth().positions;
    time = session.cloth().time;
    drag();
    EXPECT((held && session.cloth().time > time && moved(positions) > 1.0), "with Pull the cloth is held and runs");
    EXPECT((!session.pulling() && !session.simulating() && toText(w.doc()) == pattern), "let go, it is paused again and the document is as it was");
    const sim::Placement pulled = session.cloth().positions;
    REQUIRE(w.window.shelf()->undo()->isEnabled());
    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((moved(unpulled) < 1e-5 && toText(w.doc()) == pattern), "one undo takes the whole pull back, and only the pull");
    w.window.shelf()->redo()->trigger();
    w.window.tick();
    EXPECT((moved(pulled) < 1e-5), "and redo pulls it again");

    // Playing: a drag pulls the cloth, whatever the tool.
    for (const Tool tool : {Tool::Select, Tool::Move}) {
        w.window.shelf()->action(tool)->trigger();
        w.window.shelf()->simulate()->setChecked(true);
        w.window.tick();
        EXPECT((w.window.helpLine().contains("pull")), "playing says that a drag pulls");
        drag();
        EXPECT((held && toText(w.doc()) == pattern), "while the cloth plays a drag pulls it");
        w.window.shelf()->simulate()->setChecked(false);
        w.window.tick();
    }

    // The back panel, behind the body, with the front one out of the way: reached only through a see-through body.
    const PanelId back = w.panel("back");
    Outliner& outliner = *w.window.outliner();
    REQUIRE(outliner.panelsShown().front() == front);
    const QTreeWidgetItem* row = outliner.topLevelItem(1)->child(0);
    const QPointF eye(outliner.columnViewportPosition(Outliner::eyeColumn) + outliner.columnWidth(Outliner::eyeColumn) / 2.0, outliner.visualItemRect(row).center().y());
    send(outliner.viewport(), QEvent::MouseButtonPress, eye, Qt::LeftButton, Qt::LeftButton, {});
    send(outliner.viewport(), QEvent::MouseButtonRelease, eye, Qt::LeftButton, Qt::NoButton, {});
    REQUIRE(!session.panelShown(front));
    w.window.select(back);
    w.window.shelf()->action(Tool::Move)->trigger();
    w.window.tick();
    const Arrangement backBefore = w.doc().get(back).arrangement;
    drag();
    EXPECT((w.doc().get(back).arrangement == backBefore), "a solid body is in the way of a drag");
    w.window.showBodySeeThrough(true);
    w.window.select(back);
    w.window.tick();
    time = session.cloth().time;
    drag();
    EXPECT((w.doc().get(back).arrangement.at.x > backBefore.at.x + 1.0 && !held && session.cloth().time == time), "through a see-through body Move carries the paused panel behind it");
    w.window.shelf()->undo()->trigger();
    w.window.shelf()->action(Tool::Pull)->trigger();
    w.window.tick();
    drag();
    EXPECT((held && session.cloth().time > time), "and Pull pulls it");

    w.window.shelf()->simulate()->setChecked(true);
    w.window.tick();
    w.window.shelf()->reset()->trigger();
    w.window.tick();
    EXPECT((!w.window.helpLine().contains("Playing")), "after Reset the help line no longer says that it plays");
}

TEST_CASE("the 3D gizmo follows cloth that has fallen; moving and turning work about where it stands", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    const session::Session& session = w.window.session();
    const PanelId front = frontPanel(w).first;
    w.window.select(front);
    w.window.shelf()->action(Tool::Move)->trigger();
    w.window.showBody(false);  // so that the cloth falls
    w.window.tick();
    REQUIRE(space.gizmo().has_value());
    const Vec3 first = space.gizmo()->centre;

    // How far the nearest particle of the panel is from a point.
    const auto fromCloth = [&](Vec3 at) {
        double least = 1e9;
        for (std::size_t p = 0; p < session.shownMesh().panels.size(); ++p)
            if (session.shownMesh().panels[p].panel == front)
                for (const Vec3& particle : session.shownPositions()[p]) least = std::min(least, length(particle - at));
        return least;
    };
    const auto onGizmo = [&](Vec3 point) { return *render::pictureOf(space.camera(), space.pictureSize(), point); };

    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 90; ++i) w.window.tick();  // past the seams being drawn shut, and falling

    // Playing: a handle that is held does not run away with the falling cloth.
    const Vec3 falling = space.gizmo()->centre;
    EXPECT((falling.y < first.y - 5.0 && fromCloth(falling) < 1e-9), "while the cloth falls the gizmo falls with it");
    const QPointF middle = onView(space, onGizmo(falling));
    space.press(middle, Qt::LeftButton, {});
    for (int i = 0; i < 5; ++i) w.window.tick();
    EXPECT((length(space.gizmo()->centre - falling) < 1e-9), "held, it stays where it was taken hold of");
    space.release();
    w.window.tick();
    w.window.shelf()->simulate()->setChecked(false);
    w.window.tick();

    const Vec3 fallen = space.gizmo()->centre;
    EXPECT((fromCloth(fallen) < 1e-9), "paused, the gizmo is on the panel's cloth");
    w.window.shelf()->action(Tool::Rotate)->trigger();
    w.window.tick();
    EXPECT((length(space.gizmo()->centre - fallen) < 1e-9), "and so after a change of tool");

    // The x arrow carries panel, drape and gizmo.
    w.window.shelf()->action(Tool::Move)->trigger();
    w.window.tick();
    const render::PicturePoint at = onGizmo(fallen), tip = onGizmo(fallen + Vec3{1.0, 0.0, 0.0});
    const double dx = tip.x - at.x, dy = tip.y - at.y, apart = std::hypot(dx, dy), pixel = space.gizmo()->pixel;
    const auto out = [&](double pixels) { return onView(space, {at.x + dx / apart * pixels * pixel, at.y + dy / apart * pixels * pixel}); };
    const Arrangement before = w.doc().get(front).arrangement;
    space.press(out(40.0), Qt::LeftButton, {});
    for (int step = 1; step <= 5; ++step) {
        space.moveTo(out(40.0 + 20.0 * step), Qt::LeftButton, {});
        w.window.tick();
    }
    const double moved = w.doc().get(front).arrangement.at.x - before.at.x;
    EXPECT((moved > 1.0 && length(space.gizmo()->centre - (fallen + Vec3{moved, 0.0, 0.0})) < 1e-6), "the gizmo goes with the hand");
    space.release();
    w.window.tick();
    const Vec3 carried = space.gizmo()->centre;
    EXPECT((length(carried - (fallen + Vec3{moved, 0.0, 0.0})) < 1e-6 && fromCloth(carried) < 1e-6), "and is still on the cloth when let go");

    // A turn is about the gizmo: the cloth under it stays where it is.
    w.window.shelf()->action(Tool::Rotate)->trigger();
    w.window.tick();
    const render::PicturePoint centre = onGizmo(carried);
    const auto onRing = [&](double degrees) {
        const double radians = degrees * std::numbers::pi / 180.0;
        return onView(space, {centre.x + render::gizmoViewRingPixels * pixel * std::cos(radians), centre.y - render::gizmoViewRingPixels * pixel * std::sin(radians)});
    };
    const sim::Placement drape = session.cloth().positions;
    space.press(onRing(0.0), Qt::LeftButton, {});
    for (int step = 1; step <= 6; ++step) {
        space.moveTo(onRing(-15.0 * step), Qt::LeftButton, Qt::ControlModifier);
        w.window.tick();
    }
    space.release();
    w.window.tick();
    double turned = 0.0;
    for (std::size_t p = 0; p < drape.size(); ++p)
        for (std::size_t i = 0; i < drape[p].size(); ++i) turned = std::max(turned, length(session.cloth().positions[p][i] - drape[p][i]));
    EXPECT((turned > 5.0), "the panel's drape turned");
    EXPECT((fromCloth(carried) < 1e-6 && length(space.gizmo()->centre - carried) < 1e-6), "about the gizmo, which stays on its spot of the cloth");
}

namespace {

// A skirt cut at an angle along a line: three layers, the cut using the line.
struct Hem {
    Document doc;
    PanelId skirt;
    OpId rect, line, cut;
    Hem() {
        skirt = doc.add(Panel{.name = "skirt"});
        const OpRef<Rect> r = doc.add(skirt, Rect{.width = 70_cm, .height = 60_cm});
        const OpRef<Line> l = doc.add(skirt, Line{.from = r.bottomLeft(), .angle = 25_deg});
        const OpRef<Cut> c = doc.add(skirt, Cut{.along = l.line(), .keep = Cut::Side::Left});
        rect = r.id, line = l.id, cut = c.id;
    }
};

std::size_t indexOf(const Document& doc, PanelId panel, OpId op) {
    const std::vector<OpId> made = doc.operations(panel);
    return static_cast<std::size_t>(std::find(made.begin(), made.end(), op) - made.begin());
}

} // namespace

TEST_CASE("layers are dragged to a new place, are removed, show their problem, and undo brings them back", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    AttributeEditor& editor = *w.window.attributes();
    EXPECT((editor.problemShown(hem.cut).isEmpty()), "a working cut shows no problem");

    editor.moveLayer(hem.line, 2);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 2), "dropped above the cut, the line is applied after it");
    EXPECT((!editor.problemShown(hem.cut).isEmpty()), "the cut, before its line now, says what is wrong under it");
    w.window.shelf()->undo()->trigger();
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1 && editor.problemShown(hem.cut).isEmpty()), "undo puts it back");

    editor.moveLayer(hem.line, 0);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1), "nothing goes below the base shape");
    editor.moveLayer(hem.rect, 2);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.rect) == 0), "and the base shape does not move");

    editor.removeLayer(hem.line);
    EXPECT((!w.doc().contains(hem.line)), "the bin removes the layer");
    EXPECT((!editor.problemShown(hem.cut).isEmpty()), "the cut without its line shows its problem");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(hem.line)), "undo brings the layer back");
}

TEST_CASE("a layer dragged by its name leaves a gap where it will land, and moves when let go", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    QApplication::processEvents();
    AttributeEditor& editor = *w.window.attributes();
    const auto row = [&](OpId layer) { return editor.findChild<QWidget*>(QString("layer%1").arg(layer.value)); };
    const auto centre = [&](QWidget* r) { return r->mapToGlobal(QPoint(r->width() / 2, r->height() / 2)); };
    REQUIRE((row(hem.line) && row(hem.cut)));
    auto* name = row(hem.line)->findChild<QPushButton*>("layerName");
    REQUIRE(name != nullptr);
    const auto at = [&](QPoint global) { return QPointF(name->mapFromGlobal(global)); };
    const QPoint start = centre(name), above = centre(row(hem.cut)) + QPoint(0, -row(hem.cut)->height());

    send(name, QEvent::MouseButtonPress, at(start), Qt::LeftButton, Qt::LeftButton, {});
    send(name, QEvent::MouseMove, at(start + QPoint(0, -20)), Qt::NoButton, Qt::LeftButton, {});
    send(name, QEvent::MouseMove, at(above), Qt::NoButton, Qt::LeftButton, {});
    EXPECT((row(hem.line)->graphicsEffect() != nullptr), "while dragging, its row stays behind as a dim gap");
    for (const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300); std::chrono::steady_clock::now() < until;) QApplication::processEvents();  // the rows slide
    EXPECT((row(hem.line)->y() < row(hem.cut)->y()), "and the gap has moved above the cut already");

    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1), "but nothing is changed yet");
    send(name, QEvent::MouseButtonRelease, at(above), Qt::LeftButton, Qt::NoButton, {});
    QApplication::processEvents();
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 2), "letting go applies it");
    EXPECT((editor.layersShown().front() == hem.line), "and the line is the top layer");
}

TEST_CASE("a step is looked back at; the table changes nothing meanwhile, and Escape shows all steps", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    AttributeEditor& editor = *w.window.attributes();

    editor.viewStep(hem.line);
    EXPECT((w.window.viewedStep() == hem.line), "Alt+click looks back to that step");
    const std::uint64_t version = w.window.session().editor().version();
    w.window.table()->gestured(session::MovePanel{hem.skirt, 10.0, 0.0, session::DragPhase::Begin});
    EXPECT((w.window.session().editor().version() == version), "while looking back, the table moves nothing");

    editor.viewStep(hem.line);
    EXPECT((!w.window.viewedStep().has_value()), "a second click shows all steps again");
    editor.viewStep(hem.line);
    QShortcut* leave = w.window.findChild<QShortcut*>();
    REQUIRE(leave != nullptr);
    EXPECT((leave->isEnabled()), "while looking back, Escape is taken");
    emit leave->activated();
    EXPECT((!w.window.viewedStep().has_value()), "and shows all steps");
    EXPECT((!leave->isEnabled()), "then Escape is left to the others");
    editor.viewStep(hem.cut);
    EXPECT((!w.window.viewedStep().has_value()), "the last step is the panel as it is");
}

TEST_CASE("an open layer offers its choices: the side a cut keeps, a mirror applied last, an instance mirrored and its source", "[qt]")
{
    Window w;
    Hem hem;
    const PanelId other = hem.doc.add(Panel{.name = "other"});
    hem.doc.add(other, Rect{.width = 20_cm, .height = 20_cm});
    const PanelId copy = hem.doc.add(Panel{.name = "copy"});
    const OpId follows = hem.doc.add(copy, Instance{.source = hem.skirt}).id;
    w.window.open(hem.doc, "hem");
    AttributeEditor& editor = *w.window.attributes();

    w.window.select(hem.skirt);
    editor.openLayer(hem.cut);
    auto* keep = editor.findChild<QComboBox*>("keep");
    REQUIRE(keep != nullptr);
    emit keep->activated(1);
    EXPECT((std::get<Cut>(w.doc().get(hem.cut)).keep == Cut::Side::Right), "the cut now keeps the right side");
    w.window.shelf()->undo()->trigger();
    EXPECT((std::get<Cut>(w.doc().get(hem.cut)).keep == Cut::Side::Left), "and undo keeps the left again");
    EXPECT((editor.findChild<QComboBox*>("keep")->currentIndex() == 0), "and the list shows it");

    w.window.select(copy);
    editor.openLayer(follows);
    auto* mirrored = editor.findChild<QCheckBox*>("mirrored");
    REQUIRE(mirrored != nullptr);
    mirrored->click();
    EXPECT((std::get<Instance>(w.doc().get(follows)).mirrored), "the instance follows its source mirrored");
    auto* source = editor.findChild<QComboBox*>("panelReference");
    REQUIRE(source != nullptr);
    const int otherRow = source->findText("other");
    REQUIRE(otherRow >= 0);
    EXPECT((source->findText("copy") < 0), "a panel is not offered as its own source");
    emit source->activated(otherRow);
    EXPECT((std::get<Instance>(w.doc().get(follows)).source == other), "the instance now follows the other panel");
}

TEST_CASE("a mirror can be applied in its place instead of last", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    const OpId mirror = first<Mirror>(w.doc(), front);
    w.window.attributes()->openLayer(mirror);
    auto* last = w.window.attributes()->findChild<QCheckBox*>("appliedLast");
    REQUIRE(last != nullptr);
    REQUIRE(last->isChecked());
    last->click();
    EXPECT((!std::get<Mirror>(w.doc().get(mirror)).appliedLast), "unticked, the mirror runs in its place");
}

TEST_CASE("a panel's seams are listed and reversed in the Attribute Editor, deleted in the Outliner, and undone", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    AttributeEditor& editor = *w.window.attributes();
    const std::vector<SeamId> seams = editor.seamsShown();
    REQUIRE(!seams.empty());
    for (SeamId id : seams) EXPECT((w.doc().get(id).a.panel == front || w.doc().get(id).b.panel == front), "only seams that touch the panel");

    const SeamId seam = seams.front();
    const bool was = w.doc().get(seam).reversed;
    auto* card = editor.findChild<QWidget*>(QString("seam%1").arg(seam.value));
    REQUIRE(card != nullptr);
    EXPECT((card->findChild<QPushButton*>() == nullptr), "a seam's card has no Remove: the Outliner deletes seams");
    auto* twist = card->findChild<QToolButton*>("reversed");
    REQUIRE(twist != nullptr);
    twist->click();
    EXPECT((w.doc().get(seam).reversed == !was), "the twist button reverses the seam");

    const std::vector<SeamId> listed = w.window.outliner()->seamsShown();
    EXPECT((std::find(listed.begin(), listed.end(), seam) != listed.end()), "the Outliner lists the seam");
    w.window.outliner()->removeSeam(seam);
    EXPECT((!w.doc().contains(seam)), "Delete in the Outliner takes the seam away");
    EXPECT((editor.seamsShown().size() == seams.size() - 1), "and the Attribute Editor no longer lists it");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(seam) && w.doc().get(seam).reversed == !was), "undo brings it back as it was");
}

TEST_CASE("a seam's row in the Outliner lights the seam on the table and in 3D; selecting a panel lets it go", "[qt]")
{
    Window w;
    Outliner& outliner = *w.window.outliner();
    const std::vector<SeamId> listed = outliner.seamsShown();
    REQUIRE(listed.size() >= 2);
    w.window.select(w.panel("front"));
    // A click with the mouse on the row of the nth seam.
    const auto clickSeam = [&](int n) {
        QTreeWidgetItem* seams = outliner.topLevelItem(2);
        seams->setExpanded(true);
        QTreeWidgetItem* row = seams->child(n);
        REQUIRE(row != nullptr);
        outliner.scrollToItem(row);
        const QPointF at = outliner.visualItemRect(row).center();
        send(outliner.viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, {});
        send(outliner.viewport(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
        w.window.tick();
    };
    const auto amberLines = [&] {
        int count = 0;
        for (const render::Lines& lines : w.window.space()->sceneFor(200, 200).lines)
            count += lines.lift > 0.0f && lines.colour.red == 1.0f && lines.colour.blue < 0.5f;
        return count;
    };

    EXPECT((amberLines() == 0 && w.window.table()->litSeamEdges().empty()), "no seam is lit before one is chosen");
    clickSeam(1);
    EXPECT((w.window.selectedSeam() == listed[1] && !w.window.selected()), "the seam is selected instead of the panel");
    EXPECT(w.window.table()->litSeamEdges().size() == 2, "its two edges are lit on the table");
    EXPECT(amberLines() == 2, "and it is amber in 3D: its edges, and the lines between them");
    EXPECT((outliner.currentItem() && outliner.currentItem()->parent() == outliner.topLevelItem(2) &&
            outliner.topLevelItem(2)->indexOfChild(outliner.currentItem()) == 1),
           "its row stays chosen");

    outliner.choose(w.panel("front"));
    w.window.tick();
    EXPECT((!w.window.selectedSeam() && w.window.selected() == w.panel("front")), "a panel's row lets the seam go");
    EXPECT((amberLines() == 0 && w.window.table()->litSeamEdges().empty()), "and nothing of it stays lit");

    clickSeam(0);
    EXPECT(w.window.selectedSeam() == listed[0], "another seam");
    const QPointF empty(w.window.table()->width() - 3.0, w.window.table()->height() - 3.0);
    click(w.window.table(), empty);
    EXPECT(!w.window.selectedSeam(), "a click on the empty table lets it go");
    clickSeam(0);
    w.window.select(std::nullopt);
    EXPECT(!w.window.selectedSeam(), "as a click on nothing in 3D does");

    clickSeam(0);
    QKeyEvent erase(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(&outliner, &erase);
    w.window.tick();
    EXPECT((!w.doc().contains(listed[0]) && !w.window.selectedSeam()), "Delete on its row takes it away, and the selection with it");
    w.window.shelf()->undo()->trigger();
    EXPECT(w.doc().contains(listed[0]), "undo brings it back");
}

TEST_CASE("a panel's eye hides it in 3D and its other button takes it out of the simulation, leaving the selection alone", "[qt]")
{
    Window w;
    Outliner& outliner = *w.window.outliner();
    const PanelId front = w.panel("front"), back = w.panel("back");
    w.window.select(back);
    // A click with the mouse on a button of a row: the body's (-1) or the nth panel's.
    const auto press = [&](int n, int column, QEvent::Type type = QEvent::MouseButtonPress) {
        QTreeWidgetItem* row = n < 0 ? outliner.topLevelItem(0) : outliner.topLevelItem(1)->child(n);
        REQUIRE(row != nullptr);
        const QPointF at(outliner.columnViewportPosition(column) + outliner.columnWidth(column) / 2.0, outliner.visualItemRect(row).center().y());
        REQUIRE(outliner.viewport()->rect().contains(at.toPoint()));
        send(outliner.viewport(), type, at, Qt::LeftButton, Qt::LeftButton, {});
        send(outliner.viewport(), QEvent::MouseMove, at + QPointF(0, 30), Qt::NoButton, Qt::LeftButton, {});  // a hand that slips
        send(outliner.viewport(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
        w.window.tick();
    };
    const auto surfacesOf = [&](PanelId panel) {
        std::vector<render::Surface> found;
        for (const render::Surface& surface : w.window.space()->sceneFor(200, 200).surfaces)
            if (surface.panel == panel) found.push_back(surface);
        return found;
    };
    REQUIRE(outliner.panelsShown().front() == front);

    press(0, Outliner::eyeColumn);
    EXPECT((!w.window.session().panelShown(front) && surfacesOf(front).empty() && !surfacesOf(back).empty()), "the eye hides that panel in 3D");
    EXPECT(w.window.selected() == back, "and the selection is as it was");
    EXPECT(w.window.session().panelActive(front), "it is still simulated");
    press(0, Outliner::eyeColumn, QEvent::MouseButtonDblClick);
    EXPECT((w.window.session().panelShown(front) && surfacesOf(front).size() == 1), "a second quick click shows it again");
    EXPECT(w.window.selected() == back, "still the same selection");

    press(0, Outliner::activeColumn);
    EXPECT((!w.window.session().panelActive(front) && w.window.session().panelShown(front)), "the other button deactivates it");
    REQUIRE(surfacesOf(front).size() == 1);
    EXPECT(surfacesOf(front)[0].colour.alpha < 1.0f, "it is seen through");
    EXPECT(w.window.selected() == back, "and the selection is as it was");
    const float stoodAt = surfacesOf(front)[0].points[0].y, fellFrom = surfacesOf(back)[0].points[0].y;
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 30; ++i) w.window.tick();
    EXPECT(surfacesOf(front)[0].points[0].y == stoodAt, "simulating, it stands where it was");
    EXPECT(surfacesOf(back)[0].points[0].y != fellFrom, "while the other moves");

    w.window.select(front);
    w.window.tick();
    EXPECT((outliner.topLevelItem(1)->child(0)->toolTip(Outliner::activeColumn).startsWith("Deactivated")), "its row still says so after the rows are made again");
    press(0, Outliner::activeColumn);
    EXPECT((w.window.session().panelActive(front) && surfacesOf(front)[0].colour.alpha == 1.0f), "active again");

    press(-1, Outliner::eyeColumn);
    EXPECT(!w.window.bodyShown(), "the body's eye works as before");
    EXPECT(w.window.selected() == front, "and leaves the selection too");
}

TEST_CASE("View > See-through body and the button on the body's row draw the body see-through, and back", "[qt]")
{
    Window w;
    Outliner& outliner = *w.window.outliner();
    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.tick();
    auto* action = w.window.findChild<QAction*>("seeThroughBody");
    REQUIRE(action != nullptr);
    EXPECT((action->text() == "See-through body" && action->isCheckable() && !action->isChecked()), "in the View menu, off at first");
    // The body's surface as the view has it: the one that is no panel's.
    const auto body = [&] {
        for (const render::Surface& surface : w.window.space()->sceneFor(200, 200).surfaces)
            if (!surface.panel) return surface;
        FAIL("no body in the scene");
        return render::Surface{};
    };
    int drawn = 0;
    QObject::connect(w.window.space(), &SpaceView::drawn, [&] { ++drawn; });
    EXPECT((body().colour.alpha == 1.0f), "solid");

    // A click with the mouse on the row's button.
    const auto press = [&] {
        QTreeWidgetItem* row = outliner.topLevelItem(0);
        const QPointF at(outliner.columnViewportPosition(Outliner::seeThroughColumn) + outliner.columnWidth(Outliner::seeThroughColumn) / 2.0,
                         outliner.visualItemRect(row).center().y());
        REQUIRE(outliner.viewport()->rect().contains(at.toPoint()));
        send(outliner.viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, {});
        send(outliner.viewport(), QEvent::MouseMove, at + QPointF(0, 30), Qt::NoButton, Qt::LeftButton, {});  // a hand that slips
        send(outliner.viewport(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
        w.window.tick();
    };
    press();
    EXPECT((w.window.bodySeeThrough() && action->isChecked()), "the button and the menu are one thing");
    EXPECT((body().colour.alpha < 1.0f && body().nearestOnly), "the body is see-through in 3D");
    EXPECT((outliner.bodySeeThroughHint().startsWith("See-through")), "and its row says so");
    EXPECT((w.window.selected() == front), "the selection is as it was");
    EXPECT((w.window.bodyShown()), "the body is still shown, and in the simulation");
    if (w.window.space()->drawing() != SpaceView::Drawing::Nothing) EXPECT(drawn > 0, "and the 3D view is drawn again");

    drawn = 0;
    action->trigger();
    EXPECT((!w.window.bodySeeThrough() && body().colour.alpha == 1.0f), "the menu makes it solid again");
    EXPECT((outliner.bodySeeThroughHint().startsWith("Solid")), "and the row says that");
    if (w.window.space()->drawing() != SpaceView::Drawing::Nothing) EXPECT(drawn > 0, "drawn again");

    action->trigger();
    w.window.open(Document{}, "new");
    w.window.tick();
    EXPECT((!w.window.bodySeeThrough() && !action->isChecked() && body().colour.alpha == 1.0f), "it is saved with a pattern: a new one shows the body solid");
}

TEST_CASE("a seam's eye hides its lines in 3D, and the eye on Seams hides them all; the selection stays", "[qt]")
{
    Window w;
    Outliner& outliner = *w.window.outliner();
    const std::vector<SeamId> listed = outliner.seamsShown();
    REQUIRE(listed.size() >= 2);
    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.tick();
    QTreeWidgetItem* seams = outliner.topLevelItem(2);
    seams->setExpanded(true);
    // A click with the mouse on the eye of a row.
    const auto pressEye = [&](QTreeWidgetItem* row) {
        REQUIRE(row != nullptr);
        outliner.scrollToItem(row);
        const QPointF at(outliner.columnViewportPosition(Outliner::eyeColumn) + outliner.columnWidth(Outliner::eyeColumn) / 2.0,
                         outliner.visualItemRect(row).center().y());
        REQUIRE(outliner.viewport()->rect().contains(at.toPoint()));
        send(outliner.viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, {});
        send(outliner.viewport(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, {});
        w.window.tick();
    };
    const auto seamLines = [&] {
        int count = 0;
        for (const render::Lines& lines : w.window.space()->sceneFor(200, 200).lines) count += lines.lift > 0.0f;
        return count;
    };
    const int all = seamLines();
    REQUIRE(all == 2 * static_cast<int>(listed.size()));

    pressEye(seams->child(0));
    EXPECT((!w.window.session().seamShown(listed[0]) && seamLines() == all - 2), "that seam's lines are gone");
    EXPECT((w.window.selected() == front && !w.window.selectedSeam()), "and the selection is as it was");
    pressEye(seams->child(0));
    EXPECT((w.window.session().seamShown(listed[0]) && seamLines() == all), "and back");

    pressEye(seams);
    EXPECT((w.window.session().hiddenSeams().size() == listed.size() && seamLines() == 0), "the eye on Seams hides them all");
    EXPECT(seams->isExpanded(), "without folding the list away");
    pressEye(seams->child(1));
    EXPECT(seamLines() == 2, "one can be shown by itself");
    pressEye(seams);
    EXPECT(seamLines() == 0, "with any shown, the eye on Seams hides them");
    pressEye(seams);
    EXPECT((w.window.session().hiddenSeams().empty() && seamLines() == all), "with all hidden, it shows them");

    pressEye(seams->child(0));
    w.window.select(w.panel("back"));
    w.window.tick();
    EXPECT(outliner.topLevelItem(2)->child(0)->toolTip(Outliner::eyeColumn).startsWith("Hidden"), "its row still says so after the rows are made again");
}

TEST_CASE("a seam whose edges differ in length shows its lengths in orange; an even one does not", "[qt]")
{
    Window w;
    Document doc;
    const PanelId wide = doc.add(Panel{.name = "wide"});
    const OpRef<Rect> wideRect = doc.add(wide, Rect{.width = 30_cm, .height = 10_cm});
    const PanelId narrow = doc.add(Panel{.name = "narrow"});
    const OpRef<Rect> narrowRect = doc.add(narrow, Rect{.width = 20_cm, .height = 10_cm});
    const SeamId uneven = doc.add(Seam{.a = {wide, wideRect.top()}, .b = {narrow, narrowRect.top()}});
    const SeamId even = doc.add(Seam{.a = {wide, wideRect.left()}, .b = {narrow, narrowRect.right()}});
    w.window.open(std::move(doc), "seams");
    w.window.tick();
    w.window.select(wide);
    const auto colourOf = [&](SeamId seam) {
        auto* card = w.window.attributes()->findChild<QWidget*>(QString("seam%1").arg(seam.value));
        REQUIRE(card != nullptr);
        auto* lengths = card->findChild<QLabel*>("seamLengths");
        REQUIRE(lengths != nullptr);
        return lengths->styleSheet();
    };
    EXPECT((colourOf(uneven).contains(theme::uneven.name())), "30 cm sewn to 20 cm is shown in orange");
    EXPECT((!colourOf(even).contains(theme::uneven.name())), "10 cm sewn to 10 cm is not");
}

TEST_CASE("a change of a number updates the docks in place: seam cards, layer summaries and Outliner rows are not made again", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 60_cm});
    const PanelId wide = doc.add(Panel{.name = "wide"});
    const OpRef<Rect> wideRect = doc.add(wide, Rect{.width = waist / 2, .height = 10_cm});
    const PanelId narrow = doc.add(Panel{.name = "narrow"});
    const OpRef<Rect> narrowRect = doc.add(narrow, Rect{.width = 20_cm, .height = 10_cm});
    const SeamId seam = doc.add(Seam{.a = {wide, wideRect.top()}, .b = {narrow, narrowRect.top()}});
    w.window.open(std::move(doc), "numbers");
    w.window.tick();
    w.window.select(wide);

    // Each of them is marked, to tell whether it is the same one afterwards:
    // a new widget can be given the old one's address, so addresses prove nothing.
    bool cardRemade = false;
    QWidget* card = w.window.attributes()->findChild<QWidget*>(QString("seam%1").arg(seam.value));
    REQUIRE(card != nullptr);
    QObject::connect(card, &QObject::destroyed, [&] { cardRemade = true; });
    QLabel* lengths = card->findChild<QLabel*>("seamLengths");
    REQUIRE(lengths != nullptr);
    const QString lengthsBefore = lengths->text();
    const auto summary = [&]() -> QLabel* {
        for (QLabel* label : w.window.attributes()->findChildren<QLabel*>())
            if (label->text().contains(QChar(0x00D7))) return label;  // "30 × 10 cm": the Rectangle's layer
        return nullptr;
    };
    bool summaryRemade = false;
    REQUIRE(summary() != nullptr);
    QObject::connect(summary(), &QObject::destroyed, [&] { summaryRemade = true; });
    const QString summaryBefore = summary()->text();
    const QList<QTreeWidgetItem*> rows = w.window.outliner()->findItems("wide", Qt::MatchExactly | Qt::MatchRecursive);
    REQUIRE(rows.size() == 1);
    rows[0]->setData(0, Qt::UserRole + 99, 12345);

    emit w.window.parameters()->valueTyped(waist, "64 cm");
    w.window.tick();

    EXPECT((!cardRemade), "the seam's card is the same one");
    const QWidget* cardNow = w.window.attributes()->findChild<QWidget*>(QString("seam%1").arg(seam.value));
    const QLabel* lengthsNow = cardNow ? cardNow->findChild<QLabel*>("seamLengths") : nullptr;
    EXPECT((lengthsNow != nullptr && lengthsNow->text() != lengthsBefore && lengthsNow->text().contains("32")), "and shows the new lengths");
    EXPECT((!summaryRemade), "the layer's row is the same one");
    EXPECT((summary() != nullptr && summary()->text() != summaryBefore && summary()->text().contains("32")), "and its summary shows the new number, not the old");
    const QList<QTreeWidgetItem*> rowsAfter = w.window.outliner()->findItems("wide", Qt::MatchExactly | Qt::MatchRecursive);
    EXPECT((rowsAfter.size() == 1 && rowsAfter[0]->data(0, Qt::UserRole + 99).toInt() == 12345), "the Outliner's row is the same one");

    // What does change the docks' structure still does: a new name is a new row.
    w.window.outliner()->rename(wide, "bodice");
    EXPECT((w.window.outliner()->findItems("bodice", Qt::MatchExactly | Qt::MatchRecursive).size() == 1), "a rename shows in the Outliner");
}

TEST_CASE("a panel is renamed, made base only and deleted in the Outliner", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.outliner()->rename(front, "  bodice ");
    EXPECT((w.doc().get(front).name == "bodice"), "a name typed in the Outliner renames the panel, trimmed");
    w.window.outliner()->rename(front, "   ");
    EXPECT((w.doc().get(front).name == "bodice"), "an empty name is refused and the old one stays");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(front).name == "front"), "undo brings the old name back");

    w.window.outliner()->setBaseOnly(front, true);
    EXPECT((w.doc().get(front).construction), "ticked in the menu, the panel is a base only");
    w.window.outliner()->setBaseOnly(front, false);
    EXPECT((!w.doc().get(front).construction), "unticked, it is cut from cloth again");

    const std::size_t before = w.doc().panels().size();
    w.window.outliner()->remove(front);
    EXPECT((w.doc().panels().size() == before - 1 && !w.window.selected()), "Delete removes the panel and the selection");
}

TEST_CASE("a panel's particle distance is set in the Attribute Editor, for that panel only; its counts are shown", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    const PanelId back = w.panel("back");
    EXPECT((w.window.dock("Cloth") == nullptr), "there is no Cloth tab any more");
    w.window.select(front);
    w.window.tick();
    auto* editor = w.window.attributes();
    EXPECT((editor->meshShown().contains("particles") && editor->meshShown().contains("stitches")), "the panel's particles, triangles and stitches are shown");
    const QString before = editor->meshShown();

    type(editor->particleDistance(), "1");
    EXPECT((w.doc().get(front).particleDistance == 1.0 && !w.doc().get(back).particleDistance), "typing 1 sets the selected panel's distance, not the other's");
    w.window.tick();
    EXPECT((editor->meshShown() != before), "its counts follow the finer mesh");

    type(editor->particleDistance(), "0.1");
    EXPECT((w.doc().get(front).particleDistance == 1.0 && editor->particleDistance()->property("refused").toBool()), "a distance out of range is refused, with the field marked");
    w.window.shelf()->undo()->trigger();
    EXPECT((!w.doc().get(front).particleDistance), "undo gives the default back");
}

TEST_CASE("a number of a panel's place that is refused leaves its field marked", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    const double before = w.doc().get(front).arrangement.at.y;
    ValueField* field = w.window.attributes()->spaceField(session::SpaceField::Y);
    type(field, "1 / 0");
    EXPECT((w.doc().get(front).arrangement.at.y == before && field->property("refused").toBool()), "a formula that comes to no number changes nothing, and the field says so");
}

TEST_CASE("the shelf gives a mirrored instance and bakes the selected panel", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpId rect = doc.add(band, Rect{.width = waist / 2, .height = 4_cm}).id;
    w.window.open(doc, "band");
    w.window.select(band);
    EXPECT((w.window.shelf()->bake()->isEnabled()), "with a panel selected the shelf offers it");

    w.window.shelf()->mirroredInstance()->trigger();
    REQUIRE(w.window.selected().has_value());
    const PanelId mirrored = *w.window.selected();
    EXPECT((!(mirrored == band)), "the new panel is selected");
    const Instance& follows = std::get<Instance>(w.doc().get(w.doc().operations(mirrored).front()));
    EXPECT((follows.source == band && follows.mirrored), "it follows the band, mirrored");
    EXPECT((w.doc().get(mirrored).at.x > w.doc().get(band).at.x), "and stands to its right on the table");

    w.window.select(band);
    w.window.shelf()->bake()->trigger();
    const Field width = fields(w.doc().get(rect))[0];
    EXPECT((asNumber(width.value) && *asNumber(width.value) == 35.0), "the width formula is baked into its number");
    w.window.shelf()->undo()->trigger();
    EXPECT((!asNumber(fields(w.doc().get(rect))[0].value)), "undo brings the formula back");
}

TEST_CASE("the shelf instances every selected panel with its seams, and sews an instance as its source later", "[qt]")
{
    Window w;
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = 60_cm});
    const PanelId back = doc.add(Panel{.name = "back", .at = {40, 0}});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 60_cm});
    doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    w.window.open(doc, "legs");
    Shelf& shelf = *w.window.shelf();
    EXPECT((!shelf.sewAsSource()->isEnabled()), "nothing is selected");
    w.window.select(front);
    EXPECT((!shelf.sewAsSource()->isEnabled()), "a panel with its own shape is selected");

    emit w.window.table()->gestured(session::SelectPanels{{front, back}, session::Pick::Replace});
    shelf.mirroredInstance()->trigger();
    REQUIRE((w.doc().panels().size() == 4 && w.doc().seams().size() == 2));
    const std::vector<PanelId> made = w.window.selectedPanels();
    REQUIRE(made.size() == 2);
    EXPECT((w.doc().instanceOf(made[0]) && w.doc().instanceOf(made[1]) && w.doc().instanceOf(made[1])->mirrored), "two mirrored instances, both selected");
    EXPECT((w.window.helpLine() == "1 seam sewn as on the panels they follow."), "the help line says what was sewn");
    EXPECT((shelf.sewAsSource()->isEnabled()), "an instance is selected");
    shelf.undo()->trigger();
    EXPECT((w.doc().panels().size() == 2 && w.doc().seams().size() == 1), "one undo takes the instances and their seam");

    shelf.redo()->trigger();
    emit w.window.table()->gestured(session::SewEdges{{front, frontRect.left()}, {back, backRect.right()}});
    w.window.select(made[0]);
    shelf.sewAsSource()->trigger();
    EXPECT((w.doc().seams().size() == 4), "the seam made on the sources afterwards is copied");
    EXPECT((w.window.helpLine().startsWith("1 seam sewn as on ")), "and the help line says so");
    shelf.undo()->trigger();
    EXPECT((w.doc().seams().size() == 3), "one undo takes it back");
}

TEST_CASE("a parameter is renamed, refused a name that cannot be typed, and removed with its problem shown", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const Param hip = doc.add(Parameter{.name = "hip", .value = waist + 20_cm});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpId rect = doc.add(band, Rect{.width = waist / 2, .height = 4_cm}).id;
    w.window.open(doc, "band");
    ParametersPanel& parameters = *w.window.parameters();

    QLineEdit* name = parameters.nameField(waist);
    REQUIRE(name != nullptr);
    name->setText("waist_girth");
    name->setModified(true);
    emit name->editingFinished();
    EXPECT((w.doc().get(waist).name == "waist_girth"), "the parameter is renamed");
    EXPECT((formatExpr(fields(w.doc().get(rect))[0].value, w.doc()).find("waist_girth") != std::string::npos), "and formulas show the new name");

    name = parameters.nameField(waist);
    name->setText("2 waist");
    name->setModified(true);
    emit name->editingFinished();
    EXPECT((w.doc().get(waist).name == "waist_girth"), "a name that cannot be typed in a formula is refused");
    EXPECT((w.window.helpLine().contains("can't be a parameter name")), "and the help line says why");

    name = parameters.nameField(waist);
    name->setText("hip");
    name->setModified(true);
    emit name->editingFinished();
    EXPECT((w.doc().get(waist).name == "waist_girth"), "a name another parameter has is refused");

    parameters.remove(waist);
    EXPECT((!w.doc().contains(waist)), "the bin removes the parameter");
    EXPECT((!parameters.problemShown(hip).isEmpty()), "a parameter that used it shows its problem in red");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(waist) && parameters.problemShown(hip).isEmpty()), "undo brings it back, and the problem goes");
}

TEST_CASE("Add operation offers a shape for an empty panel, then changes; one needing an edge asks for it", "[qt]")
{
    Window w;
    Document doc;
    const PanelId piece = doc.add(Panel{.name = "piece"});
    w.window.open(doc, "piece");
    w.window.select(piece);
    AttributeEditor& editor = *w.window.attributes();
    const std::vector<session::Offer> shapes = editor.offered();
    EXPECT((std::find(shapes.begin(), shapes.end(), session::Offer::Rectangle) != shapes.end() &&
            std::find(shapes.begin(), shapes.end(), session::Offer::Curve) == shapes.end()),
           "an empty panel is offered shapes, not changes");

    editor.addOperation(session::Offer::Rectangle);
    REQUIRE(w.doc().operations(piece).size() == 1);
    EXPECT((std::holds_alternative<Rect>(w.doc().get(w.doc().operations(piece).front()))), "the rectangle is the panel's shape");
    const std::vector<session::Offer> changes = editor.offered();
    EXPECT((std::find(changes.begin(), changes.end(), session::Offer::Curve) != changes.end()), "then it is offered changes");

    editor.addOperation(session::Offer::Curve);
    const OpId curve = w.doc().operations(piece).back();
    EXPECT((std::holds_alternative<Curve>(w.doc().get(curve))), "a curve is added on top");
    EXPECT((editor.isOpen(curve)), "and its layer is open");
    EXPECT((w.window.helpLine().contains("Click an edge")), "its edge is chosen next on the table");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().operations(piece).size() == 1), "undo takes it away");
}

namespace {

// Presses a field, moves the mouse sideways in steps with the button held, and lets go.
void dragField(QWidget& field, double pixels) {
    const QPointF from(10.0, 5.0);
    const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons held) {
        QMouseEvent event(type, at, field.mapToGlobal(at), button, held, Qt::NoModifier);
        QApplication::sendEvent(&field, &event);
    };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 5; ++step) send(QEvent::MouseMove, from + QPointF(pixels * step / 5.0, 0.0), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, from + QPointF(pixels, 0.0), Qt::LeftButton, Qt::NoButton);
}

} // namespace

TEST_CASE("a number is dragged sideways to change it, as one undo step; a formula is not dragged", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const Param hip = doc.add(Parameter{.name = "hip", .value = waist + 20_cm});
    const PanelId band = doc.add(Panel{.name = "band"});
    doc.add(band, Rect{.width = waist / 2, .height = 4_cm});
    w.window.open(doc, "band");
    ParametersPanel& parameters = *w.window.parameters();

    dragField(*parameters.field(waist), 50.0);
    EXPECT_NEAR(*asNumber(w.doc().get(waist).value), 75.0, "fifty pixels right add 5 cm (0.1 cm a pixel)");
    EXPECT_NEAR(w.window.session().editor().shapes()[hip], 95.0, "and what follows it follows");
    w.window.shelf()->undo()->trigger();
    EXPECT_NEAR(*asNumber(w.doc().get(waist).value), 70.0, "one undo takes back the whole drag");

    dragField(*parameters.field(hip), 50.0);
    EXPECT((!asNumber(w.doc().get(hip).value).has_value()), "a formula stays a formula: it is typed, not dragged");
}

namespace {

// Presses one key on a widget, with the text it types.
void press(QWidget& widget, Qt::Key key, const QString& text = {}) {
    QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(&widget, &down);
    QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QApplication::sendEvent(&widget, &up);
}

// Clicks a field open (everything selected, so typing replaces it) and types keys.
void typeKeys(ValueField& field, const QString& keys) {
    dragField(field, 0.0);
    REQUIRE(field.hasFocus());
    for (QChar key : keys) press(field, static_cast<Qt::Key>(key.unicode()), QString(key));
}

// The band with a waist, in an active window, as on a desktop.
struct Band {
    Window w;
    Param waist;
    Band() {
        Document doc;
        waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
        const PanelId band = doc.add(Panel{.name = "band"});
        doc.add(band, Rect{.width = waist / 2, .height = 4_cm});
        w.window.open(doc, "band");
        w.window.activateWindow();
        QApplication::processEvents();
        REQUIRE(w.window.isActiveWindow());
    }
    ValueField& field() { return *w.window.parameters()->field(waist); }
    double value() { return *asNumber(w.doc().get(waist).value); }
};

} // namespace

// The tool keys and Space are shortcuts of the whole window (Shelf), so a box
// that is typed in has to take them itself. QTest hands a key to Qt's
// shortcuts first, as the system's keys go; an event sent to a widget skips them.
TEST_CASE("Space and the tool keys typed into a field are only typed: the cloth stays paused and the tool as it was", "[qt]")
{
    Table w;
    w.window.activateWindow();
    QApplication::processEvents();
    REQUIRE(w.window.isActiveWindow());
    Shelf& shelf = *w.window.shelf();
    QString keys = " ";  // Space, then every tool's key
    for (int tool = 0; tool <= static_cast<int>(Tool::Pull); ++tool) keys += shelf.action(static_cast<Tool>(tool))->shortcut().toString().toLower();
    REQUIRE((keys.contains('q') && keys.contains('s') && keys.size() > 10));

    // Where nothing is typed in, these keys do reach the shortcuts: without that the rest would show nothing.
    w.table()->setFocus();
    QTest::keyClick(w.table(), 's');
    QTest::keyClick(w.table(), ' ');
    REQUIRE((shelf.tool() == Tool::Sew && shelf.simulate()->isChecked()));
    shelf.simulate()->setChecked(false);
    choose(w, Tool::Select);

    // Types the keys one at a time into the box that has the focus, then leaves it with Escape.
    const auto typeInto = [&](QLineEdit* box, const std::string& where) {
        INFO(where);
        REQUIRE(box != nullptr);
        REQUIRE(QApplication::focusWidget() == box);
        box->selectAll();
        const Tool tool = shelf.tool();
        QString typed;
        for (const QChar key : keys) {
            QTest::keyClick(box, key.toLatin1());
            typed += key;
            INFO("typed \"" << typed.toStdString() << "\"");
            REQUIRE(QApplication::focusWidget() == box);
            EXPECT(box->text() == typed, "the box holds what was typed");
            EXPECT((!shelf.simulate()->isChecked() && !w.window.session().simulating()), "the cloth stays paused");
            EXPECT(shelf.tool() == tool, "the tool is as it was");
        }
        QTest::keyClick(box, Qt::Key_Escape);
        QApplication::processEvents();
    };

    ValueField* value = w.window.parameters()->field(w.widthParameter);
    dragField(*value, 0.0);  // a click without a drag opens it
    typeInto(value, "a parameter's value");

    QLineEdit* name = w.window.parameters()->nameField(w.widthParameter);
    name->setFocus(Qt::MouseFocusReason);
    typeInto(name, "a parameter's name");

    Outliner& outliner = *w.window.outliner();
    QTreeWidgetItemIterator row(&outliner);
    while (*row && (*row)->text(0) != "plain") ++row;
    REQUIRE(*row != nullptr);
    outliner.setFocus();
    outliner.setCurrentItem(*row);
    QTest::keyClick(&outliner, Qt::Key_F2);
    QApplication::processEvents();
    typeInto(qobject_cast<QLineEdit*>(QApplication::focusWidget()), "the Outliner's rename box");

    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    w.table()->grab();
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    send(w.table(), QEvent::MouseButtonDblClick, w.table()->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    send(w.table(), QEvent::MouseButtonRelease, w.table()->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::NoButton, {});
    typeInto(w.table()->lengthEditor(), "the box for a length on the table");

    EXPECT((w.panel("plain") == plain && w.doc().get(w.widthParameter).name == "width"), "Escape left the names as they were");
}

// On a desktop the window is active, and Qt gives a clicked widget focus
// before the press reaches it; a field that took that focus typed instead of
// dragging. Off-screen the window is not active unless asked, which hid this.
TEST_CASE("a number is dragged in an active window too; a click without a drag opens it for typing", "[qt]")
{
    Band b;
    dragField(b.field(), 50.0);
    EXPECT_NEAR(b.value(), 75.0, "the press starts a drag, not typing");
    EXPECT((!b.field().hasFocus()), "and the field is not open for typing after it");

    dragField(b.field(), 0.0);
    EXPECT((b.field().hasFocus() && b.field().selectedText() == b.field().text()), "a click without a drag opens the number for typing, all selected");
    EXPECT_NEAR(b.value(), 75.0, "and changes nothing");
}

TEST_CASE("Enter takes a typed number and closes the field, so the next press drags again", "[qt]")
{
    Band b;
    typeKeys(b.field(), "80");
    press(b.field(), Qt::Key_Return);
    EXPECT_NEAR(b.value(), 80.0, "Enter takes what was typed");
    EXPECT((!b.field().hasFocus()), "and closes the field");
    EXPECT((b.field().text() == "80 cm"), "which shows the number with its unit");
    dragField(b.field(), 50.0);
    EXPECT_NEAR(b.value(), 85.0, "the next press drags");
}

TEST_CASE("Escape puts back what was shown and closes the field, changing nothing", "[qt]")
{
    Band b;
    typeKeys(b.field(), "90");
    press(b.field(), Qt::Key_Escape);
    EXPECT_NEAR(b.value(), 70.0, "what was typed is not taken");
    EXPECT((b.field().text() == "70 cm" && !b.field().hasFocus()), "the value is shown again and the field is closed");
    EXPECT((!b.field().property("refused").toBool()), "nothing is marked as refused");
    dragField(b.field(), 50.0);
    EXPECT_NEAR(b.value(), 75.0, "the next press drags");
}

// A mark is only restyled when it changes; this checks that the marks still
// follow the value. That less restyling happens is not measured here: a
// custom QStyle cannot be put on a field while the window uses a style sheet.
TEST_CASE("a field's refused mark stays until the value is shown again", "[qt]")
{
    Band b;
    ValueField& field = b.field();
    type(&field, "not a number");
    EXPECT((field.property("refused").toBool()), "what does not read is marked");
    field.clearFocus();
    EXPECT((field.property("refused").toBool()), "and stays marked when the field is left");
    dragField(field, 50.0);
    EXPECT_NEAR(b.value(), 75.0, "a drag still changes the number");
    EXPECT((!field.property("refused").toBool() && field.text() == "75 cm"), "and the value shown again clears the mark");
}

TEST_CASE("the body is hidden in 3D and taken out of the simulation together; the table's grid can be hidden", "[qt]")
{
    Window w;
    w.window.showBody(false);
    EXPECT((!w.window.bodyShown()), "the body is hidden");
    const render::Scene scene = w.window.space()->sceneFor(400, 300);
    bool anyBody = false;
    for (const render::Surface& surface : scene.surfaces) anyBody = anyBody || !surface.panel.has_value();
    EXPECT((!anyBody), "and not drawn in 3D");
    w.window.tick();
    EXPECT((w.window.outliner()->bodyShown() == "avatarV1.obj"), "the Outliner still names it");
    w.window.outliner()->setBodyShown(true);  // a click on its eye
    EXPECT((w.window.bodyShown()), "and shows it again from there");
    w.window.outliner()->setBodyShown(false);
    EXPECT((!w.window.bodyShown()), "or hides it again");
    w.window.outliner()->setBodyShown(true);

    w.window.table()->setGridShown(false);
    EXPECT((!w.window.table()->gridShown()), "the grid can be hidden");
}

// ---- The table's floor ----

namespace {

// The columns of the table's picture where a grid line crosses its second row
// (the grid's own colours, as TableView draws them).
std::vector<int> gridColumns(TableView* table) {
    const QImage picture = table->grab().toImage();
    std::vector<int> columns;
    for (int x = 0; x < picture.width(); ++x) {
        const QColor at = picture.pixelColor(x, 2);
        if (at == QColor(32, 34, 39) || at == QColor(44, 47, 54)) columns.push_back(x);
    }
    return columns;
}

} // namespace

TEST_CASE("the table's floor is kept, and drawn again when the view or the grid changes", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const std::vector<int> before = gridColumns(table);
    EXPECT((before.size() > 5), "the grid's lines are drawn");
    EXPECT((gridColumns(table) == before), "painted again without a change, the same picture");

    QWheelEvent zoom(QPointF(100, 100), table->mapToGlobal(QPointF(100, 100)), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(table, &zoom);
    const std::vector<int> zoomed = gridColumns(table);
    EXPECT((!zoomed.empty() && zoomed != before), "zoomed, the lines are where the new view puts them, not where they were");

    table->setGridShown(false);
    EXPECT((gridColumns(table).empty()), "a hidden grid is not painted from the picture that had it");
    table->setGridShown(true);
    EXPECT((gridColumns(table) == zoomed), "shown again, it is as it was");
}

// ---- Lengths, seam ends, placing from the table, hints ----

TEST_CASE("the selected panel's lengths are labelled on the table; a double-click on one opens it for typing", "[qt]")
{
    Table w;
    const PanelId driven = w.panel("driven");
    w.window.select(driven);
    w.table()->grab();  // paints it
    REQUIRE(w.table()->lengthLabels().size() == 4);
    const OpRef<Rect> rect(first<Rect>(w.doc(), driven));
    const auto label = std::find_if(w.table()->lengthLabels().begin(), w.table()->lengthLabels().end(),
                                    [&](const auto& l) { return l.second == rect.bottom(); });
    REQUIRE(label != w.table()->lengthLabels().end());
    send(w.table(), QEvent::MouseButtonDblClick, label->first.center(), Qt::LeftButton, Qt::LeftButton, {});
    EXPECT((w.table()->lengthEditor() != nullptr && w.table()->lengthEditor()->text() == "= width"), "the second click opens the box on the table, with the formula in it");
    typeOnTable(w.table(), "26");
    EXPECT((w.width(driven) == 26.0), "a number typed there replaces the formula, in the layer behind it");

    w.window.select(std::nullopt);
    w.table()->grab();  // paints it
    EXPECT((w.table()->lengthLabels().empty()), "with nothing selected nothing is labelled");
}

TEST_CASE("the ends of a seam's edges that meet are linked; reversing the seam crosses the links", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain"), driven = w.panel("driven");
    choose(w, Tool::Sew);
    // The two top edges, each clicked toward the other end of the table: not reversed.
    click(table, table->toScreen(between(w.corner(plain, -1, 1), w.corner(plain, 1, 1), 0.3)));
    click(table, table->toScreen(between(w.corner(driven, -1, 1), w.corner(driven, 1, 1), 0.7)));
    REQUIRE(w.doc().seams().size() == 1);
    REQUIRE(!w.doc().get(w.doc().seams().front()).reversed);
    REQUIRE(table->seamLinks().size() == 2);
    const Vec2 wentTo = table->seamLinks()[0].second;
    emit w.window.attributes()->seamReversed(w.doc().seams().front(), true);
    REQUIRE(table->seamLinks().size() == 2);
    EXPECT((!(table->seamLinks()[0].second == wentTo)), "reversed, each end goes to the other end");
}

TEST_CASE("a seam the cloth could not close says so on its card, until the cloth is reset", "[qt]")
{
    Window w;
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId back = doc.add(Panel{.name = "back", .at = {30, 0}});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(front, Arrangement{.placed = true, .at = {0, 30, 5}});
    doc.arrange(back, Arrangement{.placed = true, .at = {0, 30, -5}, .turn = 180});
    const SeamId seam = doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    w.window.open(std::move(doc), "open seam");
    w.window.select(front);
    w.window.tick();
    EXPECT((w.window.attributes()->openShown(seam).isEmpty()), "with no cloth the card says nothing of it");

    const session::Session& session = w.window.session();
    w.window.shelf()->simulate()->setChecked(true);
    w.window.tick();
    REQUIRE(session.clothShown());
    // One stitched point pulled under the floor and held there, where the one sewn to it cannot follow.
    const sim::Stitch stitch = session.cloth().mesh->stitches.front();
    emit w.window.space()->clothPulled(session.cloth().mesh->panels[stitch.panelA].panel, stitch.pointA, {0, -40, 0}, session::DragPhase::Begin);
    for (int i = 0; i < 600; ++i) w.window.tick();
    const QString said = w.window.attributes()->openShown(seam);
    EXPECT((said.startsWith("Left open: ") && said.endsWith(" cm apart. Move the panels closer, then Reset.")), said.toStdString());

    w.window.select(back);
    EXPECT((w.window.attributes()->openShown(seam) == said), "the other panel's card of the same seam says it too, at once");
    w.window.shelf()->reset()->trigger();
    w.window.tick();
    EXPECT((w.window.attributes()->openShown(seam).isEmpty()), "after Reset there is nothing to say");
}

TEST_CASE("a T-shirt side seam is sewn by two clicks near the hem, without a twist; the links show it before the second", "[qt]")
{
    Window w;  // the T-shirt body
    TableView* table = w.window.table();
    const PanelId front = w.panel("front"), back = w.panel("back");
    // The sample's first seam is a side seam; it is taken away, to be sewn by hand.
    Document doc = w.doc();
    const SeamId sample = doc.seams().front();
    const Seam side = doc.get(sample);
    REQUIRE((side.a.panel == front && side.b.panel == back && !side.reversed));
    doc.remove(sample);
    doc.place(back, doc.get(front).at + Vec2{90.0, 0.0});  // clear of the front
    w.window.open(std::move(doc), "to sew");
    w.window.tick();
    table->fit();
    // A place on a sewn edge, on the table: `up` of the way from its lower end.
    const auto onEdge = [&](const SeamSide& which, double up) {
        const Pattern& shape = w.window.session().editor().shapes()[which.panel];
        const Edge& edge = shape.edge(which.edge);
        Vec2 low = shape.vertices[edge.start].position, high = shape.vertices[edge.end].position;
        if (low.y > high.y) std::swap(low, high);
        const Panel& placed = w.doc().get(which.panel);
        return placed.at + low + (high - low) * up;
    };
    const auto near = [](Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-6; };
    // Whether the links join the two lower ends, and the two upper.
    const auto lowToLow = [&](const std::vector<std::pair<Vec2, Vec2>>& links) {
        if (links.size() != 2) return false;
        return std::any_of(links.begin(), links.end(), [&](const auto& link) { return near(link.first, onEdge(side.a, 0)) && near(link.second, onEdge(side.b, 0)); }) &&
               std::any_of(links.begin(), links.end(), [&](const auto& link) { return near(link.first, onEdge(side.a, 1)) && near(link.second, onEdge(side.b, 1)); });
    };

    w.window.shelf()->action(Tool::Sew)->trigger();
    click(table, table->toScreen(onEdge(side.a, 0.15)));
    EXPECT((table->previewLinks().empty()), "over no second edge there are no links to show");
    send(table, QEvent::MouseMove, table->toScreen(onEdge(side.b, 0.85)), Qt::NoButton, Qt::NoButton, {});
    EXPECT((table->previewLinks().size() == 2 && !lowToLow(table->previewLinks())), "near the other's upper end, the links cross");
    send(table, QEvent::MouseMove, table->toScreen(onEdge(side.b, 0.15)), Qt::NoButton, Qt::NoButton, {});
    EXPECT(lowToLow(table->previewLinks()), "near its hem, hem goes to hem");
    table->grab();  // painted with the preview
    click(table, table->toScreen(onEdge(side.b, 0.15)));
    REQUIRE(w.doc().seams().size() == 4);
    const Seam sewn = w.doc().get(w.doc().seams().back());
    EXPECT((sewn.a == side.a && sewn.b == side.b && !sewn.reversed), "sewn as the sample has it: no twist");
    EXPECT((table->previewLinks().empty()), "sewn, the preview is gone");

    w.window.shelf()->undo()->trigger();
    w.window.shelf()->action(Tool::Sew)->trigger();
    click(table, table->toScreen(onEdge(side.a, 0.15)));
    click(table, table->toScreen(onEdge(side.b, 0.85)));
    REQUIRE(w.doc().seams().size() == 4);
    EXPECT((w.doc().get(w.doc().seams().back()).reversed), "clicked near opposite ends, those ends meet: twisted, as asked");
}

TEST_CASE("a parameter's name that does not fit shows its start, and whole in its tooltip", "[qt]")
{
    Window w;
    w.window.resize(900, 700);  // narrow docks
    const Param armhole = w.parameter("armhole_depth");
    QLineEdit* name = w.window.parameters()->nameField(armhole);
    REQUIRE(name != nullptr);
    name->setFixedWidth(40);  // too narrow for the name, whatever the font
    // At the start: the cursor is before the first letter, and the box is scrolled so that it is in sight at the left.
    const auto atStart = [&] {
        name->grab();  // painted, so that it is scrolled as it will be seen
        return name->cursorPosition() == 0 && name->inputMethodQuery(Qt::ImCursorRectangle).toRectF().left() < name->width() / 3.0;
    };
    EXPECT((atStart()), "the start of the name is shown");
    EXPECT((name->toolTip().startsWith("armhole_depth")), "and the whole name is in the tooltip");

    name->end(false);  // typing at its end scrolls it
    // Then left. Off screen no window has the focus, so the box is told as Qt would tell it.
    QFocusEvent left(QEvent::FocusOut, Qt::MouseFocusReason);
    QApplication::sendEvent(name, &left);
    EXPECT((atStart()), "left, it shows its start again");

    name->setFocus();
    name->selectAll();
    name->insert("a_much_longer_name_for_the_depth");  // as typed: the box knows it was changed
    emit name->editingFinished();
    name->clearFocus();
    REQUIRE(w.doc().get(armhole).name == "a_much_longer_name_for_the_depth");
    EXPECT((w.window.parameters()->nameField(armhole)->toolTip().startsWith("a_much_longer_name_for_the_depth")), "renamed, the tooltip follows");
    EXPECT((atStart()), "and the new name shows its start");
}

TEST_CASE("a panel is placed in space again where the table has it", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const double was = w.doc().get(plain).arrangement.at.x;
    emit w.window.attributes()->spaceTyped(plain, session::SpaceField::X, "250");
    REQUIRE(w.doc().get(plain).arrangement.at.x == 250.0);
    REQUIRE(w.window.shelf()->placeFromTable()->isEnabled());
    w.window.shelf()->placeFromTable()->trigger();
    EXPECT((w.doc().get(plain).arrangement.placed && std::abs(w.doc().get(plain).arrangement.at.x - was) < 1e-9), "it stands where it stood when it was new");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(plain).arrangement.at.x == 250.0), "and undo puts it back");
}

TEST_CASE("an empty table says how to start; the grid's step is written in its corner", "[qt]")
{
    Window w;
    w.window.open(Document{}, "empty");
    EXPECT((w.window.helpLine().contains("table is empty")), "an empty document says how to draw the first panel");

    const QString near = w.window.table()->gridKey();
    EXPECT((near.startsWith("grid ") && near.endsWith("cm")), "the grid's step, with its unit");
    QWheelEvent away(QPointF(50, 50), w.window.table()->mapToGlobal(QPointF(50, 50)), {}, {0, -2400}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w.window.table(), &away);
    EXPECT((w.window.table()->gridKey() != near), "zoomed far out, the grid's lines are farther apart and it says so");
}

// ---- Delete, copy, paste and duplicate; the origin ----

namespace {

// A key sent to a widget, as the keyboard does to the one that has the focus.
void pressKey(QWidget* widget, Qt::Key key, Qt::KeyboardModifiers keys = {}) {
    QKeyEvent down(QEvent::KeyPress, key, keys);
    QApplication::sendEvent(widget, &down);
}

void moveOver(TableView* table, QPointF at) { send(table, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton, {}); }

bool canUndo(Window& w) { return w.window.shelf()->undo()->isEnabled(); }

} // namespace

TEST_CASE("Delete on the table takes the selected panel, or the corners the Point tool added; each is one undo step", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    const Vec2 bottomLeft = w.corner(plain, -1, -1), bottomRight = w.corner(plain, 1, -1);

    pressKey(table, Qt::Key_Delete);
    EXPECT((w.doc().panels().size() == 2 && w.window.helpLine().contains("Nothing is selected")), "with nothing selected, Delete says so and removes nothing");

    w.window.select(plain);
    click(table, table->toScreen(w.corner(plain, 1, 1)));
    REQUIRE(w.window.selectedCorners().size() == 1);
    pressKey(table, Qt::Key_Delete);
    EXPECT((w.doc().panels().size() == 2 && w.window.helpLine().contains("1. Rectangle")), "a corner of the rectangle is refused, and the help line names its layer");
    EXPECT((!canUndo(w)), "nothing was changed");

    choose(w, Tool::Point);
    click(table, table->toScreen(between(bottomLeft, bottomRight, 0.4)));
    REQUIRE(lastIs<AddPoint>(w.doc(), plain));
    click(table, table->toScreen(between(bottomLeft, bottomRight, 0.4)));
    REQUIRE(w.window.selectedCorners().size() == 1);
    pressKey(table, Qt::Key_Delete);
    EXPECT((!lastIs<AddPoint>(w.doc(), plain) && w.doc().operations(plain).size() == 1 && w.doc().panels().size() == 2), "Delete takes the corner the Point tool added, and keeps the panel");
    w.window.shelf()->undo()->trigger();
    EXPECT((lastIs<AddPoint>(w.doc(), plain)), "one undo brings the corner back");

    click(table, table->toScreen(w.middle(plain)));
    REQUIRE((w.window.selected() == plain && w.window.selectedCorners().empty()));
    pressKey(table, Qt::Key_Delete);
    EXPECT((w.doc().panels().size() == 1 && !w.window.selected()), "with no corner selected, Delete takes the panel");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().panels().size() == 2 && lastIs<AddPoint>(w.doc(), w.panel("plain"))), "and one undo brings it back with its layers");
}

TEST_CASE("Ctrl+C remembers a panel, Ctrl+V pastes it under the pointer, Ctrl+D duplicates it beside; the copy follows the same parameter", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId driven = w.panel("driven");

    pressKey(table, Qt::Key_V, Qt::ControlModifier);
    EXPECT((w.doc().panels().size() == 2 && w.window.helpLine().contains("Nothing has been copied")), "pasting before copying says so");

    click(table, table->toScreen(w.middle(driven)));
    REQUIRE(w.window.selected() == driven);
    pressKey(table, Qt::Key_C, Qt::ControlModifier);
    EXPECT((w.doc().panels().size() == 2 && w.window.helpLine().contains("Copied")), "Ctrl+C changes no document and says what was copied");

    moveOver(table, table->toScreen({200, 150}));
    pressKey(table, Qt::Key_V, Qt::ControlModifier);
    REQUIRE(w.doc().panels().size() == 3);
    const PanelId pasted = w.doc().panels().back();
    EXPECT((w.window.selected() == pasted), "the pasted panel is selected");
    EXPECT((std::abs(w.middle(pasted).x - 200.0) < 0.1 && std::abs(w.middle(pasted).y - 150.0) < 0.1), "with its middle under the pointer");
    moveOver(table, table->toScreen({-100, 150}));
    pressKey(table, Qt::Key_V, Qt::ControlModifier);
    REQUIRE(w.doc().panels().size() == 4);
    EXPECT((std::abs(w.middle(w.doc().panels().back()).x + 100.0) < 0.1), "pasting again puts another copy where the pointer is now");

    type(w.window.parameters()->field(w.widthParameter), "30 cm");
    EXPECT((w.width(driven) == 30.0 && w.width(pasted) == 30.0), "the copy follows the parameter, as the original does");

    w.window.shelf()->undo()->trigger();  // the parameter
    w.window.shelf()->undo()->trigger();  // the second paste
    EXPECT((w.doc().panels().size() == 3), "each paste is one undo step");

    click(table, table->toScreen(w.middle(driven)));
    pressKey(table, Qt::Key_D, Qt::ControlModifier);
    REQUIRE(w.doc().panels().size() == 4);
    const PanelId beside = w.doc().panels().back();
    EXPECT((w.window.selected() == beside && w.doc().get(beside).name == "driven copy 2" && w.doc().get(beside).at.x > w.doc().get(driven).at.x + 20.0),
           "Ctrl+D puts a copy of the selected panel beside it, selected");
}

TEST_CASE("a pasted panel is refused once the copied one is gone, and nothing is copied in a new document", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    click(table, table->toScreen(w.middle(plain)));
    pressKey(table, Qt::Key_C, Qt::ControlModifier);
    pressKey(table, Qt::Key_Delete);
    REQUIRE(w.doc().panels().size() == 1);
    pressKey(table, Qt::Key_V, Qt::ControlModifier);
    EXPECT((w.doc().panels().size() == 1 && w.window.helpLine().contains("gone")), "the copied panel is gone: the help line says so, and nothing is pasted");

    click(table, table->toScreen(w.middle(w.panel("driven"))));
    pressKey(table, Qt::Key_C, Qt::ControlModifier);
    w.window.open(Document{}, "another");
    pressKey(w.window.table(), Qt::Key_V, Qt::ControlModifier);
    EXPECT((w.doc().panels().empty() && w.window.helpLine().contains("Nothing has been copied")), "another document starts with nothing copied");

    pressKey(w.window.table(), Qt::Key_C, Qt::ControlModifier);
    EXPECT((w.window.helpLine().contains("Nothing to copy")), "Ctrl+C with no panel selected says so");
}

TEST_CASE("Delete, Ctrl+C, Ctrl+V and Ctrl+D work in the 3D view on the selected panel, as on the table", "[qt]")
{
    Window w;
    SpaceView* space = w.window.space();
    const PanelId front = w.panel("front");
    const std::size_t panels = w.doc().panels().size();
    w.window.select(front);

    space->key(QKeyCombination(Qt::ControlModifier, Qt::Key_C));
    EXPECT((w.doc().panels().size() == panels && w.window.helpLine().contains("Copied")), "Ctrl+C remembers the panel");
    space->key(QKeyCombination(Qt::ControlModifier, Qt::Key_V));
    EXPECT((w.doc().panels().size() == panels + 1 && w.window.selected() != front), "Ctrl+V pastes a copy, selected");
    w.window.select(front);
    space->key(QKeyCombination(Qt::ControlModifier, Qt::Key_D));
    EXPECT((w.doc().panels().size() == panels + 2 && w.window.selected() != front), "Ctrl+D duplicates the selected panel");

    w.window.select(front);
    space->key(QKeyCombination(Qt::Key_Delete));
    EXPECT((w.doc().panels().size() == panels + 1 && !w.window.selected()), "Delete removes the selected panel");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().panels().size() == panels + 2 && w.doc().contains(front)), "one undo brings it back");

    w.window.select(std::nullopt);
    space->key(QKeyCombination(Qt::Key_Delete));
    EXPECT((w.doc().panels().size() == panels + 2 && w.window.helpLine().contains("Nothing is selected")), "with nothing selected it says so");
}

TEST_CASE("Shift- or Ctrl-click adds a panel to the selection or takes it out; a box selects the panels it touches", "[qt]")
{
    Table w;
    Document row;
    for (int i = 0; i < 3; ++i) {
        const PanelId panel = row.add(Panel{.name = std::string(1, static_cast<char>('a' + i)), .at = {40.0 * i, 0.0}});
        row.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    }
    w.window.open(std::move(row), "row");
    w.window.tick();
    TableView* table = w.table();
    const PanelId a = w.panel("a"), b = w.panel("b"), c = w.panel("c");
    const auto at = [&](PanelId panel) { return table->toScreen(w.middle(panel)); };
    const auto where = [&](double x, double y) { return table->toScreen({x, y}); };

    click(table, at(a));
    click(table, at(b), Qt::ControlModifier);
    EXPECT((w.window.selected() == b && w.window.selectedPanels().size() == 2), "Ctrl-click adds a panel; it is the one shown");
    click(table, at(b), Qt::ControlModifier);
    EXPECT((w.window.selected() == a && w.window.selectedPanels().size() == 1), "Ctrl-click on a selected panel takes it out");
    drag(table, at(a), by(at(a), table, 0, 12), Qt::ControlModifier);
    EXPECT((w.window.selected() == a && w.doc().get(a).at.y > 5.0), "Ctrl-drag on a selected panel moves it and keeps it selected");
    w.window.shelf()->undo()->trigger();
    click(table, at(b));
    EXPECT((w.window.selectedPanels() == std::vector<PanelId>{b}), "a plain click selects only that panel");
    click(table, at(a), Qt::ShiftModifier);
    EXPECT((w.window.selectedPanels().size() == 2), "Shift-click adds a panel, as Ctrl-click does");
    click(table, at(a));
    EXPECT((w.window.selectedPanels() == std::vector<PanelId>{a}), "a plain click on one of several selected panels selects only it");
    click(table, at(b));

    // A box on empty table, with a panel chosen, is for its corners; with Ctrl it is for panels.
    drag(table, where(-5, 14), where(45, -4), Qt::ControlModifier);
    EXPECT((w.window.selectedPanels().size() == 2 && w.window.selected() != c), "a box with Ctrl adds the panels it touches (a and b)");
    drag(table, where(75, 14), where(85, -4), Qt::ControlModifier);
    EXPECT((w.window.selectedPanels().size() == 3), "another box with Ctrl adds the next");
    click(table, where(-1000, -1000));
    EXPECT((w.window.selectedPanels().empty()), "a click on empty table chooses nothing");
    drag(table, where(-5, 14), where(45, -4));
    EXPECT((w.window.selectedPanels().size() == 2 && w.window.selected() != c), "with nothing chosen, a plain box selects the panels it touches");
    drag(table, where(-20, 30), where(-10, 20));
    EXPECT((w.window.selectedPanels().empty()), "a box over nothing chooses nothing, as a click on empty table does");

    // Several panels at once: copy, paste and delete.
    w.window.select(a);
    click(table, at(c), Qt::ControlModifier);
    REQUIRE(w.window.selectedPanels().size() == 2);
    pressKey(table, Qt::Key_C, Qt::ControlModifier);
    EXPECT((w.window.helpLine().contains("Copied 2 panels")), "Ctrl+C says how many panels it copied");
    moveOver(table, where(300, 100));
    pressKey(table, Qt::Key_V, Qt::ControlModifier);
    REQUIRE(w.doc().panels().size() == 5);
    EXPECT((w.window.selectedPanels().size() == 2 && w.window.selected() == w.doc().panels()[3]), "Ctrl+V pastes both, selected");
    EXPECT((std::abs(w.doc().get(w.doc().panels()[4]).at.x - w.doc().get(w.doc().panels()[3]).at.x - 80.0) < 0.1), "as far apart as the originals");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().panels().size() == 3), "the paste is one undo step");

    w.window.select(a);
    click(table, at(c), Qt::ControlModifier);
    pressKey(table, Qt::Key_Delete);
    EXPECT((w.doc().panels().size() == 1 && w.doc().contains(b) && w.window.selectedPanels().empty()), "Delete takes both selected panels");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().panels().size() == 3), "one undo brings both back");
}

namespace {

// Three panels, 20 by 10 cm each, at x = 0, 40 and 80.
void openRow(Table& w) {
    Document row;
    for (int i = 0; i < 3; ++i) {
        const PanelId panel = row.add(Panel{.name = std::string(1, static_cast<char>('a' + i)), .at = {40.0 * i, 0.0}});
        row.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    }
    w.window.open(std::move(row), "row");
    w.window.tick();
}

// Where a panel's own middle is on the table, turned and placed.
Vec2 tableMiddle(const Table& w, PanelId panel) {
    const std::optional<Bounds> box = bounds(w.window.session().editor().shapes()[panel]);
    const Panel& placed = w.doc().get(panel);
    const Vec2 own = turned(Vec2{(box->min.x + box->max.x) / 2, (box->min.y + box->max.y) / 2}, placed.angle);
    return placed.at + own;
}

} // namespace

TEST_CASE("several selected panels are dragged together, as one undo step; a click on one selects it alone", "[qt]")
{
    Table w;
    openRow(w);
    TableView* table = w.table();
    const PanelId a = w.panel("a"), b = w.panel("b"), c = w.panel("c");
    const auto at = [&](PanelId panel) { return table->toScreen(w.middle(panel)); };
    const Panel aWas = w.doc().get(a), bWas = w.doc().get(b), cWas = w.doc().get(c);

    drag(table, table->toScreen({-5, 14}), table->toScreen({45, -4}));
    REQUIRE(w.window.selectedPanels().size() == 2);
    drag(table, at(a), by(at(a), table, 12, -4));
    EXPECT((std::abs(w.doc().get(a).at.x - aWas.at.x - 12.0) < 0.2 && std::abs(w.doc().get(a).at.y - aWas.at.y + 4.0) < 0.2), "the panel dragged moves with the pointer");
    EXPECT((std::abs(w.doc().get(b).at.x - bWas.at.x - 12.0) < 0.2 && std::abs(w.doc().get(b).at.y - bWas.at.y + 4.0) < 0.2), "and the other selected panel moves as far");
    EXPECT((w.doc().get(c).at.x == cWas.at.x && w.doc().get(c).at.y == cWas.at.y), "one that is not selected stays");
    EXPECT((w.window.selectedPanels().size() == 2), "the drag leaves them selected");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(a).at.x == aWas.at.x && w.doc().get(b).at.x == bWas.at.x && w.doc().get(b).at.y == bWas.at.y), "one undo puts both back");

    drag(table, at(b), by(at(b), table, 7.3, 2.1), Qt::ControlModifier);
    EXPECT((w.window.selectedPanels().size() == 2 && w.doc().get(b).at.x != bWas.at.x && w.doc().get(a).at.x != aWas.at.x), "Ctrl-dragging one of them moves both too, and keeps them selected");
    w.window.shelf()->undo()->trigger();

    // Shift holds the group to the axis the pointer first moved along.
    drag(table, at(a), by(at(a), table, 3, 9), Qt::ShiftModifier);
    EXPECT((w.doc().get(a).at.x == aWas.at.x && w.doc().get(b).at.x == bWas.at.x && std::abs(w.doc().get(a).at.y - aWas.at.y - 9.0) < 0.2 && std::abs(w.doc().get(b).at.y - bWas.at.y - 9.0) < 0.2),
           "with Shift they all go up, and not sideways at all");
    EXPECT((w.window.selectedPanels().size() == 2), "and stay selected");
    w.window.shelf()->undo()->trigger();

    // The move gizmo moves them all, from the middle of the group.
    choose(w, Tool::Move);
    REQUIRE(table->gizmo());
    EXPECT((std::abs(table->gizmo()->centre.x - 30.0) < 1e-6 && std::abs(table->gizmo()->centre.y - 5.0) < 1e-6), "the gizmo stands at the middle of the group");
    const QPointF onArrow = table->toScreen(session::handleEnd(*table->gizmo(), session::TableHandle::AlongX)) - QPointF(25.0, 0.0);
    drag(table, onArrow, onArrow + QPointF(40.0, -30.0));
    EXPECT((std::abs(w.doc().get(a).at.x - aWas.at.x - 40.0 / table->scale()) < 0.06 && std::abs(w.doc().get(b).at.x - bWas.at.x - 40.0 / table->scale()) < 0.06 &&
            w.doc().get(a).at.y == aWas.at.y && w.doc().get(b).at.y == bWas.at.y),
           "its x arrow moves both along x only");
    w.window.shelf()->undo()->trigger();
    choose(w, Tool::Select);

    click(table, at(a));
    EXPECT((w.window.selectedPanels() == std::vector<PanelId>{a} && w.doc().get(a).at.x == aWas.at.x), "a click on a selected panel selects only it, and moves nothing");
}

TEST_CASE("several selected panels are turned and scaled together about the middle of the group, and duplicated with Ctrl+D", "[qt]")
{
    Table w;
    openRow(w);
    TableView* table = w.table();
    const PanelId a = w.panel("a"), b = w.panel("b");
    drag(table, table->toScreen({-5, 14}), table->toScreen({45, -4}));
    REQUIRE(w.window.selectedPanels().size() == 2);
    const Vec2 aMiddle = tableMiddle(w, a), bMiddle = tableMiddle(w, b);

    choose(w, Tool::Rotate);
    REQUIRE(table->gizmo());
    const QPointF pivot = table->toScreen(table->gizmo()->centre);
    drag(table, pivot + QPointF(session::tableRingPixels, 0.0), pivot + QPointF(0.0, -session::tableRingPixels), Qt::ControlModifier);
    EXPECT((std::abs(w.doc().get(a).angle - 90.0) < 1e-6 && std::abs(w.doc().get(b).angle - 90.0) < 1e-6), "both turn by the angle swept");
    const Vec2 aNow = tableMiddle(w, a), bNow = tableMiddle(w, b);
    EXPECT((std::abs(std::hypot(bNow.x - aNow.x, bNow.y - aNow.y) - std::hypot(bMiddle.x - aMiddle.x, bMiddle.y - aMiddle.y)) < 1e-6), "they stay as far apart as they were");
    EXPECT((std::abs((aNow.x + bNow.x) / 2 - 30.0) < 1e-6 && std::abs((aNow.y + bNow.y) / 2 - 5.0) < 1e-6), "turned about the middle of the group, which stays where it was");
    EXPECT((std::abs(aNow.x - 30.0) < 1e-6 && std::abs(aNow.y + 15.0) < 1e-6), "a quarter turn counter-clockwise: the left panel is now below");
    EXPECT((w.width(a) == 20.0), "turning is not shape");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(a).angle == 0.0 && w.doc().get(b).angle == 0.0 && std::abs(tableMiddle(w, a).x - aMiddle.x) < 1e-9), "one undo turns both back");

    choose(w, Tool::Scale);
    REQUIRE(table->gizmo());
    const QPointF centre = table->toScreen(table->gizmo()->centre);
    const QPointF end = table->toScreen(session::handleEnd(*table->gizmo(), session::TableHandle::AlongX));
    const QPointF unit = (end - centre) / session::tableArrowPixels;
    const std::size_t layers = w.doc().operations(a).size();
    drag(table, centre + unit * 60.0, centre + unit * 90.0);
    EXPECT((w.doc().operations(a).size() == layers && w.width(a) == 20.0 && w.window.helpLine().contains("evenly")), "pulled out one way, they are refused with a sentence, and nothing changes");

    // The middle of the gizmo scales evenly: 50 pixels to the right is half as big again.
    drag(table, centre, centre + QPointF(50.0, 0.0));
    EXPECT((std::abs(w.width(a) - 30.0) < 0.1 && std::abs(w.width(b) - 30.0) < 0.1 && w.doc().operations(a).size() == layers), "both are half as big again, and no layer is added");
    const Vec2 groupMiddle = *session::middleOfPanels(w.doc(), w.window.session().editor().shapes(), {a, b});
    EXPECT((std::abs(groupMiddle.x - 30.0) < 1e-6 && std::abs(groupMiddle.y - 5.0) < 1e-6), "the middle of the group stays where it was");
    EXPECT((w.doc().panels().size() == 3), "so far, three panels");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.width(a) == 20.0 && w.width(b) == 20.0), "one undo puts both back");

    pressKey(table, Qt::Key_D, Qt::ControlModifier);
    EXPECT((w.doc().panels().size() == 5 && w.window.selectedPanels().size() == 2), "Ctrl+D copies both, and the copies are selected");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().panels().size() == 3), "one undo takes both copies away");
}

TEST_CASE("a box dragged with one panel selected takes its corners if it touches only that panel, and panels otherwise", "[qt]")
{
    Table w;
    openRow(w);
    TableView* table = w.table();
    const PanelId a = w.panel("a"), b = w.panel("b");
    w.window.select(a);
    // Pressed outside, over a's right side and not touching b (which starts at x = 40).
    drag(table, table->toScreen({15, 14}), table->toScreen({25, -4}));
    EXPECT((w.window.selectedPanels() == std::vector<PanelId>{a} && w.window.selectedCorners().size() == 2), "touching only the selected panel: its two corners");
    // Touching the selected panel and the next one: panels, a and b, whatever was selected.
    drag(table, table->toScreen({15, 14}), table->toScreen({45, -4}));
    EXPECT((w.window.selectedPanels().size() == 2 && w.window.selectedCorners().empty()), "touching a second panel too: both panels are selected");
    EXPECT((w.window.selectedPanels() == std::vector<PanelId>{b, a} || w.window.selectedPanels() == std::vector<PanelId>{a, b}), "and only those two");
}

namespace {

// The picture's pixel at a place in the widget, which the picture may have in more pixels.
QColor pixelAt(const QImage& picture, QPointF at) {
    const double ratio = picture.devicePixelRatio();
    return picture.pixelColor(static_cast<int>(std::lround(at.x() * ratio)), static_cast<int>(std::lround(at.y() * ratio)));
}

bool reddish(const QColor& c) { return c.red() > c.green() + 30 && c.red() > c.blue() + 30; }
bool greenish(const QColor& c) { return c.green() > c.red() + 30 && c.green() > c.blue() + 30; }

} // namespace

TEST_CASE("the origin is drawn on the grid, x in red and y in green, and moves with the view; without the grid it is not drawn", "[qt]")
{
    Window w;
    w.window.open(Document{}, "empty");  // nothing to frame: the origin is in the middle of the table
    TableView* table = w.window.table();
    const QPointF origin = table->toScreen({0, 0});
    REQUIRE(table->rect().contains(origin.toPoint()));

    const auto xAxisAt = [&](QPointF at) { return pixelAt(table->grab().toImage(), QPointF(at.x() + 60, std::round(at.y()))); };
    const auto yAxisAt = [&](QPointF at) { return pixelAt(table->grab().toImage(), QPointF(std::round(at.x()), at.y() + 60)); };
    EXPECT((reddish(xAxisAt(origin))), "the line through (0, 0) along x is red");
    EXPECT((greenish(yAxisAt(origin))), "the line through (0, 0) along y is green");
    const QColor beside = pixelAt(table->grab().toImage(), origin + QPointF(60, 60));
    EXPECT((!reddish(beside) && !greenish(beside)), "the floor beside the axes is not coloured");

    // Panned with the right button: the axes follow the origin and leave where they were.
    send(table, QEvent::MouseButtonPress, {300, 300}, Qt::RightButton, Qt::RightButton, {});
    send(table, QEvent::MouseMove, {340, 340}, Qt::NoButton, Qt::RightButton, {});
    send(table, QEvent::MouseButtonRelease, {340, 340}, Qt::RightButton, Qt::NoButton, {});
    const QPointF moved = table->toScreen({0, 0});
    REQUIRE((std::abs(moved.x() - origin.x() - 40.0) < 1e-6 && std::abs(moved.y() - origin.y() - 40.0) < 1e-6));
    EXPECT((reddish(xAxisAt(moved)) && greenish(yAxisAt(moved))), "after panning, the axes are through the origin's new place");
    EXPECT((!reddish(pixelAt(table->grab().toImage(), QPointF(origin.x() + 60, std::round(origin.y()))))), "and no longer where they were");

    table->setGridShown(false);
    EXPECT((!reddish(xAxisAt(moved)) && !greenish(yAxisAt(moved))), "with the grid hidden the table is bare");
}

// ---- Files, the avatar, and random input ----

TEST_CASE("every sample saves and opens unchanged; a file that cannot be read says why", "[qt]")
{
    Window w;
    // A folder of its own to write in, beside the program: a place that is
    // certainly writable (some machines keep programs from writing in the
    // system's temp folder), and gone again when the test is done.
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-samples-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("sample.twintail");
    for (std::size_t i = 0; i < session::samples().size(); ++i) {
        w.window.openSample(i);
        const std::string before = toText(w.doc());
        REQUIRE(w.window.saveAs(path));
        REQUIRE(w.window.openFile(path));
        EXPECT((toText(w.doc()) == before), std::string(session::samples()[i].name) + " saves and opens unchanged");
        EXPECT((w.window.session().editor().shapes().errors.empty()), "and evaluates without a problem");
    }
    const std::string kept = toText(w.doc());
    EXPECT((!w.window.openFile(folder.filePath("no such file.twintail")) && !w.window.helpLine().isEmpty()), "a missing file is refused, with why");
    EXPECT((toText(w.doc()) == kept), "and the open document stays");
    EXPECT((!w.window.saveAs(folder.filePath("no such folder/x.twintail")) && !w.window.helpLine().isEmpty()), "a place that cannot be written says why");
}

TEST_CASE("each sample opens on its avatar, found from any folder; one that cannot be read leaves the body", "[qt]")
{
    Window w;
    const auto avatarName = [&] { return w.doc().avatar() ? w.doc().avatar()->name : std::string(); };
    EXPECT((avatarName() == "avatarV1.obj"), "the T-shirt is draped on the project's avatar");
    EXPECT((w.window.session().bodySurface().triangles.size() > 1000), "its surface is there");
    EXPECT((w.window.session().bodyReady()), "and ready to collide with soon after");
    EXPECT((w.window.outliner()->bodyShown() == "avatarV1.obj"), "the Outliner names it");

    const QString started = QDir::currentPath();
    QDir::setCurrent(QDir::tempPath());  // as when the program is started from its build folder
    for (std::size_t i = 0; i < session::samples().size(); ++i) {
        w.window.openSample(i);
        const char* avatar = session::samples()[i].avatar;
        EXPECT((avatar != nullptr && avatarName() == avatar), std::string(session::samples()[i].name) + " opens on its avatar");
        EXPECT((!w.window.isWindowModified()), "and is saved as it opens, avatar and all");
    }
    QDir::setCurrent(started);

    const std::shared_ptr<const Avatar> before = w.doc().avatar();
    EXPECT((!w.window.loadAvatar("no/such/avatar.obj") && !w.window.helpLine().isEmpty()), "a missing avatar file is refused, with why");
    EXPECT((w.doc().avatar() == before && !w.window.isWindowModified()), "and the body stays");
}

// ---- Unsaved changes ----

namespace {

// Answers the window's questions as they come, as a person would: clicks `with`
// in a message box, and closes a file dialog without choosing a file.
class Answering {
public:
    explicit Answering(QMessageBox::StandardButton with) {
        QObject::connect(&timer_, &QTimer::timeout, [this, with] {
            QWidget* modal = QApplication::activeModalWidget();
            if (auto* box = qobject_cast<QMessageBox*>(modal)) {
                questions.push_back(box->text());
                if (QAbstractButton* button = box->button(with)) button->click();
                else box->reject();  // a box without that button: the test sees what did not happen
            } else if (auto* files = qobject_cast<QFileDialog*>(modal)) {
                ++fileDialogs;
                fileFilters = files->nameFilters();
                files->reject();
            }
        });
        timer_.start(0);
    }
    QStringList questions;
    int fileDialogs = 0;
    QStringList fileFilters;  // of the last file dialog

private:
    QTimer timer_;
};

void choose(Window& w, const char* menuItem) {
    auto* action = w.window.findChild<QAction*>(menuItem);
    REQUIRE(action != nullptr);
    action->trigger();
}

void setChest(Window& w, const QString& to) { type(w.window.parameters()->field(w.parameter("chest")), to); }

} // namespace

TEST_CASE("a changed pattern shows '*' and is asked about before New, Open, a sample and closing; Cancel keeps all of it", "[qt]")
{
    Window w;
    EXPECT((!w.window.isWindowModified() && w.window.windowTitle().startsWith("6. T-shirt body[*]")), "a sample just opened is saved");

    setChest(w, "100 cm");
    EXPECT(w.window.isWindowModified(), "a change shows the '*'");
    w.window.shelf()->undo()->trigger();
    EXPECT((!w.window.isWindowModified()), "undone, there is nothing to save");
    w.window.shelf()->redo()->trigger();
    EXPECT(w.window.isWindowModified(), "redone, there is");

    const std::string changed = toText(w.doc());
    for (const char* item : {"fileNew", "fileOpen", "fileSample0"}) {
        Answering answer(QMessageBox::Cancel);
        choose(w, item);
        EXPECT((answer.questions.size() == 1 && answer.questions.front().startsWith("Save the changes to 6. T-shirt body before")), std::string(item) + " asks first");
        EXPECT((answer.fileDialogs == 0 && toText(w.doc()) == changed && w.window.isWindowModified()), std::string(item) + " cancelled changes nothing");
    }
    {
        Answering answer(QMessageBox::Cancel);
        EXPECT((!w.window.close()), "closing is cancelled");
        EXPECT((answer.questions.size() == 1 && w.window.isVisible() && toText(w.doc()) == changed), "and the window stays with its pattern");
    }
    {
        // Save, in a pattern that has no file yet, asks for one; that cancelled, everything is kept.
        Answering answer(QMessageBox::Save);
        choose(w, "fileNew");
        EXPECT((answer.questions.size() == 1 && answer.fileDialogs == 1), "Save asks where");
        EXPECT((toText(w.doc()) == changed && w.window.isWindowModified()), "and with no file chosen nothing is left behind");
    }
    {
        Answering answer(QMessageBox::Discard);
        choose(w, "fileSample0");
        EXPECT((answer.questions.size() == 1 && w.window.windowTitle().startsWith(session::samples()[0].name)), "Don't Save opens the sample");
        EXPECT((!w.window.isWindowModified()), "which is saved");
    }
    {
        Answering answer(QMessageBox::Cancel);
        choose(w, "fileNew");
        EXPECT((answer.questions.empty() && w.doc().panels().empty() && w.window.windowTitle().startsWith("untitled[*]")),
               "with nothing unsaved, New asks nothing");
        EXPECT((!w.window.isWindowModified()), "and an empty pattern is saved");
        EXPECT((w.window.close() && answer.questions.empty()), "nor does closing it");
    }
}

TEST_CASE("a saved pattern is offered by Open, opens, and is saved where it is", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-opened-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("mine.twintail");
    setChest(w, "104 cm");
    const std::string text = toText(w.doc(), w.window.session().state());
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()));
    }
    {
        Answering answer(QMessageBox::Discard);
        choose(w, "fileOpen");
        EXPECT((answer.fileDialogs == 1 && answer.fileFilters == QStringList{"Patterns (*.twintail)"}), "Open offers pattern files");
    }
    w.window.openSample(0);
    REQUIRE(w.window.openFile(path));
    EXPECT((asNumber(w.doc().get(w.parameter("chest")).value) == 104.0 && w.window.windowTitle().startsWith("mine[*]")), "it opens as it was saved");

    setChest(w, "108 cm");
    choose(w, "fileSave");
    QFile saved(path);
    REQUIRE(saved.open(QIODevice::ReadOnly));
    EXPECT((!w.window.isWindowModified() && QString::fromUtf8(saved.readAll()).contains("108")), "Save writes to the same file");
}

TEST_CASE("saved as a file, the pattern takes its name and loses the '*'; Save in the question writes it before going on", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-saving-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("mine.twintail");
    setChest(w, "100 cm");
    REQUIRE(w.window.saveAs(path));
    EXPECT((!w.window.isWindowModified() && w.window.windowTitle().startsWith("mine[*]")), "saved, and named after its file");

    setChest(w, "104 cm");
    EXPECT(w.window.isWindowModified(), "changed again");
    {
        Answering answer(QMessageBox::Cancel);
        choose(w, "fileSave");
        EXPECT((!w.window.isWindowModified() && answer.fileDialogs == 0 && answer.questions.empty()), "Save writes to the same file without asking");
    }
    setChest(w, "108 cm");
    {
        Answering answer(QMessageBox::Save);
        choose(w, "fileNew");
        EXPECT((answer.questions.size() == 1 && answer.fileDialogs == 0 && w.doc().panels().empty()), "saved to its file, then the new pattern");
    }
    REQUIRE(w.window.openFile(path));
    w.window.tick();
    EXPECT((w.window.parameters()->field(w.parameter("chest"))->text() == "108 cm"), "what was saved is in the file");
    EXPECT((!w.window.isWindowModified() && w.window.windowTitle().startsWith("mine[*]")), "and a file just opened is saved");

    // A file that cannot be written keeps the '*', and the pattern.
    setChest(w, "110 cm");
    EXPECT((!w.window.saveAs(folder.filePath("no such folder/x.twintail")) && w.window.isWindowModified()), "not saved, still unsaved");
    Answering answer(QMessageBox::Discard);
    EXPECT((w.window.close() && !w.window.isVisible()), "Don't Save closes");
    QCoreApplication::processEvents();
    EXPECT((answer.questions.size() == 1), "asked once");
}

// ---- The avatar in the document ----

namespace {

// A cube 20 cm across standing on the floor, as an OBJ file in `folder`.
QString cubeFile(const QTemporaryDir& folder) {
    const QString path = folder.filePath("cube.obj");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("v -10 0 -10\nv 10 0 -10\nv 10 20 -10\nv -10 20 -10\nv -10 0 10\nv 10 0 10\nv 10 20 10\nv -10 20 10\n"
               "f 5 6 7 8\nf 2 1 4 3\nf 6 2 3 7\nf 1 5 8 4\nf 8 7 3 4\nf 1 2 6 5\n");
    return path;
}

} // namespace

TEST_CASE("another avatar is an undo step; Ctrl+Z brings back the one before, and the drape on it", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-avatars-XXXXXX");
    REQUIRE(folder.isValid());
    const QString cube = cubeFile(folder);
    const std::size_t points = w.window.session().bodySurface().points.size();
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 20; ++i) w.window.tick();
    const double time = w.window.session().cloth().time;

    REQUIRE(w.window.loadAvatar(cube));
    EXPECT((!w.window.shelf()->simulate()->isChecked()), "Play is up: on another body the cloth starts again");
    w.waitForTheBody();
    EXPECT((w.window.session().bodySurface().points.size() == 8 && w.window.outliner()->bodyShown() == "cube.obj"), "the cube is the body");
    EXPECT((!w.window.session().clothShown() && w.window.isWindowModified()), "the pattern has changed");

    REQUIRE(w.window.loadAvatar(cube));
    w.window.shelf()->undo()->trigger();
    w.waitForTheBody();
    EXPECT((w.window.session().bodySurface().points.size() == points && w.window.outliner()->bodyShown() == "avatarV1.obj"),
           "one undo goes back, the cube loaded twice being one step");
    EXPECT((w.window.session().clothShown() && w.window.session().cloth().time == time), "with the drape that hung on that body");
    EXPECT((w.window.isWindowModified()), "which was never saved");
    REQUIRE(w.window.saveAs(folder.filePath("draped.twintail")));
    REQUIRE(w.window.loadAvatar(cube));
    w.waitForTheBody();
    w.window.shelf()->undo()->trigger();
    w.waitForTheBody();
    EXPECT((!w.window.isWindowModified()), "saved, then changed and undone, the pattern and its drape are the saved ones");

    // Undone while the cloth plays, it stands still: Play is up.
    w.window.shelf()->redo()->trigger();
    w.waitForTheBody();
    w.window.shelf()->simulate()->setChecked(true);
    w.window.tick();
    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((!w.window.shelf()->simulate()->isChecked() && !w.window.session().simulating()), "Play is up after an undo of the avatar");
}

TEST_CASE("Play pressed while a new avatar is worked out says so, and the cloth starts by itself when it is ready", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-waiting-XXXXXX");
    REQUIRE(folder.isValid());
    // A box 4 m across, under the panels: large, so that working it out takes a moment.
    const QString path = folder.filePath("large.obj");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("v -200 0 -200\nv 200 0 -200\nv 200 60 -200\nv -200 60 -200\nv -200 0 200\nv 200 0 200\nv 200 60 200\nv -200 60 200\n"
               "f 5 6 7 8\nf 2 1 4 3\nf 6 2 3 7\nf 1 5 8 4\nf 8 7 3 4\nf 1 2 6 5\n");
    file.close();
    auto* bar = w.window.statusBar()->findChild<QProgressBar*>("bodyProgress");
    REQUIRE(bar != nullptr);
    EXPECT(!bar->isVisible(), "no bar while there is nothing to wait for");
    REQUIRE(w.window.loadAvatar(path));
    w.window.tick();
    REQUIRE(!w.window.session().bodyReady());
    EXPECT((bar->isVisible() && bar->value() >= 0 && bar->value() <= 100 && bar->text().startsWith("Preparing the avatar")),
           "a bar in the status line shows how far it is");

    w.window.shelf()->simulate()->setChecked(true);
    w.window.tick();
    EXPECT((w.window.shelf()->simulate()->isChecked() && w.window.session().simulating()), "Play stays down");
    EXPECT(!w.window.session().clothShown(), "the cloth waits");
    EXPECT((w.window.helpLine().contains("avatar") && !w.window.helpLine().startsWith("Playing")), "and the help line says what for");
    w.waitForTheBody();
    EXPECT(!bar->isVisible(), "ready: the bar goes");
    for (int i = 0; i < 10; ++i) w.window.tick();
    EXPECT((w.window.session().clothShown() && w.window.session().cloth().time > 0.0), "then it plays, with no second press");
    EXPECT(w.window.helpLine().startsWith("Playing"), "and says so");

    // Pressed again while the next is worked out, and let go: nothing starts.
    w.window.shelf()->undo()->trigger();
    w.waitForTheBody();
    w.window.shelf()->redo()->trigger();
    w.window.tick();
    REQUIRE(!w.window.session().bodyReady());
    w.window.shelf()->simulate()->setChecked(true);
    w.window.tick();
    w.window.shelf()->simulate()->setChecked(false);
    w.waitForTheBody();
    w.window.tick();
    EXPECT((!w.window.session().simulating() && !w.window.helpLine().startsWith("Playing")), "taken back, it stays back");
}

TEST_CASE("the file holds the avatar: it opens on its body wherever it is, with the OBJ file gone", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-onefile-XXXXXX");
    REQUIRE(folder.isValid());
    const QString cube = cubeFile(folder);
    const QString onCube = folder.filePath("on a cube.twintail"), onAvatar = folder.filePath("on the avatar.twintail");
    REQUIRE(w.window.saveAs(onAvatar));
    const Avatar project = *w.doc().avatar();
    REQUIRE(w.window.loadAvatar(cube));
    REQUIRE(w.window.saveAs(onCube));
    REQUIRE(QFile::remove(cube));

    REQUIRE(w.window.openFile(onAvatar));
    w.waitForTheBody();
    EXPECT((w.doc().avatar() && *w.doc().avatar() == project), "the project's avatar, every point of it, out of the file");
    EXPECT((w.window.session().bodySurface().triangles.size() == project.triangles.size()), "and it is the body");
    REQUIRE(w.window.openFile(onCube));
    w.waitForTheBody();
    EXPECT((w.window.session().bodySurface().points.size() == 8 && w.window.outliner()->bodyShown() == "cube.obj"),
           "the other opens on the cube, whose OBJ file is gone");
    EXPECT((!w.window.isWindowModified()), "and is saved as it opens");

    // A new pattern, and a file from before avatars were saved, take the body that is shown.
    const std::shared_ptr<const Avatar> shown = w.doc().avatar();
    choose(w, "fileNew");
    EXPECT((w.doc().avatar() == shown && !w.window.isWindowModified()), "New keeps the body");
    Document old;
    old.add(old.add(Panel{.name = "old"}), Rect{.width = 10_cm, .height = 10_cm});
    const QString older = folder.filePath("older.twintail");
    REQUIRE(save(old, older.toStdString()).empty());
    REQUIRE(w.window.openFile(older));
    EXPECT((w.doc().avatar() == shown && !w.window.isWindowModified()), "so does a file that names none, and it opens saved");
}

// ---- Choosing how an avatar's file is read ----

namespace {

// A box in metres, off the middle and off the floor, as an OBJ file in `folder`:
// 0.5 wide, 1.75 high, 0.3 deep.
QString metresFile(const QTemporaryDir& folder) {
    const QString path = folder.filePath("metres.obj");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("v 1 -0.5 2\nv 1.5 -0.5 2\nv 1.5 1.25 2\nv 1 1.25 2\nv 1 -0.5 2.3\nv 1.5 -0.5 2.3\nv 1.5 1.25 2.3\nv 1 1.25 2.3\n"
               "f 5 6 7 8\nf 2 1 4 3\nf 6 2 3 7\nf 1 5 8 4\nf 8 7 3 4\nf 1 2 6 5\n");
    return path;
}

// The dialog that is open after choosing a file, and its parts.
struct Choosing {
    AvatarDialog* dialog;
    ValueField *scale, *height;
    QComboBox *unit, *up, *facing;
    QPushButton *ok, *cancel;
    explicit Choosing(Window& w) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // one that was closed is gone
        dialog = w.window.findChild<AvatarDialog*>("avatarImport");
        REQUIRE(dialog != nullptr);
        REQUIRE(dialog->isVisible());
        scale = dialog->findChild<ValueField*>("avatarScale");
        height = dialog->findChild<ValueField*>("avatarHeight");
        unit = dialog->findChild<QComboBox*>("avatarUnit");
        up = dialog->findChild<QComboBox*>("avatarUp");
        facing = dialog->findChild<QComboBox*>("avatarFacing");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        REQUIRE((scale && height && unit && up && facing && buttons));
        ok = buttons->button(QDialogButtonBox::Ok);
        cancel = buttons->button(QDialogButtonBox::Cancel);
    }
    // As a person picks from a list.
    static void pick(QComboBox* list, const QString& text) {
        const int at = list->findText(text);
        REQUIRE(at >= 0);
        list->setCurrentIndex(at);
    }
    QStringList facings() const {
        QStringList all;
        for (int i = 0; i < facing->count(); ++i) all << facing->itemText(i);
        return all;
    }
};

// How high and how low points reach, in cm.
template <class Points>
std::pair<double, double> reachOf(const Points& points) {
    double low = 1e30, high = -1e30;
    for (const auto& p : points) low = std::min<double>(low, p.y), high = std::max<double>(high, p.y);
    return {low, high};
}
std::pair<double, double> reach(const Document& doc) {
    REQUIRE(doc.avatar());
    return reachOf(doc.avatar()->points);
}
// The body the 3D view is given to draw.
std::pair<double, double> shown(Window& w) { return reachOf(w.window.session().shownBody().points); }

} // namespace

TEST_CASE("choosing an avatar's file shows it in 3D beside a dialog with a guess; Cancel changes nothing, OK is one undo step", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-import-XXXXXX");
    REQUIRE(folder.isValid());
    const QString file = metresFile(folder);
    const std::shared_ptr<const Avatar> before = w.doc().avatar();
    const double was = shown(w).second;
    w.window.shelf()->simulate()->setChecked(true);

    w.window.chooseAvatar(file);
    {
        const Choosing choosing(w);
        EXPECT((choosing.unit->currentText() == "m" && choosing.scale->text() == "100 %" && choosing.height->text() == "175 cm"),
               "1.75 high: metres, as it is, a body of 175 cm");
        EXPECT((choosing.up->currentText() == "Y" && choosing.facing->currentText() == "Z"), "y up, facing +z");
        EXPECT((w.window.session().previewingBody() && w.window.session().shownBody().points.size() == 8), "the file's body is shown in 3D");
        EXPECT_NEAR(shown(w).first, 0.0, "standing on the floor");
        EXPECT_NEAR(shown(w).second, 175.0, "as tall as it would be");
        EXPECT((w.doc().avatar() == before && !w.window.isWindowModified()), "and nothing is put on while it is chosen");
        EXPECT(!w.window.shelf()->simulate()->isChecked(), "the cloth waits");
        choosing.cancel->click();
    }
    EXPECT((!w.window.session().previewingBody() && shown(w).second == was), "cancelled: the avatar is shown again");
    EXPECT((w.doc().avatar() == before && !w.window.isWindowModified() && !w.window.shelf()->undo()->isEnabled()), "nothing at all");
    EXPECT(w.window.shelf()->simulate()->isChecked(), "and the cloth plays on");

    w.window.chooseAvatar(file);
    Choosing(w).ok->click();
    w.waitForTheBody();
    EXPECT((w.window.outliner()->bodyShown() == "metres.obj" && w.window.isWindowModified()), "put on");
    EXPECT((!w.window.session().previewingBody() && w.window.session().bodySurface().points.size() == 8), "it is the body");
    EXPECT(!w.window.shelf()->simulate()->isChecked(), "on another body the cloth starts again");
    EXPECT_NEAR(reach(w.doc()).first, 0.0, "standing on the floor");
    EXPECT_NEAR(reach(w.doc()).second, 175.0, "175 cm tall");
    EXPECT((w.doc().avatar()->readAs.unit == 100.0 && w.doc().avatar()->readAs.moved != Vec3{}), "and how its file was read is kept");
    double middle = 0.0;
    for (const Vec3& p : w.doc().avatar()->points) middle += p.x;
    EXPECT_NEAR(middle, 0.0, "in the middle");

    w.window.shelf()->undo()->trigger();
    w.waitForTheBody();
    EXPECT((w.doc().avatar() == before && !w.window.shelf()->undo()->isEnabled()), "one step back, and there was no other");
}

TEST_CASE("in the avatar dialog the unit is how the file is read; a scale or a height resizes the body and leaves the unit", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-scale-XXXXXX");
    REQUIRE(folder.isValid());
    const std::shared_ptr<const Avatar> before = w.doc().avatar();
    w.window.chooseAvatar(metresFile(folder));
    const Choosing choosing(w);

    type(choosing.height, "180");
    EXPECT((choosing.height->text() == "180 cm" && choosing.scale->text() == "102.9 %" && choosing.unit->currentText() == "m"),
           "a height typed: the scale follows, the unit stays");
    EXPECT_NEAR(shown(w).second, 180.0, "and the 3D view with it");
    type(choosing.scale, "200");
    EXPECT((choosing.height->text() == "350 cm" && choosing.scale->text() == "200 %" && choosing.unit->currentText() == "m"), "a scale typed");
    EXPECT_NEAR(shown(w).second, 350.0, "shown");
    type(choosing.scale, "100 %");
    Choosing::pick(choosing.unit, "cm");
    EXPECT((choosing.scale->text() == "100 %" && choosing.height->text() == "1.75 cm"), "another unit: the file read in it");
    EXPECT((!choosing.height->refused() && !choosing.scale->refused()), "a small body is a body all the same");
    EXPECT_NEAR(shown(w).second, 1.75, "shown as small as that");
    Choosing::pick(choosing.unit, "inch");
    EXPECT(choosing.height->text() == "4.45 cm", "inches");
    Choosing::pick(choosing.unit, "mm");
    type(choosing.height, "175 cm");
    EXPECT((choosing.unit->currentText() == "mm" && choosing.scale->text() == "100000 %"), "whatever the unit, the height can be typed");
    Choosing::pick(choosing.unit, "m");
    type(choosing.scale, "100");

    for (ValueField* field : {choosing.height, choosing.scale}) {
        for (const char* wrong : {"tall", "-3", "0", "1000000000000"}) {
            type(field, wrong);
            EXPECT(field->refused(), std::string("refused: ") + wrong);
            choosing.ok->click();
            EXPECT((choosing.dialog->isVisible() && w.doc().avatar() == before), "and OK waits");
        }
        EXPECT_NEAR(shown(w).second, 175.0, "what is shown stays");
        type(field, field == choosing.height ? "168 cm" : "100 %");
        EXPECT(!field->refused(), "until a number is typed");
        type(choosing.scale, "100 %");
        type(choosing.height, "175 cm");
    }

    EXPECT((choosing.facings() == QStringList{"Z", "X", "-Z", "-X"}), "with y up, a body faces along the floor");
    Choosing::pick(choosing.up, "Z");
    EXPECT((choosing.facings() == QStringList{"Y", "X", "-Y", "-X"}), "with z up, too");
    EXPECT(choosing.facing->currentText() == "-Y", "facing as such files usually do");
    EXPECT(choosing.height->text() == "30 cm", "the scale stays, so the height is the other side's");
    EXPECT_NEAR(shown(w).second, 30.0, "shown lying down");
    type(choosing.height, "170");
    Choosing::pick(choosing.facing, "X");
    choosing.ok->click();
    w.waitForTheBody();
    REQUIRE(w.doc().avatar());
    const AvatarImport& read = w.doc().avatar()->readAs;
    EXPECT((read.up == Axis::Z && read.facing == Axis::X && read.unit == 100.0), "the axes and the unit as chosen");
    EXPECT_NEAR(read.rescale, 170.0 / 30.0, "the scale that makes it 170 cm");
    EXPECT_NEAR(reach(w.doc()).first, 0.0, "on the floor");
    EXPECT_NEAR(reach(w.doc()).second, 170.0, "that tall");
}

TEST_CASE("while an avatar is chosen only the dialog and the 3D camera take the hand; closing it gives the window back", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-held-XXXXXX");
    REQUIRE(folder.isValid());
    const QString file = metresFile(folder);
    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.shelf()->layout(ViewLayout::TableOnly)->trigger();
    const std::string pattern = toText(w.doc());
    auto* open = w.window.findChild<QAction*>("fileNew");
    REQUIRE(open != nullptr);

    w.window.chooseAvatar(file);
    EXPECT(w.window.viewLayout() == ViewLayout::SideBySide, "the 3D view is brought up: the body is seen there");
    EXPECT((!w.window.table()->isEnabled() && !w.window.menuBar()->isEnabled() && !w.window.outliner()->isEnabled() &&
            !w.window.parameters()->isEnabled() && !w.window.attributes()->isEnabled()),
           "the table, the menus and the docks wait");
    EXPECT((!open->isEnabled() && !w.window.shelf()->simulate()->isEnabled() && !w.window.shelf()->action(Tool::Move)->isEnabled()), "and so do their keys");
    EXPECT(w.window.space()->isEnabled(), "the 3D view does not");
    EXPECT(!w.window.space()->gizmo(), "with nothing to take hold of");
    w.window.tick();
    EXPECT(!w.window.shelf()->undo()->isEnabled(), "a frame changes nothing of that");
    // What the 3D view reports meanwhile is not acted on.
    emit w.window.space()->panelClicked(std::nullopt);
    emit w.window.space()->gestured(session::MoveInSpace{front, {10, 0, 0}, session::DragPhase::Begin});
    for (const Qt::Key key : {Qt::Key_Delete, Qt::Key_Space, Qt::Key_W}) emit w.window.space()->keyed(QKeyCombination(key));
    EXPECT((toText(w.doc()) == pattern && w.window.selected() == front && !w.window.shelf()->simulate()->isChecked()), "the pattern and the choice stay");

    emit w.window.space()->keyed(QKeyCombination(Qt::Key_Escape));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT((!w.window.findChild<AvatarDialog*>("avatarImport") && !w.window.session().previewingBody()), "Escape in the 3D view cancels");
    EXPECT((w.window.table()->isEnabled() && w.window.menuBar()->isEnabled() && w.window.outliner()->isEnabled() &&
            w.window.parameters()->isEnabled() && open->isEnabled() && w.window.shelf()->simulate()->isEnabled()),
           "and the window is given back");
    EXPECT(!w.window.shelf()->undo()->isEnabled(), "as it was: nothing to undo");
    EXPECT(w.window.viewLayout() == ViewLayout::TableOnly, "in the layout it had");

    // Choosing another file, or another document, ends the choosing first.
    w.window.chooseAvatar(file);
    w.window.chooseAvatar(file);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT(w.window.findChildren<AvatarDialog*>().size() == 1, "one dialog at a time");
    w.window.openSample(0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT((!w.window.findChild<AvatarDialog*>("avatarImport") && !w.window.session().previewingBody() && open->isEnabled() &&
            w.window.table()->isEnabled()),
           "another document: no dialog, no preview, and the window is usable");
    w.window.chooseAvatar(file);
    EXPECT((w.window.close() && !w.window.session().previewingBody()), "and it closes");
}

TEST_CASE("each file dialog starts in the folder of the last file of its kind, also the next time the program runs", "[qt]")
{
    application();  // the folders below are beside the program
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-folder-XXXXXX");
    REQUIRE(folder.isValid());
    const QString settings = folder.filePath("settings.ini");
    for (const char* name : {"patterns", "bodies", "exports"}) REQUIRE(QDir(folder.path()).mkdir(name));
    const auto sub = [&](const char* name) { return QDir(folder.filePath(name)).canonicalPath(); };
    const QString patterns = sub("patterns"), bodies = sub("bodies"), exports = sub("exports");
    const QString cube = bodies + "/cube.obj";
    REQUIRE(QFile::copy(cubeFile(folder), cube));
    const QString assets = QDir(QCoreApplication::applicationDirPath() + "/assets").canonicalPath();
    // Opens a menu item's file dialog and returns the folder it starts in and the name it offers, choosing nothing.
    struct Start {
        QString folder, name;
    };
    const auto startOf = [](Window& w, const char* menuItem) {
        Start start;
        QTimer look;
        QObject::connect(&look, &QTimer::timeout, [&] {
            if (auto* files = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                start.folder = files->directory().canonicalPath();
                if (!files->selectedFiles().isEmpty()) start.name = QFileInfo(files->selectedFiles().front()).fileName();
                files->reject();
            }
        });
        look.start(0);
        choose(w, menuItem);
        return start;
    };
    {
        Window w;
        w.window.keepSettingsIn(settings);
        EXPECT(startOf(w, "bodyAvatar").folder == assets, "at first, where the program's avatars are");

        w.window.chooseAvatar(cube);
        Choosing(w).cancel->click();
        EXPECT(startOf(w, "bodyAvatar").folder == bodies, "then where the last avatar was chosen, also if it was not put on");
        w.window.chooseAvatar("no/such/folder/avatar.obj");
        EXPECT(startOf(w, "bodyAvatar").folder == bodies, "a folder that is not there is not kept");

        REQUIRE(w.window.saveAs(patterns + "/shirt.twintail"));
        EXPECT(startOf(w, "fileOpen").folder == patterns, "Open starts where the pattern was saved");
        const Start again = startOf(w, "fileSaveAs");
        EXPECT((again.folder == patterns && again.name == "shirt.twintail"), "and so does Save as, with the pattern's name");
        const Start beside = startOf(w, "fileExportObj");
        EXPECT((beside.folder == patterns && beside.name == "shirt.obj"), "a first export is offered beside the pattern");
        REQUIRE(w.window.exportObj(exports + "/shirt.obj"));
        EXPECT(startOf(w, "fileExportObj").folder == exports, "then where the last one went");
        EXPECT((startOf(w, "fileOpen").folder == patterns && startOf(w, "bodyAvatar").folder == bodies), "each kind its own");
    }
    {
        Window w;
        w.window.keepSettingsIn(settings);
        EXPECT((startOf(w, "fileOpen").folder == patterns && startOf(w, "fileExportObj").folder == exports && startOf(w, "bodyAvatar").folder == bodies),
               "the next run starts there too");
        // A new pattern's first Save is offered in the patterns' folder.
        choose(w, "fileNew");
        const Start first = startOf(w, "fileSave");
        EXPECT((first.folder == patterns && first.name == "untitled.twintail"), "with its name");
        REQUIRE(w.window.openFile(patterns + "/shirt.twintail"));
        EXPECT(startOf(w, "fileOpen").folder == patterns, "opening keeps it");
    }
    {
        Window w;
        EXPECT(startOf(w, "bodyAvatar").folder == assets, "a window that keeps no settings reads none");
        w.window.chooseAvatar(cube);
        Choosing(w).cancel->click();
        EXPECT(startOf(w, "bodyAvatar").folder == bodies, "and remembers for as long as it runs");
    }
    REQUIRE(QDir(bodies).removeRecursively());
    Window w;
    w.window.keepSettingsIn(settings);
    EXPECT(startOf(w, "bodyAvatar").folder == assets, "a folder that has gone since is not offered");
}

TEST_CASE("View > Instances in violet marks instances in 3D, is off at first and is remembered", "[qt]")
{
    application();  // the folder below is beside the program
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-violet-XXXXXX");
    REQUIRE(folder.isValid());
    const QString settings = folder.filePath("settings.ini");
    // How many panels the 3D view draws violet; the trousers' left leg is two instances.
    const auto violet = [](Window& w) {
        int count = 0;
        for (const render::Surface& surface : w.window.space()->sceneFor(200, 200).surfaces)
            if (surface.panel && surface.colour.blue > surface.colour.green + 0.1f) ++count;
        return count;
    };
    {
        Window w;
        w.window.keepSettingsIn(settings);
        w.window.openSample(6);
        w.window.tick();
        auto* action = w.window.findChild<QAction*>("markInstances");
        REQUIRE(action != nullptr);
        EXPECT((!action->isChecked() && violet(w) == 0), "at first the legs look alike");
        choose(w, "markInstances");
        EXPECT((action->isChecked() && violet(w) == 2), "switched on, the instances are violet at once");
    }
    {
        Window w;
        w.window.keepSettingsIn(settings);
        w.window.openSample(6);
        w.window.tick();
        EXPECT(violet(w) == 2, "the next run has them violet too");
        choose(w, "markInstances");
        EXPECT((violet(w) == 0 && !w.window.isWindowModified()), "off again; the pattern is as it was saved");
    }
    Window w;
    w.window.keepSettingsIn(settings);
    EXPECT(!w.window.findChild<QAction*>("markInstances")->isChecked(), "and off is remembered");
}

TEST_CASE("an avatar file that cannot be read opens no dialog and says why", "[qt]")
{
    Window w;
    const std::shared_ptr<const Avatar> before = w.doc().avatar();
    w.window.chooseAvatar("no/such/avatar.obj");
    EXPECT((!w.window.findChild<AvatarDialog*>("avatarImport") && !w.window.helpLine().isEmpty()), "no dialog, and why");
    EXPECT((w.doc().avatar() == before && !w.window.isWindowModified()), "and the body stays");
}

TEST_CASE("the '*' follows what the file holds: looking leaves it saved; the body's looks, hiding, freezing and the cloth do not", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    const session::Session& session = w.window.session();
    w.window.select(front);
    w.window.shelf()->action(Tool::Move)->trigger();
    w.window.shelf()->mesh()->setChecked(true);
    w.window.shelf()->layout(ViewLayout::SpaceOnly)->trigger();
    w.window.tick();
    EXPECT((!w.window.isWindowModified()), "selecting, tools, the mesh and the views are not in the file");

    w.window.showBodySeeThrough(true);
    EXPECT(w.window.isWindowModified(), "the body see-through is");
    w.window.showBodySeeThrough(false);
    EXPECT((!w.window.isWindowModified()), "and solid again it is as saved");
    w.window.showBody(false);
    EXPECT(w.window.isWindowModified(), "so is a hidden body");
    w.window.showBody(true);
    emit w.window.outliner()->panelShownChanged(front, false);
    EXPECT(w.window.isWindowModified(), "a hidden panel");
    emit w.window.outliner()->panelShownChanged(front, true);
    emit w.window.outliner()->panelActiveChanged(front, false);
    EXPECT(w.window.isWindowModified(), "a frozen one");
    emit w.window.outliner()->panelActiveChanged(front, true);
    const SeamId seam = w.doc().seams().front();
    emit w.window.outliner()->seamShownChanged(seam, false);
    EXPECT(w.window.isWindowModified(), "a hidden seam");
    emit w.window.outliner()->seamShownChanged(seam, true);
    EXPECT((!w.window.isWindowModified()), "each put back, nothing has changed");

    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 5; ++i) w.window.tick();
    w.window.shelf()->simulate()->setChecked(false);
    w.window.tick();
    EXPECT((session.clothShown() && w.window.isWindowModified()), "cloth that has fallen is a change");
    {
        Answering answer(QMessageBox::Cancel);
        EXPECT((!w.window.close() && answer.questions.size() == 1), "asked about before it is lost");
    }
    w.window.shelf()->reset()->trigger();
    w.window.tick();
    EXPECT((!w.window.isWindowModified()), "reset, the garment is as saved");

    emit w.window.space()->gestured(session::MoveInSpace{front, {5.0, 0.0, 0.0}, session::DragPhase::Begin});
    emit w.window.space()->gestured(session::MoveInSpace{front, {5.0, 0.0, 0.0}, session::DragPhase::End});
    w.window.tick();
    EXPECT(w.window.isWindowModified(), "where a panel stands is part of the pattern");
    w.window.shelf()->undo()->trigger();
    EXPECT((!w.window.isWindowModified()), "and undone it is as saved");
}

TEST_CASE("a file opens as it was left: the drape, what is frozen and hidden, and the body's looks", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-left-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("left.twintail");
    const session::Session& session = w.window.session();
    const PanelId front = w.panel("front"), back = w.panel("back");
    const SeamId seam = w.doc().seams().front();
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 90; ++i) w.window.tick();
    emit w.window.outliner()->panelActiveChanged(back, false);  // frozen as it hangs
    for (int i = 0; i < 5; ++i) w.window.tick();
    w.window.shelf()->simulate()->setChecked(false);
    emit w.window.outliner()->panelShownChanged(front, false);
    emit w.window.outliner()->seamShownChanged(seam, false);
    w.window.showBodySeeThrough(true);
    w.window.tick();
    const sim::Placement hung = session.cloth().positions;
    const double time = session.cloth().time;
    REQUIRE(w.window.saveAs(path));
    w.window.tick();
    EXPECT((!w.window.isWindowModified()), "all of it is saved");

    w.window.openSample(6);
    w.window.tick();
    EXPECT((!session.clothShown() && !session.bodySeeThrough() && session.hiddenPanels().empty() && !w.window.isWindowModified()),
           "another pattern shows nothing of it");
    const auto asLeft = [&] {
        EXPECT((session.clothShown() && !session.simulating() && !w.window.shelf()->simulate()->isChecked()), "the cloth is there, standing still");
        REQUIRE((session.clothShown() && session.cloth().positions.size() == hung.size()));
        double off = 0.0;
        for (std::size_t p = 0; p < hung.size(); ++p) {
            REQUIRE(session.cloth().positions[p].size() == hung[p].size());
            for (std::size_t i = 0; i < hung[p].size(); ++i) off = std::max(off, length(session.cloth().positions[p][i] - hung[p][i]));
        }
        EXPECT((off == 0.0 && session.cloth().time == time), "every particle where it was left");
        EXPECT((!session.panelActive(back) && !session.panelShown(front) && !session.seamShown(seam)), "frozen and hidden as left");
        EXPECT((session.bodySeeThrough() && w.window.findChild<QAction*>("seeThroughBody")->isChecked()), "the body see-through, and the menu says so");
        EXPECT((!w.window.isWindowModified()), "and it is saved");
    };
    REQUIRE(w.window.openFile(path));
    asLeft();
    w.waitForTheBody();
    for (int i = 0; i < 5; ++i) w.window.tick();
    asLeft();
    // Played on, it carries on from there; the frozen panel stays.
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 10; ++i) w.window.tick();
    std::size_t frozen = 0;
    while (!(session.cloth().mesh->panels[frozen].panel == back)) ++frozen;
    double most = 0.0;
    for (std::size_t i = 0; i < hung[frozen].size(); ++i) most = std::max(most, length(session.cloth().positions[frozen][i] - hung[frozen][i]));
    EXPECT((session.cloth().time > time && most == 0.0 && w.window.isWindowModified()), "it runs on from there, but for the frozen panel");
}

TEST_CASE("what is typed in a field is saved, without Enter; what cannot be taken stops the save", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-typed-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("typed.twintail");
    REQUIRE(w.window.saveAs(path));
    w.window.activateWindow();
    QCoreApplication::processEvents();
    // Typed by keys into the chest's field, and left there.
    const auto typeAndStay = [&](const QString& text) {
        QLineEdit* field = w.window.parameters()->field(w.parameter("chest"));
        field->setFocus();
        field->selectAll();
        for (const QChar c : text) {
            QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QApplication::sendEvent(field, &key);
        }
        REQUIRE((QApplication::focusWidget() == field && field->text() == text));
    };
    const auto inFile = [&] {
        std::variant<SavedScene, FileError> read = twintailor::open(path.toStdString());
        REQUIRE(std::holds_alternative<SavedScene>(read));
        const Document& doc = std::get<SavedScene>(read).document;
        return asNumber(doc.get(doc.parameters().front()).value);
    };
    REQUIRE(w.doc().get(w.doc().parameters().front()).name == "chest");

    typeAndStay("104");
    EXPECT(inFile() == 96.0, "typed, not yet taken");
    choose(w, "fileSave");
    EXPECT((inFile() == 104.0 && !w.window.isWindowModified()), "Save takes it first");

    typeAndStay("-5");
    choose(w, "fileSave");
    EXPECT((inFile() == 104.0 && w.window.helpLine().startsWith("Not changed")), "what is refused stops the save, with why");

    typeAndStay("108");
    Answering answer(QMessageBox::Cancel);
    EXPECT((!w.window.close() && answer.questions.size() == 1), "closing takes it too, and so asks about it");
    EXPECT((asNumber(w.doc().get(w.parameter("chest")).value) == 108.0), "the pattern has it");
}

TEST_CASE("names with any letters are saved and opened as they are; a save that fails leaves the old file", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-names-XXXXXX");
    REQUIRE(folder.isValid());
    const QString letters = QString::fromUtf8("zażółć 衣");
    REQUIRE(QDir(folder.path()).mkdir(letters));
    const QString path = folder.filePath(letters + "/" + letters + ".twintail");
    const QString cube = folder.filePath(letters + "/" + letters + ".obj");
    REQUIRE(QFile::copy(cubeFile(folder), cube));

    REQUIRE(w.window.loadAvatar(cube));
    w.waitForTheBody();
    EXPECT((w.window.outliner()->bodyShown() == letters + ".obj"), "the avatar is read, and named as its file is");
    REQUIRE(w.window.saveAs(path));
    EXPECT((QFile::exists(path) && QDir(folder.filePath(letters)).entryList(QDir::Files).size() == 2), "a file of exactly that name, and nothing else");
    w.window.openSample(0);
    REQUIRE(w.window.openFile(path));
    w.waitForTheBody();
    EXPECT((w.window.windowTitle().startsWith(letters) && w.window.outliner()->bodyShown() == letters + ".obj"), "and it opens");

    const auto bytes = [&] {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    };
    const QByteArray old = bytes();
    w.window.openSample(5);
    REQUIRE(QDir(folder.filePath(letters)).mkdir(letters + ".twintail.saving"));  // in the way of the new file
    EXPECT((!w.window.saveAs(path) && w.window.helpLine().contains("could not")), "the save fails, and says so");
    EXPECT((bytes() == old), "the old file is as it was");
    REQUIRE(QDir(folder.filePath(letters)).rmdir(letters + ".twintail.saving"));
    EXPECT((w.window.saveAs(path) && bytes() != old), "out of the way, it is written");
    EXPECT((QDir(folder.filePath(letters)).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size() == 2), "with nothing left beside it");
}

// ---- Recovery copies ----

TEST_CASE("unsaved work is copied, and what a run that stopped left is offered at the next start", "[qt]")
{
    application();
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-recovery-XXXXXX");
    REQUIRE(folder.isValid());
    const QString recovery = folder.filePath("recovery"), mine = folder.filePath("mine.twintail");
    const auto runs = [&] { return QDir(recovery).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(); };
    const auto copies = [&] {
        int found = 0;
        for (const QString& run : QDir(recovery).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) found += session::hasRecovery((recovery + "/" + run).toStdString());
        return found;
    };
    // A run that changes the saved pattern and stops without being closed.
    const auto stopUnsaved = [&](const QString& chest) {
        Window w;
        Answering nothing(QMessageBox::Cancel);
        w.window.keepRecoveryIn(recovery, 0.0);
        REQUIRE(w.window.saveAs(mine));
        w.window.tick();
        w.window.finishRecoveryCopy();
        EXPECT((copies() == 0), "saved work needs no copy");
        setChest(w, chest);
        w.window.shelf()->simulate()->setChecked(true);
        for (int i = 0; i < 3; ++i) w.window.tick();
        w.window.shelf()->simulate()->setChecked(false);
        w.window.tick();
        w.window.finishRecoveryCopy();
        w.window.tick();
        w.window.finishRecoveryCopy();
        EXPECT((copies() == 1), "unsaved work is copied");
    };

    stopUnsaved("104 cm");
    EXPECT((copies() == 1), "the copy stays when the program stops");
    {
        Window w;
        Answering later(QMessageBox::Cancel);
        w.window.keepRecoveryIn(recovery, 0.0);
        EXPECT((later.questions.size() == 1 && later.questions.front().contains("mine")), "the next start offers it, by name");
        EXPECT((w.window.windowTitle().startsWith("6. T-shirt body") && !w.window.isWindowModified()), "Not Now leaves the window as it was");
        EXPECT((w.window.close() && copies() == 1), "and keeps the copy for the start after");
    }
    {
        Window w;
        Answering recover(QMessageBox::Open);
        w.window.keepRecoveryIn(recovery, 0.0);
        EXPECT((recover.questions.size() == 1 && w.window.windowTitle().startsWith("mine[*]")), "opened, under its name");
        EXPECT((w.window.isWindowModified() && asNumber(w.doc().get(w.parameter("chest")).value) == 104.0), "with the work that was not saved");
        EXPECT((w.window.session().clothShown() && w.window.session().cloth().time > 0.0), "cloth and all");
        EXPECT((copies() == 1 && runs() == 1), "the copy is this run's now");
        choose(w, "fileSave");
        w.window.tick();
        EXPECT((recover.fileDialogs == 0 && !w.window.isWindowModified() && copies() == 0), "Save writes it to its own file, and the copy goes");
        EXPECT((w.window.close() && runs() == 0), "a closed run leaves nothing");
    }
    std::variant<SavedScene, FileError> saved = twintailor::open(mine.toStdString());
    REQUIRE(std::holds_alternative<SavedScene>(saved));
    EXPECT((std::get<SavedScene>(saved).state.drape.has_value()), "the recovered work is in the file");

    stopUnsaved("108 cm");
    {
        Window w;
        Answering drop(QMessageBox::Discard);
        w.window.keepRecoveryIn(recovery, 0.0);
        EXPECT((drop.questions.size() == 1 && copies() == 0 && asNumber(w.doc().get(w.parameter("chest")).value) == 96.0), "Delete throws it away");

        // While a run is going its copy is its own.
        setChest(w, "100 cm");
        w.window.tick();
        w.window.finishRecoveryCopy();
        REQUIRE(copies() == 1);
        Window other;
        other.window.keepRecoveryIn(recovery, 0.0);
        EXPECT((drop.questions.size() == 1), "another window is not offered the copy of one that is running");

        // Undone back to what is saved, there is nothing to copy.
        w.window.shelf()->undo()->trigger();
        w.window.tick();
        EXPECT((copies() == 0), "and undone, the copy goes");
        setChest(w, "100 cm");
        w.window.tick();
        w.window.finishRecoveryCopy();
        REQUIRE(copies() == 1);
        EXPECT((w.window.close() && copies() == 0), "closed with Don't Save, it is thrown away");
        w.window.tick();
        w.window.finishRecoveryCopy();
        EXPECT((copies() == 0), "and not written again");
    }
    {
        Window w;
        w.window.keepRecoveryIn(recovery, 3600.0);
        setChest(w, "100 cm");
        w.window.tick();
        w.window.finishRecoveryCopy();
        EXPECT((copies() == 1), "a change is copied at once, however long a failed copy would wait");
    }
}

TEST_CASE("a run that ends in an error leaves a crash report with what was done, offered at the next start", "[qt]")
{
    application();
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-report-XXXXXX");
    REQUIRE(folder.isValid());
    const QString recovery = folder.filePath("recovery");
    const QDir reports(folder.filePath("crash-reports"));
    const auto made = [&] { return reports.entryList(QDir::Dirs | QDir::NoDotAndDotDot); };
    const auto logOf = [&](const QString& report) {
        QFile file(reports.filePath(report + "/actions.log"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    };
    {
        Window w;  // closed as it should be
        w.window.keepRecoveryIn(recovery, 0.0);
        EXPECT((w.window.close()), "closed");
    }
    {
        Window w;  // stopped with unsaved work, but as it should
        w.window.keepRecoveryIn(recovery, 0.0);
        setChest(w, "104 cm");
        w.window.tick();
        w.window.finishRecoveryCopy();
    }
    {
        Window w;
        Answering drop(QMessageBox::Discard);
        w.window.keepRecoveryIn(recovery, 0.0);
        EXPECT((made().isEmpty() && drop.questions.size() == 1 && !drop.questions.front().contains("crash report")), "no report for a run that ended as it should");
        w.window.openSample(5);
        setChest(w, "104 cm");
        w.window.shelf()->simulate()->setChecked(true);
        w.window.tick();
        w.window.cannotGoOn("something broke");
    }
    {
        Window w;
        Answering later(QMessageBox::Cancel);
        w.window.keepRecoveryIn(recovery, 0.0);
        REQUIRE(made().size() == 1);
        const QString log = logOf(made().front());
        INFO(log.toStdString());
        EXPECT((log.startsWith("Twintailor ") && log.contains("Open sample 6") && log.contains("SetField") && log.contains("Play") &&
                log.contains("Error: something broke")),
               "the version, what was done and the error");
        EXPECT((QFile::exists(reports.filePath(made().front() + "/pattern.twintail"))), "with the pattern");
        EXPECT((later.questions.size() == 1 && later.questions.front().contains("crash report") && later.questions.front().contains(made().front())),
               "the question about the copy says where the report is");
        EXPECT((w.window.helpLine().contains("did not close properly") && w.window.helpLine().contains(made().front())), "and so does the help line");
        EXPECT((w.window.close()), "closed");
    }
    {
        Window w;
        Answering drop(QMessageBox::Discard);
        w.window.keepRecoveryIn(recovery, 0.0);
        EXPECT((made().size() == 1 && drop.questions.size() == 1 && !drop.questions.front().contains("crash report")), "said and made once");
        EXPECT((!w.window.helpLine().contains("crash report")), "and the help line leaves it");
    }
}

// ---- Errors nothing else handles ----

namespace {

// As the program has it (main.cpp): errors out of events go to the window.
struct Handling {
    explicit Handling(MainWindow& window) {
        application().handleErrors([&window](const QString& error) { window.cannotGoOn(error); });
    }
    ~Handling() { application().handleErrors({}); }
    Handling(const Handling&) = delete;
    Handling& operator=(const Handling&) = delete;
};

// Types into a field and presses Enter, as a key event that Qt delivers.
void enter(QLineEdit* field, const QString& text) {
    REQUIRE(field != nullptr);
    field->setFocus();
    field->setText(text);
    QKeyEvent pressed(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(field, &pressed);
}

// From now on every change typed into `field` ends in an error, after the window has made it.
void breakAfter(QLineEdit* field) {
    QObject::connect(field, &QLineEdit::editingFinished, field, [] { throw std::runtime_error("something broke"); });
}

} // namespace

TEST_CASE("an error in a change nothing handles copies the unsaved work at once, says so, and the window is to end", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-broken-XXXXXX");
    REQUIRE(folder.isValid());
    const QString recovery = folder.filePath("recovery");
    w.window.keepRecoveryIn(recovery, 3600.0);  // no frame passes here, so only the error makes a copy
    int ended = 0;
    QObject::connect(&w.window, &MainWindow::mustEnd, [&] { ++ended; });
    const Handling handling(w.window);
    const Param chest = w.parameter("chest");
    QLineEdit* field = w.window.parameters()->field(chest);
    breakAfter(field);

    enter(field, "104 cm");
    REQUIRE(asNumber(w.doc().get(chest).value) == 104.0);
    const QStringList runs = QDir(recovery).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    REQUIRE(runs.size() == 1);
    const std::expected<session::Recovery, std::string> copy = session::readRecovery((recovery + "/" + runs.front()).toStdString());
    REQUIRE(copy);
    EXPECT((asNumber(copy->scene.document.get(chest).value) == 104.0), "the copy is there at once, with the change just made");
    EXPECT((ended == 0), "and nothing ends before it is said");

    QCoreApplication::processEvents();
    const QList<QMessageBox*> boxes = w.window.findChildren<QMessageBox*>("cannotGoOn");
    REQUIRE(boxes.size() == 1);
    const QString said = boxes.front()->text();
    INFO(said.toStdString());
    EXPECT((boxes.front()->isVisible() && said.contains("something broke") && said.contains("was copied") && said.contains("Start Twintailor again")),
           "what happened, and where the work is");

    enter(field, "108 cm");  // another error: told once is enough
    QCoreApplication::processEvents();
    EXPECT((w.window.findChildren<QMessageBox*>("cannotGoOn").size() == 1), "only the first is told");
    EXPECT((!w.window.close() && ended == 0), "the window does not close as if all were well");
    boxes.front()->button(QMessageBox::Close)->click();
    EXPECT((ended == 1), "closing the message ends the program");
    EXPECT((session::hasRecovery((recovery + "/" + runs.front()).toStdString())), "and the copy stays for the next start");
}

TEST_CASE("an error with nothing unsaved, or with no copy to be had, says which", "[qt]")
{
    application();
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-broken-XXXXXX");
    REQUIRE(folder.isValid());
    const auto said = [](Window& w) {
        QCoreApplication::processEvents();
        const auto* box = w.window.findChild<QMessageBox*>("cannotGoOn");
        REQUIRE(box != nullptr);
        return box->text();
    };
    {
        Window w;
        w.window.keepRecoveryIn(folder.filePath("saved"), 3600.0);
        w.window.cannotGoOn("something broke");
        EXPECT((said(w).contains("Nothing was unsaved")), "nothing to copy");
    }
    {
        Window w;
        setChest(w, "100 cm");
        w.window.cannotGoOn("something broke");
        EXPECT((said(w).contains("No recovery copy") && said(w).contains("lost")), "nowhere to copy to");
    }
    {
        Window w;
        const QString recovery = folder.filePath("blocked");
        w.window.keepRecoveryIn(recovery, 3600.0);
        setChest(w, "100 cm");
        const QStringList runs = QDir(recovery).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        REQUIRE(runs.size() == 1);
        REQUIRE(QDir(recovery + "/" + runs.front()).mkdir("pattern-1.twintail.saving"));  // in the way of the copy
        w.window.cannotGoOn("something broke");
        EXPECT((said(w).contains("could not be copied") && said(w).contains("earlier copy")), "the copy failed, and why");
    }
}

TEST_CASE("every sample meshes; random clicks, drags and keys on both views and the shelf find no crash", "[qt]")
{
    Window w;
    w.window.shelf()->mesh()->setChecked(true);
    std::mt19937 random(12345);
    const auto below = [&](int n) { return static_cast<int>(random() % static_cast<unsigned>(n)); };
    const int rounds = qEnvironmentVariableIsSet("FUZZ") ? qEnvironmentVariableIntValue("FUZZ") : 60;
    const std::vector<QAction*> tools = [&] {
        std::vector<QAction*> found;
        for (const Tool tool : {Tool::Select, Tool::Move, Tool::Rotate, Tool::Scale, Tool::Rectangle, Tool::Circle, Tool::Ring, Tool::Point, Tool::Curve,
                                Tool::Line, Tool::Length, Tool::Mirror, Tool::Cut, Tool::Split, Tool::Sew})
            found.push_back(w.window.shelf()->action(tool));
        return found;
    }();
    for (std::size_t i = 0; i < session::samples().size(); ++i) {
        w.window.openSample(i);
        w.window.tick();
        std::size_t problems = 0;
        for (const sim::PanelMesh& mesh : w.window.session().mesh()->panels) problems += !mesh.problem.empty();
        EXPECT((problems == 0 && !w.window.session().mesh()->panels.empty()), std::string(session::samples()[i].name) + ": every cloth panel meshes");

        w.window.shelf()->simulate()->setChecked(true);  // the cloth moves while everything below happens
        SpaceView& space = *w.window.space();
        TableView* table = w.window.table();
        for (int round = 0; round < rounds; ++round) {
            if (round % 7 == 0) tools[static_cast<std::size_t>(below(static_cast<int>(tools.size())))]->trigger();
            if (round % 2 == 0) {
                const QPointF at(below(std::max(table->width(), 1)), below(std::max(table->height(), 1)));
                send(table, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, round % 4 == 0 ? Qt::ShiftModifier : Qt::NoModifier);
                send(table, QEvent::MouseMove, at + QPointF(below(60) - 30, below(60) - 30), Qt::NoButton, Qt::LeftButton, {});
                send(table, QEvent::MouseButtonRelease, at + QPointF(below(60) - 30, below(60) - 30), Qt::LeftButton, Qt::NoButton, {});
                if (round % 5 == 0) send(table, QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton, {});
            } else {
                const QPointF at(below(std::max(space.width(), 1)), below(std::max(space.height(), 1)));
                const Qt::KeyboardModifiers keys = round % 3 == 0 ? Qt::AltModifier : Qt::NoModifier;
                space.press(at, round % 5 == 0 ? Qt::MiddleButton : Qt::LeftButton, keys);
                space.moveTo(at + QPointF(below(80) - 40, below(80) - 40), Qt::LeftButton, keys);
                space.release();
            }
            if (round % 11 == 0) {
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(table, &escape);
            }
            if (round % 13 == 0) w.window.shelf()->undo()->trigger();
            if (round % 17 == 0) w.window.shelf()->redo()->trigger();
            w.window.tick();
        }
        w.window.shelf()->simulate()->setChecked(false);
        INFO(session::samples()[i].name << ": " << w.doc().panels().size() << " panels, " << w.window.session().editor().shapes().errors.size() << " problems");
        SUCCEED("survived the random input");
    }
}

TEST_CASE("the layers scroll on their own, apart from the rest of the Attribute Editor", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    AttributeEditor& editor = *w.window.attributes();
    auto* list = editor.findChild<QScrollArea*>("layerScroll");
    REQUIRE(list != nullptr);
    const OpId rect = first<Rect>(w.doc(), front);
    QWidget* row = editor.findChild<QWidget*>(QString("layer%1").arg(OpId(rect).value));
    REQUIRE(row != nullptr);
    EXPECT((list->widget()->isAncestorOf(row)), "the layer rows are inside the list's own scroll area");
    EXPECT((list->minimumHeight() > 0), "which keeps some height of its own");
}

TEST_CASE("the choice of what a length keeps can be changed through the list's own keys, on the T-shirt, the cloth running", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    const OpRef<Rect> body(first<Rect>(w.doc(), front));
    // As the Length tool does it: a click on the shoulder.
    emit w.window.table()->gestured(session::EditLength{front, body.top(), true});
    REQUIRE(std::holds_alternative<SetLength>(w.doc().get(w.doc().operations(front).back())));
    const OpId set = w.doc().operations(front).back();
    w.window.attributes()->openLayer(set);
    w.window.shelf()->simulate()->setChecked(true);
    w.window.tick();
    // The keys of a closed list choose the next or the previous entry, and report it the way a mouse pick does.
    // The list is made again by that report, so the one that is asked is looked up every time.
    for (const int key : {Qt::Key_Down, Qt::Key_Down, Qt::Key_Up, Qt::Key_Up, Qt::Key_Down}) {
        QComboBox* keeps = w.window.attributes()->findChild<QComboBox*>("keeps");
        REQUIRE(keeps != nullptr);
        const int was = static_cast<int>(std::get<SetLength>(w.doc().get(set)).keeping);
        QKeyEvent pressed(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(keeps, &pressed);
        w.window.tick();
        EXPECT((static_cast<int>(std::get<SetLength>(w.doc().get(set)).keeping) == was + (key == Qt::Key_Down ? 1 : -1)), "the key chose the next or the previous entry");
    }
}

TEST_CASE("the list in the length bar is opened and an entry chosen with the mouse, as a person does", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();
    const auto layers = [&] { return w.doc().operations(plain).size(); };
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(table->lengthEditor() != nullptr);
    QComboBox* scope = table->lengthCard()->findChild<QComboBox*>("lengthScope");
    REQUIRE(scope != nullptr);
    // The entry is picked in the list's own window: a press and a release on it.
    const auto pick = [&](int row) {
        // A click on the list gives it the focus, which the field loses, and opens it.
        scope->setFocus(Qt::MouseFocusReason);
        const QPointF on = scope->rect().center();
        QMouseEvent open(QEvent::MouseButtonPress, on, scope->mapToGlobal(on), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(scope, &open);
        QApplication::processEvents();
        EXPECT((table->lengthEditor() != nullptr), "opening the list does not close the bar");
        // Qt ignores a release on a list that has only just opened, for about a double-click's time.
        std::this_thread::sleep_for(std::chrono::milliseconds(QApplication::doubleClickInterval() + 100));
        QApplication::processEvents();
        QAbstractItemView* view = scope->view();
        REQUIRE(view != nullptr);
        const QPoint at = view->visualRect(view->model()->index(row, 0)).center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), view->viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), view->viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::NoButton, {});
        QApplication::sendEvent(view->viewport(), &release);
        QApplication::processEvents();
    };
    pick(0);
    EXPECT((table->lengthEditor() != nullptr && scope->currentIndex() == 0 && layers() == 2), "choosing 'Edge only' in the list gives the edge a length of its own, and the bar stays");
    pick(1);
    EXPECT((table->lengthEditor() != nullptr && scope->currentIndex() == 1 && layers() == 1), "choosing the shape's setting in the list gives it up again, and the bar stays");
    EXPECT((w.window.selectedPanels() == std::vector<PanelId>{plain}), "and the panel is still chosen");
}

TEST_CASE("the length in the bar is typed or dragged sideways, as in the Attribute Editor", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const OpRef<Rect> rect(first<Rect>(w.doc(), plain));
    TableView* table = w.table();
    const auto bottom = [&] { return w.window.session().editor().shapes()[plain].length(rect.bottom()); };
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    REQUIRE(table->lengthEditor() != nullptr);
    QLineEdit* field = table->lengthEditor();
    EXPECT((field->text() == "30 cm"), "the number is shown with its unit, as in the Attribute Editor");

    // A press on the number and a pull sideways, as a hand does it: the number follows, and is shown at once.
    const auto pull = [&](int pixels, Qt::KeyboardModifiers keys = {}) {
        const QPointF from = QPointF(field->width() / 2.0, field->height() / 2.0);
        QMouseEvent press(QEvent::MouseButtonPress, from, field->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, keys);
        QApplication::sendEvent(field, &press);
        for (int i = 1; i <= 5; ++i) {
            const QPointF at = from + QPointF(pixels * i / 5.0, 0);
            QMouseEvent move(QEvent::MouseMove, at, field->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, keys);
            QApplication::sendEvent(field, &move);
        }
        const QPointF to = from + QPointF(pixels, 0);
        QMouseEvent release(QEvent::MouseButtonRelease, to, field->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, keys);
        QApplication::sendEvent(field, &release);
    };
    pull(40);
    EXPECT_NEAR((bottom()), (34.0), "pulling right makes it more: a tenth of a centimetre a pixel");
    EXPECT((field->text() == "34 cm" && table->lengthEditor() != nullptr && w.window.selectedPanels() == std::vector<PanelId>{plain}), "the field shows it, the bar stays, the panel stays chosen");
    pull(-30);
    EXPECT_NEAR((bottom()), (31.0), "pulling left makes it less");
    pull(5, Qt::ShiftModifier);
    EXPECT_NEAR((bottom()), (36.0), "Shift pulls ten times as far");
    pull(10, Qt::AltModifier);
    EXPECT_NEAR((bottom()), (36.1), "Alt pulls a tenth as far");

    // What is typed is where a pull starts from.
    field->setText("20");
    EXPECT_NEAR((bottom()), (20.0), "typing shows at once");
    pull(10);
    EXPECT_NEAR((bottom()), (21.0), "a pull goes on from what was typed");

    // A plain click only places the cursor.
    const double before = bottom();
    const QPointF at(field->width() / 2.0, field->height() / 2.0);
    QMouseEvent press(QEvent::MouseButtonPress, at, field->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, {});
    QApplication::sendEvent(field, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, field->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, {});
    QApplication::sendEvent(field, &release);
    EXPECT_NEAR((bottom()), (before), "a click without a pull changes nothing");

    // Only a number as a person writes it is pulled, not what strtod would take.
    for (const char* odd : {"nan", "0x10", "1e3"}) {
        field->setText(odd);
        const QString oddText = field->text();
        pull(30);
        EXPECT((field->text() == oddText), std::string("'") + odd + "' is not pulled");
    }

    // A formula is not pulled.
    field->setText("= width");
    const QString formula = field->text();
    pull(30);
    EXPECT((field->text() == formula), "a formula stays as it is when pulled");

    // Escape takes all of it back, pulls too.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(field, &escape);
    EXPECT((table->lengthEditor() == nullptr && std::abs(bottom() - 30.0) < 1e-9 && !w.window.shelf()->undo()->isEnabled()), "and Escape takes it all back");
}

TEST_CASE("carrying a panel in 3D does not paint the table again, for nothing on it moved; moving it on the table does", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.tick();
    QCoreApplication::processEvents();  // the table painted as it is now
    int paints = 0;
    QObject::connect(w.window.table(), &TableView::painted, [&](double) { ++paints; });
    const double xBefore = w.doc().get(front).arrangement.at.x;
    for (int i = 1; i <= 5; ++i) {
        emit w.window.space()->gestured(session::MoveInSpace{front, {static_cast<double>(i), 0.0, 0.0}, i == 1 ? session::DragPhase::Begin : session::DragPhase::Continue});
        w.window.tick();
        QCoreApplication::processEvents();
    }
    emit w.window.space()->gestured(session::MoveInSpace{front, {5.0, 0.0, 0.0}, session::DragPhase::End});
    w.window.tick();
    QCoreApplication::processEvents();
    EXPECT_NEAR(w.doc().get(front).arrangement.at.x, xBefore + 5.0, "the panel was carried 5 cm in space");
    EXPECT((paints == 0), "and the table, where nothing moved, was not painted again: at 4K that is 3 ms of every frame of the carry");

    emit w.window.table()->gestured(session::MovePanel{front, 5.0, 0.0, session::DragPhase::Begin});
    emit w.window.table()->gestured(session::MovePanel{front, 5.0, 0.0, session::DragPhase::End});
    QCoreApplication::processEvents();
    EXPECT((paints > 0), "a panel moved on the table is painted where it is now");
}

TEST_CASE("the window ticks once for every picture its screen shows, after it was stopped too", "[qt]")
{
    // Off screen the screen says 60 Hz, which was the fixed tick before: this
    // checks that the window follows its screen; the rule for other rates is
    // checked in FrameTimesTests, and 240 Hz on the owner's screen by hand.
    Window w;
    const int every = session::FrameTimes::tickMilliseconds(w.window.screen()->refreshRate());
    EXPECT((w.window.tickMilliseconds() == every), "the timer ticks at the screen's rate");
    w.window.setTicking(false);
    w.window.setTicking(true);
    EXPECT((w.window.tickMilliseconds() == every), "and again when it is started again");
}

TEST_CASE("View > Frame times shows how long the views' frames, a frame's work and the cloth's step take; it is off at first", "[qt]")
{
    Window w;
    auto* line = w.window.findChild<QLabel*>("frameTimes");
    auto* frameTimes = w.window.findChild<QAction*>("showFrameTimes");
    REQUIRE((line != nullptr && frameTimes != nullptr));
    EXPECT((line->isHidden() && !frameTimes->isChecked()), "it is off at first, for users have no need of it");

    // The table drawn again and again, as during a drag; a panel dragged on it; the cloth running.
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 6; ++i) {
        w.window.table()->grab();  // paints it
        w.window.tick();
    }
    const PanelId front = w.panel("front");
    const std::optional<Bounds> box = bounds(w.window.session().editor().shapes()[front]);
    REQUIRE(box.has_value());
    const Panel& placed = w.doc().get(front);
    const QPointF middle = w.window.table()->toScreen({placed.at.x + (box->min.x + box->max.x) / 2, placed.at.y + box->min.y + (box->max.y - box->min.y) / 4});
    drag(w.window.table(), middle, middle + QPointF(30, 0));
    frameTimes->trigger();
    const std::string shown = line->text().toStdString();
    INFO(shown);
    EXPECT((!line->isHidden()), "switched on, the line is in the status bar");
    EXPECT((shown.find("table still") == std::string::npos && shown.find("  ·  table ") != std::string::npos && shown.find(" ms a frame") != std::string::npos),
           "how far apart the table's pictures are");
    EXPECT((shown.find("tick ") != std::string::npos && shown.find("table painting ") != std::string::npos), "what a frame's work takes");
    EXPECT((shown.find("change ") != std::string::npos), "a drag's change of the document too");
    EXPECT((shown.find("cloth step ") != std::string::npos && shown.find(" particles, ") != std::string::npos), "and the cloth's step and size");
    frameTimes->trigger();
    EXPECT((line->isHidden()), "switched off, it is gone");
    w.window.shelf()->simulate()->setChecked(false);
}

TEST_CASE("the status bar's help line gives way to the frame times, and both to the pointer's place; nothing overlaps", "[qt]")
{
    Window w;
    w.window.findChild<QAction*>("showFrameTimes")->trigger();
    auto* help = w.window.findChild<QLabel*>("helpLine");
    auto* times = w.window.findChild<QLabel*>("frameTimes");
    auto* pointer = w.window.findChild<QLabel*>("pointerAt");
    REQUIRE((help != nullptr && times != nullptr && pointer != nullptr));
    pointer->setText("x -248.72 cm   y 56.09 cm");
    const auto at = [&](int width) {
        w.window.resize(width, 800);
        w.window.statusBar()->layout()->activate();
        QApplication::processEvents();
        INFO("width " << width << ": help " << help->geometry().left() << ".." << help->geometry().right() << ", times " << times->geometry().left()
                      << ".." << times->geometry().right() << ", pointer " << pointer->geometry().left() << ".." << pointer->geometry().right());
        EXPECT((help->geometry().right() < times->geometry().left() && times->geometry().right() < pointer->geometry().left()), "the three stand side by side");
        EXPECT((pointer->width() >= pointer->sizeHint().width()), "the pointer's place is shown whole");
    };
    at(2400);  // as wide as the owner's window
    EXPECT((times->width() >= times->sizeHint().width()), "with room, the frame times are shown whole");
    EXPECT((help->fontMetrics().horizontalAdvance(help->text()) > help->width()), "and the Select tool's hint is what is cut");
    at(1100);
    EXPECT((times->toolTip() == times->text() && help->toolTip() == help->text()), "what is cut is whole in its tooltip");
    w.window.findChild<QAction*>("showFrameTimes")->trigger();
}

// ---- Export ----

namespace {

std::size_t trianglesOf(const sim::ClothMesh& mesh, const std::string& without = {}, const Document* doc = nullptr) {
    std::size_t triangles = 0;
    for (const sim::PanelMesh& panel : mesh.panels)
        if (!doc || doc->get(panel.panel).name != without) triangles += panel.triangles.size();
    return triangles;
}

} // namespace

// ---- The owner's notes: lists and names in the docks ----

namespace {

// A key pressed on a widget, as Qt delivers it.
void pressKey(QWidget* widget, int key, const QString& text = {}) {
    QKeyEvent pressed(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(widget, &pressed);
}

// Typed letter by letter over what is selected.
void typeKeys(QWidget* widget, const QString& text) {
    for (const QChar letter : text) pressKey(widget, 0, QString(letter));
}

// The wheel turned one notch towards the user with the pointer over a widget, as
// when a dock is scrolled down. It goes in where the system's events do: only
// such a one does Qt hand on from a widget that does not take it, and give the
// focus to a widget that asks for it by the wheel.
void wheelDown(QWidget* widget) {
    QWidget* top = widget->window();
    const QPoint at = widget->mapTo(top, widget->rect().center());
    REQUIRE(top->childAt(at) == widget);  // seen, and under the pointer
    qt_handleWheelEvent(top->windowHandle(), at, top->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoModifier, Qt::NoScrollPhase);
    QApplication::processEvents();
}

// A frame, and the layouts it asked for done, one pass of events after another.
void settle(Window& w) {
    w.window.tick();
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

// Two panels with every list the Attribute Editor has: an instance's source, a cut's side, what a set length keeps.
struct Lists : Window {
    PanelId cut, instance;
    OpId set;
    Lists() {
        Document doc;
        cut = doc.add(Panel{.name = "cut"});
        const OpRef<Rect> rect = doc.add(cut, Rect{.width = 30_cm, .height = 40_cm});
        // Under the cut and the set length, layers enough that their list has to be scrolled.
        for (int i = 0; i < 10; ++i) doc.add(cut, Scale{.x = 1.0, .y = 1.0});
        const OpRef<LineBetween> line = doc.add(cut, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.5}});
        doc.add(cut, Cut{.along = line.line()});
        set = doc.add(cut, SetLength{.edge = rect.bottom(), .length = 25_cm});
        instance = doc.add(Panel{.name = "instance", .at = {50, 0}});
        doc.add(instance, Instance{.source = cut});
        window.open(std::move(doc), "lists");
        window.activateWindow();  // as on a desktop: only there can a part of it have the focus
        window.tick();
        QApplication::processEvents();
        REQUIRE(window.isActiveWindow());
    }
    // A list of the panel shown now.
    QComboBox* list(const char* name) {
        QComboBox* found = window.attributes()->findChild<QComboBox*>(name);
        REQUIRE(found != nullptr);
        return found;
    }
};

} // namespace

TEST_CASE("the wheel over a list of the Attribute Editor scrolls the dock and changes nothing, until the list is clicked", "[qt]")
{
    Lists w;
    for (const char* name : {"keeps", "keep", "panelReference"}) {
        INFO(name);
        w.window.select(std::string_view(name) == "panelReference" ? w.instance : w.cut);
        settle(w);
        QComboBox* list = w.list(name);
        EXPECT((list->focusPolicy() != Qt::WheelFocus), "the wheel does not give it the focus");
        QScrollArea* around = w.window.attributes()->findChild<QScrollArea*>("layerScroll");
        // An instance has one short card: nothing to scroll, but still nothing to change.
        if (std::string_view(name) != "panelReference") REQUIRE(around->verticalScrollBar()->maximum() > 0);
        around->verticalScrollBar()->setValue(0);
        w.window.table()->setFocus();
        const std::uint64_t version = w.window.session().editor().version();
        const int chosen = list->currentIndex();
        // Over a list that could go further down: the wheel would choose the next entry.
        if (chosen + 1 >= list->count()) list->insertItem(list->count(), "one more");
        wheelDown(list);
        w.window.tick();
        EXPECT((w.window.session().editor().version() == version && w.list(name)->currentIndex() == chosen), "nothing is changed");
        if (std::string_view(name) != "panelReference") EXPECT((around->verticalScrollBar()->value() > 0), "and the layers scroll on");
    }

    // Clicked, it has the focus and takes the wheel, as any list.
    w.window.select(w.cut);
    settle(w);
    w.window.attributes()->findChild<QScrollArea*>("layerScroll")->verticalScrollBar()->setValue(0);
    QComboBox* keeps = w.list("keeps");
    keeps->setFocus(Qt::MouseFocusReason);
    REQUIRE(keeps->hasFocus());
    wheelDown(keeps);
    w.window.tick();
    EXPECT((std::get<SetLength>(w.doc().get(w.set)).keeping == SetLength::Keeping::Start), "the next entry is chosen");
}

TEST_CASE("the wheel over the length bar changes nothing, not over its list and not over its words", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    send(table, QEvent::MouseButtonDblClick, table->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2}), Qt::LeftButton, Qt::LeftButton, {});
    QComboBox* scope = table->lengthCard()->findChild<QComboBox*>("lengthScope");
    REQUIRE((scope != nullptr && scope->isVisible()));
    scope->setCurrentIndex(0);
    settle(w);  // the table has its place and its zoom
    const std::uint64_t version = w.window.session().editor().version();
    const double zoom = table->scale();
    wheelDown(scope);
    w.window.tick();
    EXPECT((scope->currentIndex() == 0 && w.window.session().editor().version() == version), "nothing is chosen and nothing changed");
    EXPECT((table->scale() == zoom), "nor is the table zoomed under the bar");
    wheelDown(table->lengthCard()->findChild<QLabel*>("lengthCaption"));
    EXPECT((table->scale() == zoom), "nor over the rest of the bar");
    EXPECT((table->lengthEditor() != nullptr), "which stays");
}

TEST_CASE("no card of the Attribute Editor is wider than the dock; what a length keeps is said in few words", "[qt]")
{
    Window w;
    choose(w, "fileSample6");
    w.waitForTheBody();
    AttributeEditor& editor = *w.window.attributes();
    // A name as long as a person might give: the instances' list of sources holds it.
    const PanelId front = w.panel("right front");
    emit w.window.outliner()->renamed(front, "right front of the long summer trousers");
    std::size_t setLengths = 0;
    for (PanelId panel : w.doc().panels()) {
        w.window.select(panel);
        for (OpId op : w.doc().operations(panel)) editor.openLayer(op);
        settle(w);
        QScrollArea* layers = editor.findChild<QScrollArea*>("layerScroll");
        INFO(w.doc().get(panel).name << ": the layers want " << layers->widget()->minimumSizeHint().width() << " px, the dock shows " << layers->viewport()->width());
        EXPECT((layers->widget()->minimumSizeHint().width() <= layers->viewport()->width()), "every card fits in the dock");
        EXPECT((layers->horizontalScrollBar()->maximum() == 0), "so nothing is scrolled to sideways");
        for (QComboBox* list : layers->findChildren<QComboBox*>("keeps")) {
            ++setLengths;
            REQUIRE(list->count() == 3);
            EXPECT((list->itemText(0) == "both ends move evenly" && list->itemText(1).endsWith(" stays") && !list->itemText(1).contains("Rectangle")),
                   "the corner without the layer it comes from");
            EXPECT((list->itemData(1, Qt::ToolTipRole).toString().contains(" of ")), "which the tooltip says");
        }
        // Asked of the list that opens, not by opening it: each window opened off screen is one more thing to
        // close, and Qt's platform without a screen crashed as the program ended after many of them.
        for (QComboBox* list : layers->findChildren<QComboBox*>())
            EXPECT((list->view()->minimumWidth() >= list->view()->sizeHintForColumn(0)), "opened, the list shows its entries whole");
    }
    EXPECT((setLengths >= 4), "the trousers' set lengths were among them");
}

TEST_CASE("Enter in a parameter's name renames it and leaves the box; Escape keeps the old name and leaves it", "[qt]")
{
    Window w;
    w.window.activateWindow();  // as on a desktop: only there can a part of it have the focus
    QApplication::processEvents();
    REQUIRE(w.window.isActiveWindow());
    const Param chest = w.parameter("chest");
    QLineEdit* name = w.window.parameters()->nameField(chest);
    REQUIRE(name != nullptr);
    const auto begin = [&](const QString& typed) {
        name->setFocus(Qt::MouseFocusReason);
        name->selectAll();
        typeKeys(name, typed);
        REQUIRE((name->hasFocus() && name->text() == typed));
    };

    begin("bust");
    pressKey(name, Qt::Key_Return);
    w.window.tick();
    EXPECT((w.doc().get(chest).name == "bust"), "Enter renames");
    EXPECT((!name->hasFocus() && name->text() == "bust"), "and leaves the box");

    begin("waist2");
    pressKey(name, Qt::Key_Escape);
    w.window.tick();
    EXPECT((w.doc().get(chest).name == "bust" && name->text() == "bust"), "Escape keeps the old name");
    EXPECT((!name->hasFocus()), "and leaves the box too");

    begin("2 wrong");
    pressKey(name, Qt::Key_Enter);
    w.window.tick();
    EXPECT((w.doc().get(chest).name == "bust" && name->text() == "bust" && !name->hasFocus()), "a name that is refused leaves the old one, and the box");
    EXPECT((w.window.helpLine().contains("letters")), "with why");
}

TEST_CASE("Enter in the Outliner's rename box renames and closes it; Escape closes it with the old name", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    Outliner& outliner = *w.window.outliner();
    const auto begin = [&](const QString& typed) {
        QTreeWidgetItem* row = nullptr;
        for (QTreeWidgetItemIterator at(&outliner); *at; ++at)
            if ((*at)->text(0) == QString::fromStdString(w.doc().get(front).name)) row = *at;
        REQUIRE(row != nullptr);
        outliner.setCurrentItem(row);
        w.window.tick();
        emit outliner.itemDoubleClicked(outliner.currentItem(), 0);  // what a double click on the name does
        auto* box = outliner.viewport()->findChild<QLineEdit*>();
        REQUIRE(box != nullptr);
        box->selectAll();
        typeKeys(box, typed);
        return QPointer<QLineEdit>(box);
    };
    const auto settle = [&] {
        for (int i = 0; i < 3; ++i) {
            QApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        w.window.tick();
    };

    QPointer<QLineEdit> box = begin("bodice");
    pressKey(box, Qt::Key_Return);
    settle();
    EXPECT((w.doc().get(front).name == "bodice"), "Enter renames");
    EXPECT((box.isNull()), "and the box is gone");

    box = begin("skirt");
    pressKey(box, Qt::Key_Escape);
    settle();
    EXPECT((w.doc().get(front).name == "bodice" && box.isNull()), "Escape keeps the old name, and the box is gone");
}

TEST_CASE("File > Export OBJ writes the garment, says how many panels went where, and why when it cannot", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-export-XXXXXX");
    REQUIRE(folder.isValid());
    auto* item = w.window.findChild<QAction*>("fileExportObj");
    REQUIRE(item != nullptr);
    EXPECT((item->isEnabled() && item->shortcut() == QKeySequence("Ctrl+E")), "in the File menu, with Ctrl+E");
    {
        Answering answer(QMessageBox::Cancel);
        item->trigger();
        EXPECT((answer.fileDialogs == 1 && answer.questions.empty()), "it asks where to; cancelled, nothing else happens");
    }

    const QString path = folder.filePath(QString::fromUtf8("koszulka 衣.obj"));
    REQUIRE(w.window.exportObj(path));
    const sim::LoadedSurface read = sim::loadObj(path.toStdString());
    REQUIRE(read.problem.empty());
    EXPECT(read.surface.triangles.size() == trianglesOf(w.window.session().shownMesh()), "every triangle of the garment");
    EXPECT((w.window.helpLine().contains("2 panels") && w.window.helpLine().contains(QDir::toNativeSeparators(path))), "and says so");
    EXPECT(!w.window.isWindowModified(), "exporting leaves the pattern saved");

    const PanelId hidden = w.doc().panels().front();
    emit w.window.outliner()->panelShownChanged(hidden, false);
    REQUIRE(w.window.exportObj(path));
    EXPECT(sim::loadObj(path.toStdString()).surface.triangles.size() ==
               trianglesOf(w.window.session().shownMesh(), w.doc().get(hidden).name, &w.doc()),
           "a hidden panel is not written");
    EXPECT((w.window.helpLine().contains("1 panel ") && w.window.helpLine().contains("1 hidden")), "and that is said");

    EXPECT(!w.window.exportObj(folder.filePath("no such folder/x.obj")), "a place that cannot be written");
    EXPECT(w.window.helpLine().startsWith("Could not write"), "says why");

    emit w.window.outliner()->panelShownChanged(w.doc().panels().back(), false);
    const auto bytes = [&] {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    };
    const QByteArray old = bytes();
    EXPECT((!w.window.exportObj(path) && w.window.helpLine().contains("hidden") && bytes() == old), "with nothing to write, the file is left as it was");
}

TEST_CASE("Export OBJ replaces a file that another program holds for a moment; one that stays held is left, and why is said", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-held-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("shirt.obj");
    const auto bytes = [&] {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    };
    REQUIRE(w.window.exportObj(path));
    const QByteArray whole = bytes();
    const auto putOldInItsPlace = [&] {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write("old") == 3);
    };

    putOldInItsPlace();
    std::ifstream held(filePath(path.toStdString()), std::ios::binary);
    REQUIRE(held.is_open());
    std::thread other([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        held.close();
    });
    const bool exported = w.window.exportObj(path);
    other.join();
    EXPECT(exported, "it is exported once the file is let go");
    EXPECT(bytes() == whole, "the whole garment is in the file");

#ifdef _WIN32  // elsewhere a file that is read can still be replaced
    putOldInItsPlace();
    const std::ifstream kept(filePath(path.toStdString()), std::ios::binary);
    REQUIRE(kept.is_open());
    EXPECT(!w.window.exportObj(path), "a file that stays held is not replaced");
    EXPECT(w.window.helpLine().startsWith("Could not write"), "and why is said");
    EXPECT(bytes() == "old", "the old file is as it was");
    EXPECT(QDir(folder.path()).entryList(QDir::Files).size() == 1, "nothing is left beside it");
#endif
}

TEST_CASE("Export OBJ writes the pattern as it is, also with only the table shown; with nothing to export it is greyed", "[qt]")
{
    Window w;
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-export-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("wide.obj");
    w.window.shelf()->layout(ViewLayout::TableOnly)->trigger();
    w.window.tick();
    const std::size_t before = trianglesOf(w.window.session().shownMesh());
    setChest(w, "120 cm");
    w.window.tick();
    REQUIRE(w.window.exportObj(path));
    const std::size_t written = sim::loadObj(path.toStdString()).surface.triangles.size();
    EXPECT(written > before, "the wider chest is in the file");
    EXPECT(written == trianglesOf(sim::meshOf(w.doc(), evaluate(w.doc()))), "as the pattern is now");

    Answering answer(QMessageBox::Discard);
    choose(w, "fileNew");
    EXPECT(!w.window.findChild<QAction*>("fileExportObj")->isEnabled(), "an empty table has nothing to export");
    w.window.shelf()->undo()->trigger();
    choose(w, "fileSample0");
    EXPECT(w.window.findChild<QAction*>("fileExportObj")->isEnabled(), "a garment has");
}

TEST_CASE("Help > About names the version, the commit and the licences", "[qt]")
{
    Window w;
    auto* item = w.window.findChild<QAction*>("helpAbout");
    REQUIRE(item != nullptr);
    item->trigger();
    auto* about = w.window.findChild<QMessageBox*>("about");
    REQUIRE(about != nullptr);
    EXPECT(about->isVisible(), "it is shown, and the window goes on");
    const QString text = about->text();
    EXPECT(text.startsWith("Twintailor " TWINTAILOR_PROJECT_VERSION " ("), "the version CMakeLists.txt names, and the commit");
    EXPECT((text.contains("MIT") && text.contains("LGPL 3") && text.contains("THIRD-PARTY.md")), "the licences, and where the rest is said");
    about->close();
}

// The README's walk through "A first garment": what it tells a new user to do must work.
TEST_CASE("the README's first garment: the trousers drape, take a wider waist, and export", "[qt]")
{
    Window w;
    choose(w, "fileSample6");
    w.waitForTheBody();
    const session::Session& session = w.window.session();
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 400; ++i) w.window.tick();
    EXPECT((session.clothShown() && !session.cloth().sewing && session.cloth().open.empty()), "every seam closes");

    type(w.window.parameters()->field(w.parameter("waist")), "80");
    for (int i = 0; i < 400; ++i) w.window.tick();
    EXPECT(w.window.parameters()->field(w.parameter("waist"))->text() == "80 cm", "a waist of 80 cm is taken");
    EXPECT((session.editor().shapes().errors.empty() && session.cloth().open.empty()), "and the trousers still close");

    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/twintailor-readme-XXXXXX");
    REQUIRE(folder.isValid());
    EXPECT(w.window.exportObj(folder.filePath("trousers.obj")), "and it is exported");
}


// Numbers start again in every pattern, so its first panel and shape have those of the last one's.
TEST_CASE("a layer of another pattern that has the same number is shown as what it is", "[qt]")
{
    Window w;
    // Opens a pattern of one panel with this shape, selects the panel and says what its layer's card is called.
    const auto cardOf = [&](auto shape) {
        Document doc;
        const PanelId panel = doc.add(Panel{.name = "panel"});
        const OpId layer = doc.add(panel, shape);
        w.window.open(std::move(doc), "pattern");
        w.window.select(panel);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QWidget* card = w.window.attributes()->findChild<QWidget*>(QString("layer%1").arg(layer.value));
        REQUIRE(card != nullptr);
        QPushButton* name = card->findChild<QPushButton*>("layerName");
        REQUIRE(name != nullptr);
        return name->text();
    };
    EXPECT(cardOf(RingSector{.innerLength = 60_cm, .sweep = 180_deg, .depth = 30_cm}) == "Ring sector", "three numbers");
    // The same numbers name a shape with two now: its card is made for it, and no third number is read.
    EXPECT(cardOf(Rect{.width = 40_cm, .height = 20_cm}) == "Rectangle", "two numbers");
    EXPECT(cardOf(Circle{.radius = 10_cm}) == "Circle", "and another two, called by their own name");
}
