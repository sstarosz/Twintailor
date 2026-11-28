// Benchmarks of the Qt window: how long a frame takes while nothing happens,
// while a panel or a corner is dragged on the table, while the 3D view is
// orbited and while the cloth runs. Judged against baselines/QtWindow.txt;
// see Perf.h.
//
// The rows after "the cloth runs on its own thread" look for what the owner
// feels as slow: where one change's time goes (each dock, the table, the 3D
// scene, one by one), a 4K picture, a panel carried in 3D, a parameter
// dragged, and where one change's time goes on the trousers. A drag reports
// its slow frames too (Perf.h, `Frames`), and splits each frame into the
// change, the session's tick and Qt's painting.
//
// The window runs without a screen (Qt's off-screen platform). There the 3D
// view draws off screen and reads the picture back each time it changes,
// which costs more than showing it in a window: the limits allow for that.
// With no Vulkan at all, the 3D view draws nothing and costs nothing.

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QPaintEvent>
#include <algorithm>
#include <map>
#include <QMouseEvent>
#include <QWheelEvent>
#include <chrono>
#include <thread>

#include "AttributeEditor.h"
#include "Avatar.h"
#include "MainWindow.h"
#include "ParametersPanel.h"
#include "Perf.h"
#include "SpaceView.h"
#include "TableView.h"
#include "Theme.h"
#include "twintailor/render/Camera.h"
#include "twintailor/render/Gizmo.h"
#include "twintailor/render/SceneOf.h"
#include "twintailor/session/Samples.h"
#include "twintailor/sim/Mesh.h"

using namespace twintailor;
using namespace twintailor::ui;

namespace {

// A garment the size of a skirt, arranged around the body and sewn.
Document skirt() {
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front", .at = {20, 20}});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 60_cm, .height = 80_cm});
    const PanelId back = doc.add(Panel{.name = "back", .at = {100, 20}});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 60_cm, .height = 80_cm});
    doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.add(Seam{.a = {front, frontRect.left()}, .b = {back, backRect.right()}});
    doc.arrange(front, Arrangement{.placed = true, .at = {0, 90, 26}, .wrap = 100});
    doc.arrange(back, Arrangement{.placed = true, .at = {0, 90, -26}, .turn = 180, .wrap = 100});
    return doc;
}

// One frame as the window runs it: the session catches up, then Qt paints.
void frame(MainWindow& window) {
    window.tick();
    QApplication::processEvents();
}

void mouse(QWidget* widget, QEvent::Type type, QPointF at, Qt::MouseButtons held) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

// An avatar as detailed as a generated one: 1.4 million triangles, an egg
// where the skirt hangs.
std::shared_ptr<const Avatar> detailedAvatar() {
    constexpr int around = 850, rings = 850;
    sim::Surface egg;
    for (int r = 0; r <= rings; ++r)
        for (int a = 0; a < around; ++a) {
            const double up = pi * r / rings, turn = 2.0 * pi * a / around;
            egg.points.push_back({15.0 * std::sin(up) * std::cos(turn), 100.0 - 30.0 * std::cos(up), 12.0 * std::sin(up) * std::sin(turn)});
        }
    const auto at = [&](int r, int a) { return static_cast<std::uint32_t>(r * around + a % around); };
    for (int r = 0; r < rings; ++r)
        for (int a = 0; a < around; ++a) {
            egg.triangles.push_back({at(r, a), at(r + 1, a + 1), at(r + 1, a)});
            egg.triangles.push_back({at(r, a), at(r, a + 1), at(r + 1, a + 1)});
        }
    return std::make_shared<const Avatar>(sim::avatarOf(egg, "detailed.obj"));
}

// The cloth playing on such a body: every frame has new cloth to draw and the
// same body, which must cost little however detailed it is.
void onADetailedAvatar(MainWindow& window, perf::Report& report) {
    Document doc = skirt();
    doc.setAvatar(detailedAvatar());
    window.open(std::move(doc), "detailed");
    for (int i = 0; i < 2000 && !window.session().bodyReady(); ++i) {
        window.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 30; ++i) frame(window);
    report.record("frame with a cloth step in it, 3D shown, avatar of 1,445,000 triangles", 50.0,
                  perf::bestMilliseconds([&] { frame(window); }, 20));
    report.expect(window.session().clothShown(), "the cloth plays on the detailed avatar");
    window.shelf()->simulate()->setChecked(false);
}

// The cloth playing on its own thread. A frame that brings a new state of the
// cloth draws the 3D picture again, and a frame that brings none costs next to
// nothing, so each kind is timed by itself: how many there are of each depends
// on how long a sleep of Windows lasts (4 ms or 15.6 ms), which other programs
// decide.
void clothOnItsOwnThread(MainWindow& window, perf::Report& report) {
    perf::Frames fresh, same;
    const auto timed = [&] {
        const double before = window.session().cloth().time;
        const double one = perf::bestMilliseconds([&] { frame(window); }, 1);
        (window.session().cloth().time > before ? fresh : same).add(one);
    };
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (fresh.count() < 40 && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));  // the cloth steps meanwhile
        timed();
        timed();  // at once after it: nearly always nothing new
    }
    report.expect(fresh.count() >= 40 && same.count() >= 20, "the cloth moves without the frames stepping it");
    report.record("own-thread cloth: a frame with a new state", 16.0, fresh.mean());
    // So short that it moves threefold with how long the sleep before it was: its limit alone judges it.
    report.record("own-thread cloth: a frame with nothing new", 1.0, same.mean(), false);
}

// Which widgets are painted, how often and how big: what a change makes Qt paint.
class PaintCounter : public QObject {
public:
    struct Seen {
        int times = 0;
        long long pixels = 0;
    };
    std::map<std::string, Seen> seen;
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Paint && object->isWidgetType()) {
            const QRect area = static_cast<QPaintEvent*>(event)->region().boundingRect();
            Seen& one = seen[std::string(object->metaObject()->className()) + "#" + object->objectName().toStdString()];
            ++one.times;
            one.pixels += static_cast<long long>(area.width()) * area.height();
        }
        return false;
    }
    void print(const char* title, int frames) const {
        std::vector<std::pair<std::string, Seen>> rows(seen.begin(), seen.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.pixels > b.second.pixels; });
        std::printf("paint events per frame while %s:\n", title);
        for (std::size_t i = 0; i < rows.size() && i < 12; ++i)
            std::printf("   %-44s %5.1f paints, %9.0f pixels\n", rows[i].first.c_str(), rows[i].second.times / double(frames), rows[i].second.pixels / double(frames));
    }
};

// A drag as a screen shows it: every frame is timed alone and spaced out as a
// 60 Hz screen would, and the first few pixels (not yet a drag) are left out.
// `split`, if given, also gets each frame's two halves: the pointer's move,
// which changes the document and shows it in every dock (`change`), and the
// frame that follows, itself in two: `tick` (the session catches up and
// everything shown follows) and `paint` (Qt paints what was marked).
struct Halves {
    perf::Frames change, tick, paint;
};
template <class Move>
perf::Frames dragged(MainWindow& window, int frames, Move move, Halves* split = nullptr) {
    perf::Frames spent;
    for (int i = 0; i < frames + 3; ++i) {
        double moving = 0.0, ticking = 0.0, painting = 0.0;
        const double one = perf::bestMilliseconds(
            [&] {
                moving = perf::bestMilliseconds([&] { move(i); }, 1);
                ticking = perf::bestMilliseconds([&] { window.tick(); }, 1);
                painting = perf::bestMilliseconds([&] { QApplication::processEvents(); }, 1);
            },
            1);
        if (i >= 3) {
            spent.add(one);
            if (split) {
                split->change.add(moving);
                split->tick.add(ticking);
                split->paint.add(painting);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    return spent;
}

// A run of frames with its halves, and the best of several: a busy machine
// only ever makes a drag slower (Perf.h), and the drags that draw in 3D vary
// by half between runs on one quiet machine.
struct Run {
    perf::Frames frames;
    Halves halves;
};
template <class Once>
Run bestOf(int runs, Once once) {
    Run best = once();
    for (int i = 1; i < runs; ++i)
        if (Run now = once(); now.frames.mean() < best.frames.mean()) best = std::move(now);
    return best;
}

// The halves of a drag as rows of their own. The move (the change and showing
// it in every dock) is what the docks' work shows in, so it is held to the
// baseline; the other two only to a limit.
void reportHalves(perf::Report& report, const std::string& of, const Halves& halves, double moveLimit) {
    report.record(of + ": the move, the change shown in every dock", moveLimit, halves.change.mean());
    report.record(of + ": the frame's tick, the session catches up", 16.0, halves.tick.mean(), false);
    report.record(of + ": the frame's painting", 16.0, halves.paint.mean(), false);
}

// A corner of the front panel, dragged on the table. Its frames; the document
// must have changed on most of them, or the drag missed the corner.
perf::Frames cornerDragged(MainWindow& window, perf::Report& report, const std::string& what, Halves* split = nullptr) {
    TableView* table = window.table();
    const PanelId front = window.session().document().panels().front();
    window.select(front);
    frame(window);
    const Panel& placed = window.session().document().get(front);
    QPointF corner = table->toScreen({placed.at.x + 60, placed.at.y + 80});
    mouse(table, QEvent::MouseButtonPress, corner, Qt::LeftButton);
    const std::uint64_t versionAtGrab = window.session().editor().version();
    const perf::Frames spent = dragged(
        window, 40,
        [&](int) {
            corner += QPointF(3, -3);
            mouse(table, QEvent::MouseMove, corner, Qt::LeftButton);
        },
        split);
    mouse(table, QEvent::MouseButtonRelease, corner, Qt::NoButton);
    report.expect(window.session().editor().version() - versionAtGrab >= 20, what + ": the corner follows the pointer");
    window.shelf()->undo()->trigger();
    return spent;
}

// A panel dragged on the table.
perf::Frames panelDragged(MainWindow& window, perf::Report& report, const std::string& what) {
    TableView* table = window.table();
    const PanelId front = window.session().document().panels().front();
    window.select(front);
    frame(window);
    const double xBefore = window.session().document().get(front).at.x;
    QPointF on = table->toScreen({xBefore + 30, window.session().document().get(front).at.y + 40});
    mouse(table, QEvent::MouseButtonPress, on, Qt::LeftButton);
    const perf::Frames spent = dragged(window, 40, [&](int) {
        on += QPointF(4, 0);
        mouse(table, QEvent::MouseMove, on, Qt::LeftButton);
    });
    mouse(table, QEvent::MouseButtonRelease, on, Qt::NoButton);
    report.expect(window.session().document().get(front).at.x != xBefore, what + ": the panel moves");
    window.shelf()->undo()->trigger();
    return spent;
}

// Where one change's time goes. After every change of the document the window
// shows it again in each dock, the table and the 3D view; each is timed here
// as the window calls it, so that the dearest can be found. They are not
// added up: some are not made again when nothing they show has changed.
void phases(MainWindow& window, perf::Report& report, const std::string& of) {
    std::printf("-- where the time of one change goes: %s, 1600 x 900 --\n", of.c_str());
    window.resize(1600, 900);
    window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    const PanelId front = window.session().document().panels().front();
    window.select(front);
    for (int i = 0; i < 5; ++i) frame(window);
    const std::string phase = "phase (" + of + "): ";
    const Document& doc = window.session().document();
    const Result& shapes = window.session().editor().shapes();
    const Outliner::Body body{.name = "avatar", .shown = true, .ready = true};
    const int runs = 20;
    report.record(phase + "the Outliner shows the document", 8.0, perf::bestMilliseconds([&] { window.outliner()->show(doc, shapes, front, body); }, runs));
    report.record(phase + "the Parameters dock shows the document", 8.0, perf::bestMilliseconds([&] { window.parameters()->show(doc, shapes); }, runs));
    report.record(phase + "the Attribute Editor shows a panel", 8.0, perf::bestMilliseconds([&] { window.attributes()->show(doc, shapes, front); }, runs));
    report.record(phase + "the table is given a document", 8.0, perf::bestMilliseconds([&] { window.table()->show(doc, shapes, front, {}, session::Tool::Select); }, runs));
    report.record(phase + "the document is evaluated to outlines", 8.0, perf::bestMilliseconds([&] { (void)evaluate(doc); }, runs));
    report.record(phase + "the outlines are meshed at 2 cm", 40.0, perf::bestMilliseconds([&] { (void)sim::meshOf(doc, shapes, {.particleDistance = 2_cm}); }, 5));
    QImage image(window.table()->size(), QImage::Format_ARGB32_Premultiplied);
    report.record(phase + "the table paints itself", 8.0, perf::bestMilliseconds([&] { window.table()->render(&image); }, runs));
    SpaceView& space = *window.space();
    report.record(phase + "the 3D scene is made from the session", 8.0,
                  perf::bestMilliseconds([&] { (void)render::sceneOf(window.session(), space.camera(), 1.0, {.selected = front, .body = true, .mesh = false}); }, runs));
    if (space.drawing() != SpaceView::Drawing::Nothing && space.renderer()) {
        const int width = std::max(1, space.width()), height = std::max(1, space.height());
        report.record(phase + "the scene is copied for a drawing (with the gizmo)", 8.0, perf::bestMilliseconds([&] { (void)space.sceneFor(width, height); }, runs));
        const render::Scene scene = space.sceneFor(width, height);
        report.record(phase + "the renderer draws it and reads it back", 60.0, perf::bestMilliseconds([&] { (void)space.renderer()->draw(scene, width, height); }, runs));
    }
}

// The same things on a 4K picture (3840 x 2160 pixels: the owner's display).
// The off-screen platform has a device pixel ratio of 1, so the size is set in
// pixels, which costs the picture the same.
void atFourK(MainWindow& window, perf::Report& report) {
    std::printf("-- 4K (3840 x 2160) --\n");
    window.resize(3840, 2160);
    window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    for (int i = 0; i < 5; ++i) frame(window);
    report.record("4K: frame, both views, nothing changing", 2.0, perf::bestMilliseconds([&] { frame(window); }, 40));
    QImage image(window.table()->size(), QImage::Format_ARGB32_Premultiplied);
    report.record("4K: the table paints itself", 8.0, perf::bestMilliseconds([&] { window.table()->render(&image); }, 10));
    SpaceView& space = *window.space();
    if (space.drawing() != SpaceView::Drawing::Nothing) {
        QPointF at(400, 600);
        const auto orbited = [&](double step) {
            space.press(at, Qt::LeftButton, Qt::AltModifier);
            const perf::Frames spent = dragged(window, 30, [&](int) {
                at += QPointF(step, 0);
                space.moveTo(at, Qt::LeftButton, Qt::AltModifier);
            });
            space.release();
            return spent;
        };
        report.frames("4K: frames while the 3D view is orbited (read back)", 120.0, orbited(4));
        // The same way back with the body see-through: it is then drawn twice, over the cloth.
        window.showBodySeeThrough(true);
        report.frames("4K: frames while the 3D view is orbited, the body see-through (read back)", 120.0, orbited(-4));
        window.showBodySeeThrough(false);
    }
    window.setThreading(session::Threading::OwnThread);
    const auto panelRun = [&] { return Run{panelDragged(window, report, "4K"), {}}; };
    const auto cornerRun = [&](const std::string& what) {
        return bestOf(2, [&] {
            Run run;
            run.frames = cornerDragged(window, report, what, &run.halves);
            return run;
        });
    };
    report.frames("4K: frames while dragging a panel on the table", 16.0, bestOf(2, panelRun).frames);
    report.frames("4K: frames while dragging a corner, 3D shown", 16.0, cornerRun("4K").frames);
    window.shelf()->layout(ViewLayout::TableOnly)->trigger();
    for (int i = 0; i < 3; ++i) frame(window);
    report.frames("4K: frames while dragging a corner, table only", 16.0, cornerRun("4K, table only").frames);
    // Zooming moves the grid, so the table's floor is drawn again every frame.
    {
        TableView* table = window.table();
        const QPointF middle(table->width() / 2.0, table->height() / 2.0);
        const Run zoomed = bestOf(2, [&] {
            Run run;
            run.frames = dragged(window, 30, [&](int i) {
                QWheelEvent wheel(middle, table->mapToGlobal(middle), QPoint(), QPoint(0, i % 2 ? -60 : 60), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(table, &wheel);
            });
            return run;
        });
        report.frames("4K: frames while the table is zoomed, table only", 16.0, zoomed.frames);
        table->fit();  // the panels now fill the table: what follows is zoomed in
    }
    // Zoomed in on a panel, the table's shapes are many pixels, not just its grid.
    const Run filled = cornerRun("4K, zoomed in");
    report.frames("4K: frames while dragging a corner, panel fills the table", 16.0, filled.frames);
    reportHalves(report, "4K, panel fills the table", filled.halves, 16.0);
    window.setThreading(session::Threading::OneStepPerAdvance);
    window.resize(1600, 900);
    window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    for (int i = 0; i < 3; ++i) frame(window);
}

// The trousers (the sample), the largest one: four panels, two of them
// instances, and eight seams. Where one change's time goes there.
void trousers(MainWindow& window, perf::Report& report) {
    window.open(session::samples()[6].build(), "trousers");
    for (int i = 0; i < 400 && (window.session().mesh()->panels.empty() || !window.session().bodyReady()); ++i) {
        frame(window);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    phases(window, report, "trousers");
}

// A T-shirt body (the sample) as the owner works on it: a panel carried in 3D
// with the move gizmo, and a parameter dragged.
void tshirt(MainWindow& window, perf::Report& report) {
    std::printf("-- a T-shirt, as it is worked on --\n");
    Document shirt = session::samples()[5].build();
    session::layOutOnTheTable(shirt);
    window.open(std::move(shirt), "T-shirt");
    for (int i = 0; i < 400 && (window.session().mesh()->panels.empty() || !window.session().bodyReady()); ++i) {
        frame(window);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    window.shelf()->action(session::Tool::Move)->trigger();
    const PanelId panel = window.session().document().panels().front();
    window.select(panel);
    for (int i = 0; i < 5; ++i) frame(window);
    phases(window, report, "T-shirt");
    window.select(panel);
    window.shelf()->action(session::Tool::Move)->trigger();
    for (int i = 0; i < 5; ++i) frame(window);

    // ---- A panel carried in 3D by the middle of its move gizmo ----
    SpaceView& space = *window.space();
    if (space.drawing() != SpaceView::Drawing::Nothing && space.picture() && render::gizmoOf(window.session(), panel, session::GizmoMode::Move, 1.0)) {
        const Run carried = bestOf(3, [&] {
            const render::Gizmo gizmo = *render::gizmoOf(window.session(), panel, session::GizmoMode::Move, 1.0);
            const render::Picture& picture = *space.picture();
            const render::Projected centre = render::project(space.camera().viewProjection(picture.width / static_cast<double>(picture.height)), gizmo.centre);
            QPointF at((centre.x + 1.0) / 2.0 * picture.width, (centre.y + 1.0) / 2.0 * picture.height);
            const std::uint64_t sceneBefore = window.session().sceneVersion();
            space.press(at, Qt::LeftButton, Qt::NoModifier);
            Run run;
            run.frames = dragged(
                window, 40,
                [&](int) {
                    at += QPointF(3, 0);
                    space.moveTo(at, Qt::LeftButton, Qt::NoModifier);
                },
                &run.halves);
            space.release();
            report.expect(window.session().sceneVersion() - sceneBefore >= 20, "carrying a panel in 3D changes the scene on most frames");
            window.shelf()->undo()->trigger();
            for (int i = 0; i < 5; ++i) frame(window);
            return run;
        });
        report.frames("frames while a panel is carried in 3D", 24.0, carried.frames);
        reportHalves(report, "carried in 3D", carried.halves, 16.0);
    } else {
        std::printf("(no Vulkan, or no gizmo: carrying a panel in 3D is not timed) %s\n", space.problem().toStdString().c_str());
    }

    // ---- A length parameter dragged: every dock, both views and the mesh follow ----
    window.setThreading(session::Threading::OwnThread);
    const Document& doc = window.session().document();
    for (const Param parameter : doc.parameters()) {
        if (doc.get(parameter).unit != Unit::Length) continue;
        // TWINTAILOR_BENCH_PAINT=1 also lists which widgets Qt paints, and how much, per frame.
        const bool counting = qEnvironmentVariableIsSet("TWINTAILOR_BENCH_PAINT");
        const Run changed = bestOf(3, [&] {
            const std::uint64_t versionBefore = window.session().editor().version();
            int step = 0;
            emit window.parameters()->valueDragged(parameter, 40.0, session::DragPhase::Begin);
            PaintCounter counter;
            if (counting) qApp->installEventFilter(&counter);
            Run run;
            run.frames = dragged(
                window, 40, [&](int) { emit window.parameters()->valueDragged(parameter, 40.0 + 0.1 * ++step, session::DragPhase::Continue); }, &run.halves);
            if (counting) {
                qApp->removeEventFilter(&counter);
                counter.print("a parameter is dragged", 43);
            }
            emit window.parameters()->valueDragged(parameter, 40.0 + 0.1 * step, session::DragPhase::End);
            report.expect(window.session().editor().version() - versionBefore >= 20, "dragging a parameter changes the document on most frames");
            window.shelf()->undo()->trigger();
            for (int i = 0; i < 5; ++i) frame(window);
            return run;
        });
        report.frames("frames while a parameter is dragged, 3D shown", 16.0, changed.frames);
        reportHalves(report, "parameter dragged", changed.halves, 8.0);
        break;
    }
    window.setThreading(session::Threading::OneStepPerAdvance);
}

} // namespace

int main(int argc, char** argv) {
    perf::Report report(argc, argv);
    if (!report.worthRunning()) return 0;
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    int qtArguments = 1;
    QApplication app(qtArguments, argv);
    theme::apply(app);
    {
        MainWindow window(session::Threading::OneStepPerAdvance);
        window.setTicking(false);  // the frames are run here, one at a time
        window.resize(1600, 900);
        window.open(skirt(), "skirt");
        // On the project's avatar, as the samples are; the cloth waits until it can collide with it.
        if (!window.loadAvatar(QString::fromStdString(project::avatarPath()))) return 1;
        for (int i = 0; i < 1000 && !window.session().bodyReady(); ++i) {
            window.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        window.show();
        const auto best = [&](int runs) { return perf::bestMilliseconds([&] { frame(window); }, runs); };

        // ---- Nothing changing ----
        window.shelf()->layout(ViewLayout::TableOnly)->trigger();
        for (int i = 0; i < 5; ++i) frame(window);
        report.record("frame, table only, nothing changing", 1.0, best(60));
        window.shelf()->layout(ViewLayout::SideBySide)->trigger();
        for (int i = 0; i < 5; ++i) frame(window);
        const std::uint64_t meshes = window.session().meshesMade();
        report.record("frame, both views, nothing changing", 1.0, best(60));
        report.expect(window.session().meshesMade() == meshes, "an unchanged document is not meshed again");

        // ---- Orbiting the 3D view: the picture drawn again each time ----
        SpaceView& space = *window.space();
        if (space.drawing() != SpaceView::Drawing::Nothing) {
            const double yaw = space.camera().yaw;
            QPointF at(200, 300);
            space.press(at, Qt::LeftButton, Qt::AltModifier);
            const double orbit = perf::bestMilliseconds(
                [&] {
                    at += QPointF(4, 0);
                    space.moveTo(at, Qt::LeftButton, Qt::AltModifier);
                    QApplication::processEvents();
                },
                40);
            space.release();
            report.record("3D view orbited (drawn off screen and read back)", 60.0, orbit);
            report.expect(space.camera().yaw != yaw, "orbiting turns the camera");
        } else {
            std::printf("(no Vulkan: the 3D view is not timed) %s\n", space.problem().toStdString().c_str());
        }

        // ---- Dragging a panel on the table: placement only, nothing meshed again ----
        window.shelf()->layout(ViewLayout::TableOnly)->trigger();
        for (int i = 0; i < 3; ++i) frame(window);
        TableView* table = window.table();
        const PanelId front = window.session().document().panels().front();
        const double xBefore = window.session().document().get(front).at.x;
        QPointF on = table->toScreen({xBefore + 30, window.session().document().get(front).at.y + 40});
        mouse(table, QEvent::MouseButtonPress, on, Qt::LeftButton);
        const std::uint64_t meshesBeforeDrag = window.session().meshesMade();
        const double drag = perf::bestMilliseconds(
            [&] {
                on += QPointF(4, 0);
                mouse(table, QEvent::MouseMove, on, Qt::LeftButton);
                frame(window);
            },
            40);
        mouse(table, QEvent::MouseButtonRelease, on, Qt::NoButton);
        report.record("frame while dragging a panel on the table", 8.0, drag);
        report.expect(window.session().document().get(front).at.x != xBefore && window.session().meshesMade() == meshesBeforeDrag, "the panel moves and is not meshed again");

        // ---- Dragging a corner, the mesh made on another thread, 1 cm particles ----
        // The shape changes every frame and the mesh follows it, but the frame
        // does not wait for it. The average counts, not the best: one slow
        // frame in three is what a lag feels like.
        {
            window.shelf()->layout(ViewLayout::SideBySide)->trigger();  // the 3D view shown, as one works
            window.setThreading(session::Threading::OwnThread);
            window.select(front);
            frame(window);
            emit window.attributes()->particleDistanceTyped(front, "1");
            for (int i = 0; i < 200 && window.session().meshesMade() == meshesBeforeDrag; ++i) {
                frame(window);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const Panel& placed = window.session().document().get(front);
            QPointF corner = table->toScreen({placed.at.x + 60, placed.at.y + 80});
            mouse(table, QEvent::MouseButtonPress, corner, Qt::LeftButton);
            const std::uint64_t meshesAtGrab = window.session().meshesMade();
            double sum = 0.0;
            const int watched = 40;
            for (int i = 0; i < watched + 3; ++i) {
                corner += QPointF(3, -3);
                const double one = perf::bestMilliseconds(
                    [&] {
                        mouse(table, QEvent::MouseMove, corner, Qt::LeftButton);
                        frame(window);
                    },
                    1);
                if (i >= 3) sum += one;  // the first few pixels of a drag are not yet a drag
                std::this_thread::sleep_for(std::chrono::milliseconds(12));  // as a 60 Hz screen spaces frames out
            }
            mouse(table, QEvent::MouseButtonRelease, corner, Qt::NoButton);
            const std::uint64_t meshesDuringDrag = window.session().meshesMade() - meshesAtGrab;
            report.record("frame while dragging a corner, 3D shown, 1 cm particles", 16.0, sum / watched);
            std::printf("(%llu meshes made during the drag of %d frames)\n", static_cast<unsigned long long>(meshesDuringDrag), watched + 3);
            report.expect(meshesDuringDrag >= 5, "the mesh follows the corner while it is dragged");
            emit window.attributes()->particleDistanceTyped(front, "2");
            window.setThreading(session::Threading::OneStepPerAdvance);
            for (int i = 0; i < 3; ++i) frame(window);
        }

        // ---- The cloth, one step a frame, both views shown ----
        window.shelf()->simulate()->setChecked(true);
        for (int i = 0; i < 60; ++i) frame(window);
        report.record("frame with a cloth step in it, 3D shown", 40.0, best(40));
        report.expect(window.session().clothShown() && window.session().cloth().time > 1.0, "the cloth's time moves on");
        window.shelf()->simulate()->setChecked(false);

        // ---- The cloth on its own thread: the frame only picks up the news ----
        window.setThreading(session::Threading::OwnThread);
        window.shelf()->simulate()->setChecked(true);
        for (int i = 0; i < 3; ++i) frame(window);
        clothOnItsOwnThread(window, report);
        window.shelf()->simulate()->setChecked(false);

        // What follows is what the owner feels: the slow frames, not the best
        // ones; a 4K picture; moving a panel in 3D; changing a parameter.
        window.setThreading(session::Threading::OneStepPerAdvance);
        for (int i = 0; i < 3; ++i) frame(window);
        phases(window, report, "skirt");
        atFourK(window, report);
        tshirt(window, report);
        trousers(window, report);
        onADetailedAvatar(window, report);
    }
    return report.finish();
}
