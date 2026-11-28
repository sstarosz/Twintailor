#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <chrono>
#include <thread>

#include "AttributeEditor.h"
#include "AvatarDialog.h"
#include "Outliner.h"
#include "ParametersPanel.h"
#include "SpaceView.h"
#include "TableView.h"
#include "Theme.h"
#include "ValueField.h"
#include "Version.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/File.h"
#include "twintailor/core/Text.h"
#include "twintailor/session/CrashDump.h"
#include "twintailor/session/Samples.h"
#include "twintailor/sim/AvatarImport.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Layout.h"

namespace twintailor::ui {

namespace {

QDockWidget* makeDock(const QString& title, QWidget* content, QMainWindow* window) {
    auto* dock = new QDockWidget(title, window);
    dock->setObjectName(title);
    dock->setWidget(content);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    return dock;
}

// The avatars that come with the program: the build and an installed
// package put the project's assets/ beside it (apps/qt/CMakeLists.txt).
QString assetsFolder() { return QCoreApplication::applicationDirPath() + "/assets"; }

// The names the folders are kept under in the settings file, in MainWindow::Files' order.
constexpr std::array<const char*, 3> folderKeys = {"patternFolder", "avatarFolder", "exportFolder"};
constexpr const char* markInstancesKey = "instancesInViolet";

// A file named in a folder; the name alone where there is no folder to name.
QString inFolder(const QString& folder, const QString& name) { return folder.isEmpty() ? name : QDir(folder).filePath(name); }

// The pointer that says "wait", for as long as this is there.
struct Waiting {
    Waiting() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
    ~Waiting() { QGuiApplication::restoreOverrideCursor(); }
    Waiting(const Waiting&) = delete;
    Waiting& operator=(const Waiting&) = delete;
};

// The help line while the cloth plays, and while Play waits for a new body.
const char* const playingHint = "Playing: drag the cloth in 3D to pull it.";
const char* const waitingHint = "Preparing the avatar. The cloth starts when it is ready.";

// The surface in an OBJ file, in the file's own numbers; or why there is none.
std::expected<sim::Surface, QString> readSurface(const QString& path) {
    sim::LoadedSurface loaded = sim::loadObj(path.toStdString());
    if (!loaded.problem.empty()) return std::unexpected(QString::fromStdString(loaded.problem) + ". Choose another Wavefront OBJ file.");
    return std::move(loaded.surface);
}

// A surface read from `path` as an avatar, named as the file is.
std::shared_ptr<const Avatar> avatarFrom(const sim::Surface& file, const QString& path, const AvatarImport& readAs) {
    return std::make_shared<const Avatar>(sim::avatarOf(file, QFileInfo(path).fileName().toStdString(), readAs));
}

} // namespace

MainWindow::MainWindow(session::Threading threading, QWidget* parent) : QMainWindow(parent), session_(Document{}, threading) {
    showTitle("untitled");
    resize(1600, 940);
    setDockNestingEnabled(true);

    shelf_ = new Shelf(this);
    // As in Maya: the action row under the menu, the shelf under that, and the
    // toolbox at the left edge, outside the docks.
    shelf_->actionRow()->setWindowTitle("Actions");
    addToolBar(Qt::TopToolBarArea, shelf_->actionRow());
    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar* top = addToolBar("Shelf");
    top->setObjectName("shelf");
    top->setMovable(false);
    top->addWidget(shelf_);
    shelf_->toolbox()->setWindowTitle("Tools");
    addToolBar(Qt::LeftToolBarArea, shelf_->toolbox());

    space_ = new SpaceView(this);
    table_ = new TableView(this);
    views_ = new QSplitter(Qt::Horizontal, this);
    views_->addWidget(space_);
    views_->addWidget(table_);
    views_->setChildrenCollapsible(false);
    views_->setStretchFactor(0, 1);
    views_->setStretchFactor(1, 1);
    views_->setSizes({1, 1});  // each view's own size hint would otherwise decide: half and half
    setCentralWidget(views_);

    outliner_ = new Outliner(this);
    parameters_ = new ParametersPanel(this);
    attributes_ = new AttributeEditor(this);
    QDockWidget* outlinerDock = makeDock("Outliner", outliner_, this);
    QDockWidget* parametersDock = makeDock("Parameters", parameters_, this);
    QDockWidget* attributesDock = makeDock("Attribute Editor", attributes_, this);
    addDockWidget(Qt::LeftDockWidgetArea, outlinerDock);
    splitDockWidget(outlinerDock, parametersDock, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, attributesDock);
    // Docks stacked as tabs show their tabs on top, as Maya's do.
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    resizeDocks({outlinerDock, attributesDock}, {240, 300}, Qt::Horizontal);
    docks_ = {outlinerDock, parametersDock, attributesDock};

    help_ = new QLabel(this);
    help_->setObjectName("helpLine");
    // The help line gives way to the numbers beside it: cut at its own edge, not
    // painted over them; the whole of it is in its tooltip.
    help_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    pointerAt_ = new QLabel(this);
    pointerAt_->setObjectName("pointerAt");
    pointerAt_->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
    frameTimesLine_ = new QLabel(this);
    frameTimesLine_->setObjectName("frameTimes");
    frameTimesLine_->setMinimumWidth(1);  // whole where there is room; cut, after the help line, where there is not
    frameTimesLine_->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
    frameTimesLine_->hide();
    clock_.start();
    statusBar()->addWidget(help_, 1);
    preparing_ = new QProgressBar(this);
    preparing_->setObjectName("bodyProgress");
    preparing_->setRange(0, 100);
    preparing_->setFormat("Preparing the avatar  %p %");
    preparing_->setAlignment(Qt::AlignCenter);
    preparing_->setFixedWidth(280);
    // No higher than the words beside it: the status line, and with it the views, keep their size.
    preparing_->setFixedHeight(fontMetrics().height());
    preparing_->hide();
    statusBar()->addPermanentWidget(preparing_);
    statusBar()->addPermanentWidget(frameTimesLine_);
    statusBar()->addPermanentWidget(pointerAt_);
    statusBar()->setSizeGripEnabled(false);
    say(hintFor(Tool::Select));

    // ---- What the user did, and what it changes ----
    connect(shelf_, &Shelf::toolChosen, this, [this](Tool tool) {
        stopPicking();
        useTool(tool);
        say(QString(nameOf(tool)) + ": " + hintFor(tool));
    });
    connect(shelf_->undo(), &QAction::triggered, this, [this] {
        if (!session_.undo()) return;
        stopPicking();
        drafting_.forget(session_.document());
        table_->cancel();
        refresh();
    });
    connect(shelf_->redo(), &QAction::triggered, this, [this] {
        if (!session_.redo()) return;
        stopPicking();
        drafting_.forget(session_.document());
        table_->cancel();
        refresh();
    });
    connect(shelf_->simulate(), &QAction::toggled, this, [this](bool on) {
        session_.simulate(on);
        // What a drag on the cloth does changes with Play and Pause.
        if (on && session_.bodyProblem()) say(QString::fromStdString(*session_.bodyProblem()), true);
        else if (on) say(session_.bodyReady() ? playingHint : waitingHint);
        else if (session_.clothShown()) say("Paused: in 3D, Move (W) moves the selected panel, drape and all; Pull (H) pulls the cloth.");
        else if (helpLine() == playingHint || helpLine() == waitingHint) say({});  // reset, or another garment: no cloth to drag
    });
    connect(shelf_->reset(), &QAction::triggered, this, [this] {
        if (session_.run(session::ResetCloth{})) say("The cloth is reset. Undo (Ctrl+Z) brings it back as it hung.");
        shelf_->simulate()->setChecked(false);
    });
    connect(shelf_, &Shelf::layoutChosen, this, &MainWindow::setViewLayout);
    connect(outliner_, &Outliner::panelChosen, this, [this](PanelId panel) { select(panel); });
    connect(outliner_, &Outliner::seamChosen, this, [this](SeamId seam) { selectSeam(seam); });
    connect(table_, &TableView::gestured, this, &MainWindow::on);
    // While an avatar is chosen the 3D view only turns: nothing done in it changes anything.
    connect(space_, &SpaceView::panelClicked, this, [this](std::optional<PanelId> panel) {
        if (!choosing_) select(panel);
    });
    connect(space_, &SpaceView::gestured, this, [this](const session::Gesture& gesture) {
        if (!choosing_) on(gesture);
    });
    connect(space_, &SpaceView::keyed, this, [this](QKeyCombination pressed) {
        if (choosing_) {
            if (pressed == QKeyCombination(Qt::Key_Escape)) stopChoosing();
            return;
        }
        if (editInSpace(pressed)) return;
        // Keys typed while the 3D view's own window has the focus do not reach
        // the window's shortcuts by themselves.
        const QKeySequence typed(pressed);
        for (QAction* action : findChildren<QAction*>())
            if (action->isEnabled() && action->shortcuts().contains(typed)) {
                action->trigger();
                return;
            }
    });
    connect(space_, &SpaceView::hinted, this, [this](const QString& next) {
        if (!choosing_) say(next);
    });
    connect(space_, &SpaceView::clothPulled, this, [this](PanelId panel, std::uint32_t point, Vec3 to, session::DragPhase phase) {
        // Pulling the cloth by hand changes nothing in the document, only the drape: one undo step.
        if (choosing_ || !session_.document().contains(panel)) return;
        if (phase == session::DragPhase::Begin) pulled_ = session_.begin();
        if (!pulled_) return;
        if (phase == session::DragPhase::End) session_.commit(*pulled_);
        else if (session_.preview(*pulled_, session::PullCloth{panel, point, to}) && phase == session::DragPhase::Begin) refresh();  // there is something to undo
    });
    connect(table_, &TableView::told, this, [this](const QString& why) { say(why, true); });
    connect(table_, &TableView::hinted, this, [this](const QString& next) { say(next); });
    connect(attributes_, &AttributeEditor::referenceClicked, this, [this](OpId layer, ReferenceId reference) { startPicking(layer, reference); });
    connect(attributes_, &AttributeEditor::operationOffered, this, [this](PanelId panel, session::Offer offer) {
        if (!session_.document().contains(panel)) return;
        // Added as it starts; one that still needs an edge, corner or line shows a problem until it is chosen.
        const auto added = session_.run(session::AddLayer{panel, offer});
        if (!answer(added)) return;
        attributes_->openLayer(added->operation);
        if (added->toChoose) startPicking(added->operation, *added->toChoose);
        if (session_.document().operations(panel).size() == 1) table_->fit();  // a new shape: show it
    });
    connect(table_, &TableView::referencePicked, this, [this](const Reference& reference) {
        const std::optional<Picking> was = picking_;
        stopPicking();
        if (was) answer(session_.run(session::SetReference{was->operation, was->reference, reference}));
    });
    connect(table_, &TableView::choosingDropped, this, [this] {
        stopPicking();
        say({});
    });
    connect(table_, &TableView::pointerAt, this, [this](Vec2 at) {
        pointerAt_->setText(QString::fromStdString("x " + formatValue(at.x, Unit::Length) + "   y " + formatValue(at.y, Unit::Length)));
    });
    connect(table_, &TableView::painted, this, [this](double milliseconds) {
        frameTimes_.shown(session::FrameTimes::View::Table, static_cast<double>(clock_.nsecsElapsed()) / 1e6);
        frameTimes_.took(session::FrameTimes::Work::TablePainting, milliseconds);
    });
    connect(space_, &SpaceView::drawn, this, [this](double milliseconds) {
        frameTimes_.shown(session::FrameTimes::View::Space, static_cast<double>(clock_.nsecsElapsed()) / 1e6);
        frameTimes_.took(session::FrameTimes::Work::SpaceDrawing, milliseconds);
    });

    connect(parameters_, &ParametersPanel::valueTyped, this, [this](Param parameter, QString text) {
        ValueField* field = parameters_->field(parameter);
        if (const std::optional<Expr> value = read(text, session_.document().get(parameter).unit, field))
            answer(session_.run(session::SetField{parameter, *value}), field);
    });
    connect(parameters_, &ParametersPanel::addRequested, this, [this] { answer(session_.run(session::AddParameter{})); });

    // Dragging a number: the same changes as typing it, one undo step per drag.
    connect(parameters_, &ParametersPanel::valueDragged, this, [this](Param parameter, double value, session::DragPhase phase) {
        if (session_.document().contains(parameter)) dragField(phase, session::SetField{parameter, Expr(value)});
    });
    connect(attributes_, &AttributeEditor::fieldDragged, this, [this](OpId layer, FieldId id, double value, session::DragPhase phase) {
        if (session_.document().contains(layer)) dragField(phase, session::SetField{session::OperationField{layer, id}, Expr(value)});
    });
    connect(attributes_, &AttributeEditor::spaceDragged, this, [this](PanelId panel, session::SpaceField field, double value, session::DragPhase phase) {
        if (session_.document().contains(panel)) dragField(phase, session::SetField{session::PanelField{panel, field}, Expr(value)});
    });
    connect(attributes_, &AttributeEditor::tableDragged, this, [this](PanelId panel, session::TableField field, double value, session::DragPhase phase) {
        if (session_.document().contains(panel)) dragField(phase, session::SetField{session::PanelField{panel, field}, Expr(value)});
    });
    connect(parameters_, &ParametersPanel::renamed, this, [this](Param parameter, QString name) {
        const std::string wanted = name.toStdString();
        if (!session_.document().contains(parameter) || wanted == session_.document().get(parameter).name) return;
        QLineEdit* field = parameters_->nameField(parameter);
        const auto renamed = session_.run(session::RenameParameter{parameter, wanted});
        if (renamed) {
            answer(renamed);
            return;
        }
        say(QString::fromStdString(renamed.error()), true);
        if (field) field->setText(QString::fromStdString(session_.document().get(parameter).name));
    });
    connect(parameters_, &ParametersPanel::removeRequested, this, [this](Param parameter) {
        if (session_.document().contains(parameter)) answer(session_.run(session::RemoveParameter{parameter}));
    });
    connect(outliner_, &Outliner::renamed, this, [this](PanelId panel, QString name) {
        if (!session_.document().contains(panel)) return;
        const std::string wanted = name.trimmed().toStdString();
        if (wanted == session_.document().get(panel).name) return;
        const auto renamed = session_.run(session::RenamePanel{panel, wanted});
        if (renamed) {
            answer(renamed);
            return;
        }
        say(QString::fromStdString(renamed.error()), true);
        refresh();  // shows the old name again
    });
    connect(attributes_, &AttributeEditor::layerSwitched, this, [this](OpId layer, bool on) { answer(session_.run(session::SwitchLayer{layer, on})); });
    connect(attributes_, &AttributeEditor::layerOpened, this, [this] { refresh(); });
    // Escape comes back from looking at an earlier step; it is only taken while looking back.
    showAllSteps_ = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    showAllSteps_->setEnabled(false);
    connect(showAllSteps_, &QShortcut::activated, attributes_, &AttributeEditor::showAllSteps);
    connect(attributes_, &AttributeEditor::seamReversed, this, [this](SeamId seam, bool reversed) {
        if (session_.document().contains(seam)) answer(session_.run(session::SetSeamReversed{seam, reversed}));
    });
    connect(outliner_, &Outliner::seamDeleteRequested, this, [this](SeamId seam) {
        if (session_.document().contains(seam)) answer(session_.run(session::RemoveSeam{seam}));
    });
    connect(attributes_, &AttributeEditor::panelReferenceChosen, this, [this](OpId layer, ReferenceId reference, PanelId panel) {
        answer(session_.run(session::SetReference{layer, reference, Reference(panel)}));
    });
    connect(attributes_, &AttributeEditor::cutSideChosen, this, [this](OpId layer, Cut::Side side) {
        answer(session_.run(session::SetCutSide{OpRef<Cut>(layer), side}));
    });
    connect(attributes_, &AttributeEditor::lengthKeepingChosen, this, [this](OpId layer, SetLength::Keeping keeping) {
        answer(session_.run(session::SetKeeping{OpRef<SetLength>(layer), keeping}));
    });
    connect(attributes_, &AttributeEditor::mirrorLastChanged, this, [this](OpId layer, bool last) {
        answer(session_.run(session::SetMirrorLast{OpRef<Mirror>(layer), last}));
    });
    connect(attributes_, &AttributeEditor::instanceMirroredChanged, this, [this](OpId layer, bool mirrored) {
        answer(session_.run(session::SetInstanceMirrored{OpRef<Instance>(layer), mirrored}));
    });
    connect(attributes_, &AttributeEditor::layerMoved, this, [this](OpId layer, std::size_t to) {
        // A layer dragged to where it cannot go is quietly left where it is.
        if (const auto moved = session_.run(session::MoveLayer{layer, to})) answer(moved);
    });
    connect(attributes_, &AttributeEditor::layerRemoved, this, [this](OpId layer) {
        if (viewUntil_ == layer) viewUntil_.reset();
        answer(session_.run(session::RemoveLayer{layer}));
    });
    connect(attributes_, &AttributeEditor::stepViewed, this, [this](std::optional<OpId> layer) {
        // The last step is the panel as it is: that is no looking back.
        if (layer && session_.document().contains(*layer)) {
            const std::vector<OpId> made = session_.document().operations(session_.document().panelOf(*layer));
            if (!made.empty() && made.back() == *layer) layer.reset();
        }
        viewUntil_ = layer;
        table_->cancel();
        say(layer ? "Showing the panel as it was at this step. Nothing is changed on the table until you show all steps again." : QString());
        refresh();
    });
    connect(attributes_, &AttributeEditor::fieldTyped, this, [this](OpId layer, FieldId id, QString text) { typed(layer, id, text, attributes_->field(layer, id)); });
    connect(table_, &TableView::keepingChosen, this, [this](OpId layer, SetLength::Keeping keeping) {
        if (!session_.document().contains(layer)) return;
        const auto done = session_.run(drafting_.typingIn(session_),
                                       session::SetKeeping{OpRef<SetLength>(layer), keeping, session::Grows::InPlaceUnlessSewn});
        if (!done) table_->showLengthProblem(QString::fromStdString(done.error()));
        // The anchor shown is the one the document has, whether or not the change was taken.
        const SetLength::Keeping now = std::get<SetLength>(session_.document().get(layer)).keeping;
        table_->showKeeping(now);
        const QString word = table_->anchorWord(now);
        say(now == SetLength::Keeping::Middle ? "Midpoint fixed · both ends move. Enter applies, Esc cancels."
                                              : word + " end fixed · the other end moves. Enter applies, Esc cancels.",
            !done);
        refresh();
    });
    connect(table_, &TableView::lengthEdited, this, [this](OpId layer, FieldId id, QString text) { previewLength(layer, id, text); });
    connect(table_, &TableView::lengthCommitted, this, [this](OpId layer, FieldId id, QString text) {
        previewLength(layer, id, text);  // what is in the field, if it was not shown yet
        drafting_.commitTyping(session_);
        say({});
    });
    connect(table_, &TableView::lengthCancelled, this, [this] { cancelLength(); });
    connect(attributes_, &AttributeEditor::spaceTyped, this, [this](PanelId panel, session::SpaceField field, QString text) {
        typed({panel, field}, text, attributes_->spaceField(field));
    });
    connect(attributes_, &AttributeEditor::tableTyped, this, [this](PanelId panel, session::TableField field, QString text) {
        typed({panel, field}, text, attributes_->tableField(field));
    });
    connect(outliner_, &Outliner::copyRequested, this, [this](PanelId panel) { on(session::AddBeside{panel, session::Follow::Copy}); });
    // Instances and their seams are made for every selected panel; the rest of the shelf acts on the shown one.
    connect(shelf_, &Shelf::instanceRequested, this, [this] { on(session::DuplicateSelection{session::Follow::Instance}); });
    connect(shelf_, &Shelf::mirroredInstanceRequested, this, [this] { on(session::DuplicateSelection{session::Follow::MirroredInstance}); });
    connect(shelf_, &Shelf::sewAsSourceRequested, this, [this] { on(session::SewAsSource{}); });
    connect(shelf_, &Shelf::bakeRequested, this, [this] {
        const std::optional<PanelId> panel = drafting_.selection.panel;
        if (!panel) return;
        const auto baked = session_.run(session::BakePanel{*panel});
        if (!baked) return say(QString::fromStdString(baked.error()));
        refresh();
        say(QString("Baked %1 formula%2 into numbers.").arg(baked->replaced).arg(baked->replaced == 1 ? "" : "s"));
    });
    connect(shelf_, &Shelf::placeFromTableRequested, this, [this] {
        if (const std::optional<PanelId> selected = drafting_.selection.panel) answer(session_.run(session::PlaceFromTable{*selected}));
    });
    connect(outliner_, &Outliner::baseOnlyChanged, this, [this](PanelId panel, bool baseOnly) {
        if (session_.document().contains(panel)) answer(session_.run(session::SetConstruction{panel, baseOnly}));
    });
    connect(outliner_, &Outliner::deleteRequested, this, [this](PanelId panel) {
        on(session::DeletePanel{panel});
    });

    connect(attributes_, &AttributeEditor::particleDistanceTyped, this, [this](PanelId panel, QString text) {
        if (!session_.document().contains(panel)) return;
        ValueField* field = attributes_->particleDistance();
        if (const std::optional<Expr> value = read(text, Unit::Length, field)) answer(session_.run(session::SetParticleDistance{panel, *value}), field);
    });
    connect(attributes_, &AttributeEditor::particleDistanceDragged, this, [this](PanelId panel, double value, session::DragPhase phase) {
        if (!session_.document().contains(panel)) return;
        // A number pulled past a limit stays at it.
        dragField(phase, session::SetParticleDistance{panel, Expr(std::clamp(value, finestParticleDistance, coarsestParticleDistance))});
    });
    connect(outliner_, &Outliner::bodyShownChanged, this, &MainWindow::showBody);
    connect(outliner_, &Outliner::bodySeeThroughChanged, this, &MainWindow::showBodySeeThrough);
    connect(outliner_, &Outliner::panelShownChanged, this, [this](PanelId panel, bool shown) {
        session_.setPanelShown(panel, shown);
        tick();
    });
    connect(outliner_, &Outliner::panelActiveChanged, this, [this](PanelId panel, bool active) {
        session_.setPanelActive(panel, active);
        tick();
    });
    connect(outliner_, &Outliner::seamShownChanged, this, [this](SeamId seam, bool shown) {
        session_.setSeamShown(seam, shown);
        tick();
    });

    buildMenus();
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);  // a tick late is a picture of the screen missed
    connect(timer_, &QTimer::timeout, this, &MainWindow::tick);
    timer_->start(session::FrameTimes::tickMilliseconds(screen()->refreshRate()));
    refresh();
}

MainWindow::~MainWindow() {
    // Not closed: a copy of unsaved work stays for the next start. After an error the log stays too, for a report.
    if (!autosave_) return;
    autosave_.reset();  // waits for a copy being written
    if (!failed_) {
        session_.endLog();
        session::writeCrashDumpTo({});
    }
    recoveryLock_.reset();
    if (!failed_ && !session::hasRecovery(recoveryFolder_.toStdString())) QDir(recoveryFolder_).removeRecursively();
}

namespace {

// A menu item that calls `does`; the same in every Qt 6.
template <class Does>
QAction* item(QMenu* menu, const QString& text, QKeySequence shortcut, QObject* owner, Does does) {
    QAction* action = menu->addAction(text);
    action->setShortcut(shortcut);
    QObject::connect(action, &QAction::triggered, owner, does);
    return action;
}

} // namespace

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("File");
    item(file, "New", QKeySequence::New, this, [this] {
        if (mayDiscard("starting a new pattern")) open(Document{}, "untitled");
    })->setObjectName("fileNew");
    item(file, "Open…", QKeySequence::Open, this, [this] {
        if (!mayDiscard("opening another pattern")) return;
        const QString path = QFileDialog::getOpenFileName(this, "Open", folderFor(Files::Patterns), "Patterns (*.twintail)");
        if (!path.isEmpty()) openFile(path);
    })->setObjectName("fileOpen");
    item(file, "Save", QKeySequence::Save, this, [this] { save(); })->setObjectName("fileSave");
    item(file, "Save as…", QKeySequence::SaveAs, this, [this] {
        if (!takeWhatIsTyped()) return;
        const QString path =
            QFileDialog::getSaveFileName(this, "Save as", inFolder(folderFor(Files::Patterns), title_ + ".twintail"), "Patterns (*.twintail)");
        if (!path.isEmpty()) saveAs(path);
    })->setObjectName("fileSaveAs");
    exportAction_ = item(file, "Export OBJ…", QKeySequence("Ctrl+E"), this, [this] {
        if (!takeWhatIsTyped()) return;
        const QString path =
            QFileDialog::getSaveFileName(this, "Export OBJ", inFolder(folderFor(Files::Exports), title_ + ".obj"), "Wavefront OBJ (*.obj)");
        if (!path.isEmpty()) exportObj(path);
    });
    exportAction_->setObjectName("fileExportObj");
    file->addSeparator();
    QMenu* samples = file->addMenu("Samples");
    for (std::size_t i = 0; i < session::samples().size(); ++i)
        item(samples, session::samples()[i].name, {}, this, [this, i] {
            if (mayDiscard("opening a sample")) openSample(i);
        })->setObjectName(QString("fileSample%1").arg(i));
    file->addSeparator();
    item(file, "Quit", QKeySequence::Quit, this, [this] { close(); });

    QMenu* edit = menuBar()->addMenu("Edit");
    edit->addAction(shelf_->undo());
    edit->addAction(shelf_->redo());

    QMenu* body = menuBar()->addMenu("Body");
    item(body, "Avatar from OBJ…", {}, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Avatar", folderFor(Files::Avatars), "Wavefront OBJ (*.obj)");
        if (!path.isEmpty()) chooseAvatar(path);
    })->setObjectName("bodyAvatar");

    QMenu* view = menuBar()->addMenu("View");
    item(view, "Frame everything", {}, this, [this] {
        table_->fit();
        space_->frame();
    });
    view->addSeparator();
    bodyAction_ = view->addAction("Body");
    bodyAction_->setCheckable(true);
    bodyAction_->setChecked(session_.bodyPresent());
    bodyAction_->setToolTip("Hidden, the body is also taken out of the simulation.");
    connect(bodyAction_, &QAction::triggered, this, &MainWindow::showBody);
    seeThroughAction_ = view->addAction("See-through body");
    seeThroughAction_->setObjectName("seeThroughBody");
    seeThroughAction_->setCheckable(true);
    seeThroughAction_->setChecked(session_.bodySeeThrough());
    connect(seeThroughAction_, &QAction::triggered, this, &MainWindow::showBodySeeThrough);
    markInstancesAction_ = view->addAction("Instances in violet");
    markInstancesAction_->setObjectName("markInstances");
    markInstancesAction_->setCheckable(true);
    markInstancesAction_->setToolTip("In 3D, draws instances violet, as the table does.");
    connect(markInstancesAction_, &QAction::triggered, this, &MainWindow::markInstances);
    QAction* grid = view->addAction("Grid on the table");
    grid->setCheckable(true);
    grid->setChecked(table_->gridShown());
    connect(grid, &QAction::triggered, this, [this](bool on) { table_->setGridShown(on); });
    view->addAction(shelf_->mesh());
    view->addSeparator();
    // For finding what is slow: the frames as this window and this screen have them.
    QAction* frameTimes = view->addAction("Frame times");
    frameTimes->setObjectName("showFrameTimes");
    frameTimes->setCheckable(true);
    connect(frameTimes, &QAction::toggled, this, [this](bool on) {
        frameTimesLine_->setVisible(on);
        frameTimesShownAt_.reset();  // at once, not at the next quarter second
        showFrameTimes();
    });

    QMenu* windows = menuBar()->addMenu("Windows");
    for (QDockWidget* dock : docks_) windows->addAction(dock->toggleViewAction());

    QMenu* help = menuBar()->addMenu("Help");
    item(help, "About Twintailor", {}, this, [this] {
        auto* about = new QMessageBox(QMessageBox::NoIcon, "About Twintailor", aboutText(), QMessageBox::Ok, this);
        about->setObjectName("about");
        about->setAttribute(Qt::WA_DeleteOnClose);
        about->setTextInteractionFlags(Qt::TextSelectableByMouse);  // so that a report can copy the version
        about->open();
    })->setObjectName("helpAbout");
}

void MainWindow::open(Document document, const QString& title, SceneState state) {
    stopChoosing();
    // A document that names no body (a new one, or a file from before avatars were saved) takes the one shown.
    if (!document.avatar()) document.setAvatar(session_.document().avatar());
    session::layOutOnTheTable(document);
    // A bar for a length of the document that is going has nothing to type into, and the next document may
    // have a layer with the same number.
    table_->closeLength();
    if (autosave_) autosave_->forget();  // what was there is saved, or thrown away
    session_.open(std::move(document), std::move(state));
    shelf_->simulate()->setChecked(false);
    showTitle(title);
    path_.clear();
    drafting_ = {};
    stopPicking();
    table_->cancel();
    refresh();
    table_->fit();
    if (session_.document().panels().empty()) say("The table is empty: choose Rectangle on the shelf and drag on the table to draw a panel.");
}

bool MainWindow::openFile(const QString& path) {
    rememberFolder(Files::Patterns, path);
    sayAtOnce("Opening " + QFileInfo(path).fileName() + "…");
    const Waiting waiting;  // a pattern with a detailed avatar takes seconds
    session_.note("Read a pattern file");
    std::variant<SavedScene, FileError> opened = twintailor::open(path.toStdString());
    if (FileError* problem = std::get_if<FileError>(&opened)) {
        say(QString::fromStdString(problem->line ? "Line " + std::to_string(problem->line) + ": " + problem->message : problem->message), true);
        return false;
    }
    SavedScene& scene = std::get<SavedScene>(opened);
    say({});  // opening says what there is to say
    open(std::move(scene.document), QFileInfo(path).completeBaseName(), std::move(scene.state));
    path_ = path;
    return true;
}

bool MainWindow::saveAs(const QString& path) {
    rememberFolder(Files::Patterns, path);
    sayAtOnce("Saving " + QFileInfo(path).fileName() + "…");
    const Waiting waiting;
    session_.note("Save");
    const std::string problem = twintailor::save(session_.document(), path.toStdString(), session_.state());
    if (!problem.empty()) {
        say(QString::fromStdString(problem), true);
        return false;
    }
    path_ = path;
    session_.markSaved();
    if (autosave_) autosave_->forget();
    showTitle(QFileInfo(path).completeBaseName());
    showUnsaved();
    say("Saved.");
    return true;
}

bool MainWindow::exportObj(const QString& path) {
    rememberFolder(Files::Exports, path);
    const std::expected<sim::Obj, std::string> obj = session_.obj();
    if (!obj) {
        say(QString::fromStdString(obj.error()), true);
        return false;
    }
    // Written beside the file and then put in its place: one that fails leaves the old file.
    // Another program may hold the file for a moment, so a replacing that fails is tried
    // again after growing waits, about half a second in all, as twintailor::save does.
    const auto size = static_cast<qint64>(obj->text.size());
    for (std::chrono::milliseconds wait{1};; wait *= 2) {
        QSaveFile file(path);
        const bool written = file.open(QIODevice::WriteOnly) && file.write(obj->text.data(), size) == size;
        if (written && file.commit()) break;
        if (!written || wait > std::chrono::milliseconds{256}) {
            say(QString("Could not write %1 (%2). Choose another place and export again.").arg(QDir::toNativeSeparators(path), file.errorString()), true);
            return false;
        }
        std::this_thread::sleep_for(wait);
    }
    QString words = QString("Exported %1 %2 to %3.").arg(obj->panels).arg(obj->panels == 1 ? "panel" : "panels", QDir::toNativeSeparators(path));
    if (obj->leftOut > 0) words += QString(" Left out: %1 hidden.").arg(obj->leftOut);
    if (obj->unmeshed > 0) words += QString(" Left out: %1 with no cloth; put its shape right and export again.").arg(obj->unmeshed);
    say(words);
    return true;
}

bool MainWindow::save() {
    if (!takeWhatIsTyped()) return false;
    const QString path = path_.isEmpty() ? QFileDialog::getSaveFileName(this, "Save", inFolder(folderFor(Files::Patterns), title_ + ".twintail"),
                                                                        "Patterns (*.twintail)")
                                         : path_;
    return !path.isEmpty() && saveAs(path);
}

bool MainWindow::mayDiscard(const QString& before) {
    if (!takeWhatIsTyped()) return false;
    if (!session_.unsaved()) return true;
    QMessageBox box(QMessageBox::Question, "Twintailor", QString("Save the changes to %1 before %2?").arg(title_, before),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.button(QMessageBox::Discard)->setText("Don't Save");
    box.setDefaultButton(QMessageBox::Save);
    box.setEscapeButton(QMessageBox::Cancel);
    const int answer = box.exec();
    if (answer == QMessageBox::Save) return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // After an error nothing is saved or thrown away: closing would take the recovery copy with it.
    if (failed_) {
        event->ignore();
        return;
    }
    stopChoosing();
    if (!mayDiscard("closing")) {
        event->ignore();
        return;
    }
    endRecovery();
    QMainWindow::closeEvent(event);
}

bool MainWindow::takeWhatIsTyped() {
    auto* typing = qobject_cast<QLineEdit*>(QApplication::focusWidget());
    // The length bar on the table shows what is typed in it already, and leaving it would cancel it.
    if (!typing || typing == table_->lengthEditor()) return true;
    const std::uint64_t before = problemsSaid_;
    typing->clearFocus();
    return problemsSaid_ == before;
}

// ---- Recovery copies ----------------------------------------------------------

// Each run keeps its copy in a folder of its own, and holds a lock there while
// it runs: a folder whose lock can be taken is of a run that has stopped.
void MainWindow::keepRecoveryIn(const QString& folder, double waitSeconds) {
    const QString own = QDir(folder).absoluteFilePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto lock = std::make_unique<QLockFile>(own + "/running");
    lock->setStaleLockTime(0);  // only a program that has stopped gives its lock up, however old it is
    if (!QDir().mkpath(own) || !lock->tryLock()) {
        say("No recovery copies can be kept: " + own + " cannot be written. Save your work often.", true);
        return;
    }
    recoveryFolder_ = own;
    recoveryLock_ = std::move(lock);
    session_.keepLog(session::actionLogIn(own.toStdString()), versionLine().toStdString());
    session::writeCrashDumpTo(session::crashDumpIn(own.toStdString()));
    autosave_ = std::make_unique<session::Autosave>(own.toStdString(), waitSeconds);
    offerRecovery(folder);
}

// The copies that stopped runs left, the newest first: each is opened or
// deleted, or kept to be offered at the next start. A run that did not end as
// it should gets a crash report first, in "crash-reports" beside the folder.
void MainWindow::offerRecovery(const QString& folder) {
    const QString reports = QDir::cleanPath(folder + "/../crash-reports");
    QString reported;  // where the newest report is, in words
    const auto sayOfReport = [&] {
        if (!reported.isEmpty()) say("Twintailor did not close properly last time. " + reported);
    };
    const QFileInfoList left = QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& run : left) {
        const QString theirs = run.absoluteFilePath();
        if (theirs == recoveryFolder_) continue;
        QLockFile lock(theirs + "/running");
        lock.setStaleLockTime(0);
        if (!lock.tryLock()) continue;  // that run is still going
        const auto remove = [&] {
            lock.unlock();
            QDir(theirs).removeRecursively();
        };
        const std::string stopped = theirs.toStdString();
        const QString when = QFileInfo(QString::fromStdString(session::actionLogIn(stopped))).lastModified().toString("yyyy-MM-dd HH-mm-ss");
        const std::optional<std::string> report = session::keepCrashReport(stopped, reports.toStdString(), when.toStdString());
        if (report && reported.isEmpty())
            reported = QString("A crash report is in %1").arg(QDir::toNativeSeparators(QString::fromStdString(*report)));
        if (!session::hasRecovery(theirs.toStdString())) {
            remove();  // it stopped with nothing unsaved
            continue;
        }
        std::expected<session::Recovery, std::string> copy = session::readRecovery(theirs.toStdString());
        if (!copy) {
            say("A recovery copy cannot be read (" + QString::fromStdString(copy.error()) + "). It is left in " + theirs + ".", true);
            continue;
        }
        const QString title = QString::fromStdString(copy->title);
        QString question = QString("Twintailor stopped before %1 was saved. Open the recovered copy?").arg(title);
        if (!reported.isEmpty()) question += "\n\n" + reported;
        QMessageBox box(QMessageBox::Question, "Twintailor", question, QMessageBox::Open | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);  // so that the report's place can be copied
        box.button(QMessageBox::Discard)->setText("Delete");
        box.button(QMessageBox::Cancel)->setText("Not Now");
        box.setDefaultButton(QMessageBox::Open);
        box.setEscapeButton(QMessageBox::Cancel);
        const int answer = box.exec();
        if (answer == QMessageBox::Discard) {
            remove();
            continue;
        }
        if (answer != QMessageBox::Open) {
            sayOfReport();
            return;
        }
        open(std::move(copy->scene.document), title, std::move(copy->scene.state));
        path_ = QString::fromStdString(copy->path);  // Save writes it to its own file, if it had one
        session_.markUnsaved();
        // Copied again before the old copy goes, so that there is one all along.
        autosave_->write(session_, copy->title, copy->path);
        if (const std::optional<std::string> problem = autosave_->wait()) {
            say(QString::fromStdString(*problem), true);
        } else {
            remove();
            say(reported.isEmpty() ? QString("Recovered. Save to keep it.") : "Recovered. Save to keep it. " + reported);
        }
        showUnsaved();
        return;
    }
    sayOfReport();
}

void MainWindow::finishRecoveryCopy() {
    if (!autosave_) return;
    if (const std::optional<std::string> problem = autosave_->wait()) say(QString::fromStdString(*problem), true);
}

void MainWindow::endRecovery() {
    if (!autosave_) return;
    autosave_->forget();
    autosave_.reset();
    session_.endLog();
    session::writeCrashDumpTo({});
    recoveryLock_.reset();
    QDir(recoveryFolder_).removeRecursively();
}

void MainWindow::cannotGoOn(const QString& error) {
    if (std::exchange(failed_, true)) return;
    timer_->stop();
    try {
        session_.note("Error: " + error.toStdString());
    } catch (const std::exception&) {  // out of memory, most likely: the report goes without the words
    }
    const QString words = QString("Twintailor met an error it cannot go on from, and has to close (%1).\n\n").arg(error) + copyBeforeEnding();
    // The message not at once: the error may have come out of the middle of drawing.
    QTimer::singleShot(0, this, [this, words] {
        auto* box = new QMessageBox(QMessageBox::Critical, "Twintailor", words, QMessageBox::Close, this);
        box->setObjectName("cannotGoOn");
        box->setTextInteractionFlags(Qt::TextSelectableByMouse);  // so that a report can copy the error
        connect(box, &QMessageBox::finished, this, &MainWindow::mustEnd);
        box->open();
    });
}

QString MainWindow::copyBeforeEnding() {
    try {
        if (!session_.unsaved()) return "Nothing was unsaved.";
        if (!autosave_) return "No recovery copy could be kept: the changes since the last save are lost.";
        const std::expected<void, std::string> written = autosave_->writeNow(session_, title_.toStdString(), path_.toStdString());
        if (written) return "Your unsaved work was copied. Start Twintailor again: it offers to open it.";
        return QString("Your unsaved work could not be copied (%1). Start Twintailor again: it offers an earlier copy, if there is one.")
            .arg(QString::fromStdString(written.error()));
    } catch (const std::exception& problem) {
        return QString("Your unsaved work could not be copied (%1). Start Twintailor again: it offers an earlier copy, if there is one.")
            .arg(QString::fromUtf8(problem.what()));
    }
}

void MainWindow::showTitle(const QString& title) {
    title_ = title;
    setWindowTitle(title + "[*]  —  Twintailor");  // Qt shows the "*" while the window is marked modified
}

void MainWindow::showUnsaved() {
    // Only when it changes: Qt hands the title to the system again each time.
    const bool unsaved = session_.unsaved();
    if (isWindowModified() != unsaved) setWindowModified(unsaved);
}

bool MainWindow::loadAvatar(const QString& path, const AvatarImport& readAs) {
    session_.note("Read an avatar file");
    const std::expected<sim::Surface, QString> file = readSurface(path);
    if (!file) {
        say(file.error(), true);
        return false;
    }
    return wear(avatarFrom(*file, path, readAs));
}

void MainWindow::keepSettingsIn(const QString& file) {
    settingsFile_ = file;
    const QSettings settings(file, QSettings::IniFormat);
    for (std::size_t i = 0; i < folders_.size(); ++i)
        if (const QString kept = settings.value(folderKeys[i]).toString(); !kept.isEmpty()) folders_[i] = kept;
    markInstancesAction_->setChecked(settings.value(markInstancesKey, false).toBool());
    showSpace();
}

// How the person likes to look at any pattern, so it is the program's to remember, not the file's.
void MainWindow::markInstances(bool on) {
    markInstancesAction_->setChecked(on);
    if (!settingsFile_.isEmpty()) QSettings(settingsFile_, QSettings::IniFormat).setValue(markInstancesKey, on);
    showSpace();
}

QString MainWindow::folderFor(Files files) const {
    const QString& kept = folders_[static_cast<std::size_t>(files)];
    if (!kept.isEmpty() && QDir(kept).exists()) return kept;
    if (files == Files::Avatars) return assetsFolder();
    return files == Files::Exports ? folderFor(Files::Patterns) : QString();
}

void MainWindow::rememberFolder(Files files, const QString& file) {
    const QFileInfo chosen(file);
    if (!chosen.dir().exists()) return;
    const auto which = static_cast<std::size_t>(files);
    folders_[which] = chosen.absolutePath();
    // A convenience: where it cannot be written, it is remembered for this run alone.
    if (!settingsFile_.isEmpty()) QSettings(settingsFile_, QSettings::IniFormat).setValue(folderKeys[which], folders_[which]);
}

void MainWindow::sayAtOnce(const QString& words) {
    say(words);
    statusBar()->repaint();  // now: the window is about to be busy
}

void MainWindow::chooseAvatar(const QString& path) {
    stopChoosing();
    rememberFolder(Files::Avatars, path);
    sayAtOnce("Reading " + QFileInfo(path).fileName() + "…");
    const Waiting waiting;
    session_.note("Read an avatar file");
    std::expected<sim::Surface, QString> read = readSurface(path);
    if (!read) {
        say(read.error(), true);
        return;
    }
    // Read once: kept while the dialog is open, for the preview and for OK.
    const auto file = std::make_shared<const sim::Surface>(std::move(*read));
    choosing_ = holdTheRest();  // before the dialog is made: what is its own stays usable
    auto* dialog = new AvatarDialog(sim::sizeOf(*file), sim::guessImport(*file), this);
    choosing_->dialog = dialog;
    dialog->setObjectName("avatarImport");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const auto preview = [this, file](const AvatarImport& chosen) {
        session_.previewBody(std::make_shared<const sim::Surface>(sim::imported(*file, sim::standing(*file, chosen))));
        showSpace();
    };
    connect(dialog, &AvatarDialog::changed, this, preview);
    connect(dialog, &QDialog::finished, this, [this, dialog, file, path](int result) {
        const AvatarImport chosen = dialog->choice();
        endChoosing();
        if (result == QDialog::Accepted) wear(avatarFrom(*file, path, sim::standing(*file, chosen)));
    });
    preview(dialog->choice());
    say("The 3D view shows the avatar as you choose it: Alt and the mouse turn the view. OK puts it on.");
    dialog->show();
    // Beside the 3D view, not on it: over the table.
    const QWidget* beside = table_->isVisible() ? static_cast<QWidget*>(table_) : attributes_;
    dialog->move(beside->mapToGlobal(QPoint(24, 24)));
}

MainWindow::ChoosingAvatar MainWindow::holdTheRest() {
    ChoosingAvatar held{.playing = shelf_->simulate()->isChecked(), .layout = layout_};
    shelf_->simulate()->setChecked(false);  // the cloth waits: it is on the body that is on, not the one shown
    if (layout_ == ViewLayout::TableOnly) setViewLayout(ViewLayout::SideBySide);  // the body is seen in 3D
    // The actions too: their keys work wherever the focus is.
    for (QAction* action : findChildren<QAction*>())
        if (action->isEnabled()) {
            action->setEnabled(false);
            held.actions.push_back(action);
        }
    std::vector<QWidget*> rest{menuBar(), table_};
    rest.insert(rest.end(), docks_.begin(), docks_.end());
    for (QToolBar* bar : findChildren<QToolBar*>(Qt::FindDirectChildrenOnly)) rest.push_back(bar);
    for (QWidget* widget : rest)
        if (widget->isEnabled()) {
            widget->setEnabled(false);
            held.widgets.push_back(widget);
        }
    return held;
}

void MainWindow::endChoosing() {
    if (!choosing_) return;
    const ChoosingAvatar was = std::move(*choosing_);
    choosing_.reset();
    for (const QPointer<QWidget>& widget : was.widgets)
        if (widget) widget->setEnabled(true);
    for (const QPointer<QAction>& action : was.actions)
        if (action) action->setEnabled(true);
    if (layout_ != was.layout) setViewLayout(was.layout);
    session_.previewBody(nullptr);
    shelf_->simulate()->setChecked(was.playing);
    say({});
    refresh();
}

void MainWindow::stopChoosing() {
    if (choosing_ && choosing_->dialog) choosing_->dialog->reject();
}

bool MainWindow::wear(std::shared_ptr<const Avatar> avatar) {
    const std::shared_ptr<const Avatar>& worn = session_.document().avatar();
    if (worn && *worn == *avatar) return true;  // the one that is on: nothing to change
    shelf_->simulate()->setChecked(false);      // the cloth starts again on another body
    return answer(session_.run(session::SetAvatar{avatar}));
}

void MainWindow::openSample(std::size_t index) {
    const session::Sample& sample = session::samples().at(index);
    session_.note("Open sample " + std::to_string(index + 1));
    Document document = sample.build();
    // The project's avatars are read as they are: in cm, y up, where they should stand.
    const QString path = assetsFolder() + "/" + sample.avatar;
    const std::expected<sim::Surface, QString> file = readSurface(path);
    if (file) document.setAvatar(avatarFrom(*file, path, {}));
    open(std::move(document), sample.name);
    if (!file) say(file.error(), true);  // opened all the same, on the body that was there
}

void MainWindow::showBody(bool shown) {
    session_.setBodyPresent(shown);
    if (bodyAction_) bodyAction_->setChecked(shown);
    outliner_->showBody(bodyNow());
    tick();
}

void MainWindow::showBodySeeThrough(bool seeThrough) {
    session_.setBodySeeThrough(seeThrough);
    if (seeThroughAction_) seeThroughAction_->setChecked(seeThrough);
    outliner_->showBody(bodyNow());
    tick();
}

void MainWindow::showSpace() {
    if (!space_->isVisible()) return;
    // While an avatar is chosen: no gizmo and nothing marked, and the new body is solid.
    const bool choosing = choosing_.has_value();
    space_->show(session_, choosing ? Tool::Select : drafting_.tool,
                 {.selected = choosing ? std::nullopt : drafting_.selection.panel,
                  .body = session_.bodyPresent(),
                  .mesh = shelf_->mesh()->isChecked(),
                  .seam = choosing ? std::nullopt : drafting_.selection.seam,
                  .seeThroughBody = !choosing && session_.bodySeeThrough(),
                  .markInstances = markInstancesAction_->isChecked()});
}

void MainWindow::setThreading(session::Threading threading) {
    session_.setThreading(threading);
    shelf_->simulate()->setChecked(false);
}

Outliner::Body MainWindow::bodyNow() const {
    const std::shared_ptr<const Avatar>& avatar = session_.document().avatar();
    return {.name = avatar ? QString::fromStdString(avatar->name) : QString(),
            .shown = session_.bodyPresent(),
            .ready = session_.bodyReady(),
            .seeThrough = session_.bodySeeThrough()};
}

void MainWindow::startPicking(OpId layer, ReferenceId reference) {
    picking_ = Picking{layer, reference};
    useTool(Tool::Select);
    refresh();
}

void MainWindow::select(std::optional<PanelId> panel) {
    if (panel && !session_.document().contains(*panel)) panel.reset();
    if (panel == drafting_.selection.panel && !drafting_.selection.seam) return;
    drafting_.handle(session_, session::SelectPanel{panel});
    refresh();
}

void MainWindow::selectSeam(std::optional<SeamId> seam) {
    if (seam && !session_.document().contains(*seam)) seam.reset();
    if (seam == drafting_.selection.seam) return;
    drafting_.handle(session_, session::SelectSeam{seam});
    refresh();
}

void MainWindow::setTicking(bool running) {
    if (running) timer_->start(session::FrameTimes::tickMilliseconds(screen()->refreshRate()));
    else timer_->stop();
}

int MainWindow::tickMilliseconds() const { return timer_->interval(); }

// Once for each picture of the screen the window is on now: it may have been
// moved to another screen, or the screen's rate changed, since the last tick.
void MainWindow::paceTicks() {
    const int every = session::FrameTimes::tickMilliseconds(screen()->refreshRate());
    if (timer_->isActive() && timer_->interval() != every) timer_->setInterval(every);
}

void MainWindow::tick() {
    if (failed_) return;
    QElapsedTimer took;
    took.start();
    const bool meshShown = shelf_->mesh()->isChecked();
    if (const std::optional<std::string> problem = session_.update(space_->isVisible() || meshShown)) say(QString::fromStdString(*problem), true);
    // A cloth that went wrong, or a Reset that was redone, has stopped it: Play shows that.
    if (!session_.simulating()) shelf_->simulate()->setChecked(false);
    table_->setMesh(meshShown ? session_.mesh() : nullptr);
    if (session_.bodyReady() && helpLine() == waitingHint) say(shelf_->simulate()->isChecked() ? playingHint : "");
    if (session_.editor().version() != shownVersion_) refresh();
    showSpace();
    space_->showTime(session_.clothShown() ? std::optional<double>(session_.cloth().time) : std::nullopt, session_.clothShown() && session_.cloth().sewing);
    showPreparing(session_.bodyProgress());
    outliner_->showBody(bodyNow());
    outliner_->showPanelStates(session_.hiddenPanels(), session_.inactivePanels());
    outliner_->showSeamStates(session_.hiddenSeams());
    showMeshCounts();
    attributes_->showOpenSeams(session_.cloth().open);
    if (autosave_)
        if (const std::optional<std::string> problem =
                autosave_->update(session_, static_cast<double>(clock_.nsecsElapsed()) / 1e9, title_.toStdString(), path_.toStdString()))
            say(QString::fromStdString(*problem), true);
    showUnsaved();  // the cloth moves without a change of the document
    frameTimes_.took(session::FrameTimes::Work::Tick, static_cast<double>(took.nsecsElapsed()) / 1e6);
    showFrameTimes();
    paceTicks();
}

void MainWindow::showPreparing(std::optional<double> done) {
    // Only what changes: it is called every frame.
    if (done) {
        const int percent = static_cast<int>(*done * 100.0);
        if (preparing_->value() != percent) preparing_->setValue(percent);
    }
    if (preparing_->isHidden() == done.has_value()) preparing_->setVisible(done.has_value());
}

void MainWindow::showFrameTimes() {
    if (frameTimesLine_->isHidden()) return;
    const double now = static_cast<double>(clock_.nsecsElapsed()) / 1e6;
    if (frameTimesShownAt_ && now - *frameTimesShownAt_ < 250.0) return;
    frameTimesShownAt_ = now;
    std::optional<session::FrameTimes::Cloth> cloth;
    if (session_.clothShown()) {
        std::size_t particles = 0;
        for (const sim::PanelMesh& panel : session_.cloth().mesh->panels) particles += panel.points.size();
        cloth = session::FrameTimes::Cloth{.stepMilliseconds = session_.cloth().stepMilliseconds,
                                           .particles = particles,
                                           .running = session_.simulating(),
                                           .sewing = session_.cloth().sewing};
    }
    frameTimesLine_->setText(QString::fromStdString(frameTimes_.text(cloth)));
    frameTimesLine_->setToolTip(frameTimesLine_->text());
}

// What the selected panel's cloth came to. Worked out again only when a new
// mesh was made or another panel was chosen, not every frame.
void MainWindow::showMeshCounts() {
    const std::optional<PanelId> panel = drafting_.selection.panel;
    if (session_.meshesMade() == countedMesh_ && panel == countedPanel_) return;
    countedMesh_ = session_.meshesMade();
    countedPanel_ = panel;
    std::optional<AttributeEditor::MeshCounts> counts;
    const sim::ClothMesh& mesh = *session_.mesh();
    for (std::size_t i = 0; panel && i < mesh.panels.size(); ++i) {
        if (!(mesh.panels[i].panel == *panel)) continue;
        counts = AttributeEditor::MeshCounts{.particles = mesh.panels[i].points.size(), .triangles = mesh.panels[i].triangles.size()};
        for (const sim::Stitch& stitch : mesh.stitches) counts->stitches += stitch.panelA == i || stitch.panelB == i;
    }
    attributes_->showMesh(counts);
}

bool MainWindow::answer(const std::expected<void, std::string>& done, ValueField* field, const QElapsedTimer* since) {
    if (!done) {
        const QString why = QString::fromStdString(done.error());
        say("Not changed: " + why, true);
        const QPointer<ValueField> marked = field;
        refresh();  // first: showing a field takes its mark off
        if (marked) marked->refuse(why);
        return false;
    }
    say({});
    refresh();
    if (since) frameTimes_.took(session::FrameTimes::Work::Change, static_cast<double>(since->nsecsElapsed()) / 1e6);
    return true;
}

void MainWindow::typed(OpId layer, FieldId id, const QString& text, ValueField* field) {
    if (!session_.document().contains(layer)) return;
    const Unit unit = fields(session_.document().get(layer))[id.index].unit;
    if (const std::optional<Expr> value = read(text, unit, field)) answer(session_.run(session::SetField{session::OperationField{layer, id}, *value}), field);
}

void MainWindow::typed(const session::PanelField& place, const QString& text, ValueField* field) {
    if (!session_.document().contains(place.panel)) return;
    if (const std::optional<Expr> value = read(text, field->unit(), field)) answer(session_.run(session::SetField{place, *value}), field);
}

std::optional<Expr> MainWindow::read(const QString& text, Unit unit, ValueField* field) {
    const std::variant<Expr, TextError> value = parseValue(text.toStdString(), unit, session_.document());
    if (const TextError* problem = std::get_if<TextError>(&value)) {
        const QString why = QString::fromStdString(problem->message);
        say(why, true);
        if (field) field->refuse(why);
        return std::nullopt;
    }
    return std::get<Expr>(value);
}

void MainWindow::refresh() {
    const Document& doc = session_.document();
    if (!gesturing_) closeLengthIfOver();  // (a gesture that took the length away opens the bar again on another)
    const Result& shapes = session_.editor().shapes();
    // Undo or redo of another avatar ends the cloth, or brings back the one that hung on it: it stands still then.
    if (doc.avatar() != shownAvatar_) {
        shownAvatar_ = doc.avatar();
        shelf_->simulate()->setChecked(false);
    }
    std::optional<PanelId>& selected = drafting_.selection.panel;
    if (selected && !doc.contains(*selected)) drafting_.forget(doc);
    std::optional<SeamId>& seam = drafting_.selection.seam;
    if (seam && !doc.contains(*seam)) seam.reset();
    outliner_->show(doc, shapes, selected, bodyNow(), seam);
    exportAction_->setEnabled(session_.hasGarment());
    for (QAction* onPanel : {shelf_->instance(), shelf_->mirroredInstance(), shelf_->bake(), shelf_->placeFromTable()}) onPanel->setEnabled(selected.has_value());
    const std::vector<PanelId> chosen = drafting_.selection.panels();
    shelf_->sewAsSource()->setEnabled(std::any_of(chosen.begin(), chosen.end(), [&](PanelId panel) { return doc.contains(panel) && doc.instanceOf(panel); }));
    parameters_->show(doc, shapes);
    // Looking back only at a step of the selected panel.
    if (viewUntil_ && (!selected || !doc.contains(*viewUntil_) || !(doc.panelOf(*viewUntil_) == *selected))) viewUntil_.reset();
    showAllSteps_->setEnabled(viewUntil_.has_value());
    if (viewUntil_) past_ = evaluateUpTo(doc, *viewUntil_);
    attributes_->show(doc, shapes, selected, viewUntil_);
    table_->show(doc, viewUntil_ ? past_ : shapes, selected, drafting_.selection.corners, drafting_.tool, drafting_.selection.others, seam);
    showSpace();
    // A layer's reference being chosen: offer what that layer could use.
    if (picking_ && (!doc.contains(picking_->operation))) picking_.reset();
    std::optional<TableView::Choice> choice;
    if (picking_) {
        const std::vector<ReferenceField> uses = references(doc.get(picking_->operation));
        if (picking_->reference.index < uses.size() && !std::holds_alternative<PanelId>(uses[picking_->reference.index].value)) {
            const Reference& slot = uses[picking_->reference.index].value;
            const auto kind = std::holds_alternative<EdgeRef>(slot)     ? TableView::Choice::Kind::Edge
                              : std::holds_alternative<CornerRef>(slot) ? TableView::Choice::Kind::Corner
                                                                        : TableView::Choice::Kind::Line;
            choice = TableView::Choice{doc.panelOf(picking_->operation), kind, outlineBefore(doc, picking_->operation)};
            const std::size_t offered = kind == TableView::Choice::Kind::Edge     ? choice->candidates.edges.size()
                                        : kind == TableView::Choice::Kind::Corner ? choice->candidates.vertices.size()
                                                                                  : choice->candidates.lines.size();
            const char* what = kind == TableView::Choice::Kind::Edge ? "an edge" : kind == TableView::Choice::Kind::Corner ? "a corner" : "a line";
            if (offered == 0)
                say(kind == TableView::Choice::Kind::Line ? "This panel has no line to choose yet. Draw one with the Line tool first. Esc cancels."
                                                          : "There is nothing to choose on this panel at this point. Esc cancels.",
                    true);
            else
                say(QString("Click ") + what + " on the highlighted panel. Esc cancels.");
        } else {
            picking_.reset();
        }
    }
    table_->choose(choice);
    if (choice) table_->setFocus();  // so Esc reaches it
    shelf_->undo()->setEnabled(session_.editor().canUndo());
    shelf_->redo()->setEnabled(session_.editor().canRedo());
    if (bodyAction_) bodyAction_->setChecked(session_.bodyPresent());  // as the file that was opened has it
    if (seeThroughAction_) seeThroughAction_->setChecked(session_.bodySeeThrough());
    showUnsaved();
    shownVersion_ = session_.editor().version();
}

void MainWindow::previewLength(OpId layer, FieldId id, const QString& text) {
    if (!session_.document().contains(layer)) return;
    if (text == lengthText_) {  // back to what is applied: whatever was said against what was typed since is over
        table_->showLengthProblem({});
        return;
    }
    const Unit unit = fields(session_.document().get(layer))[id.index].unit;
    const std::variant<Expr, TextError> parsed = parseValue(text.toStdString(), unit, session_.document());
    if (const TextError* problem = std::get_if<TextError>(&parsed)) {
        table_->showLengthProblem(QString::fromStdString(problem->message));
        return;
    }
    const Expr value = std::get<Expr>(parsed);
    // A preview: what is typed so far takes the place of what was typed before.
    const auto done = session_.preview(drafting_.typingIn(session_),
                                       session::SetField{session::OperationField{layer, id}, value, session::Grows::InPlaceUnlessSewn});
    if (!done) {
        table_->showLengthProblem(QString::fromStdString(done.error()));  // the document is as it was
        return;
    }
    table_->showLengthProblem({});
    lengthText_ = text;
    refresh();
}

void MainWindow::closeLengthIfOver() {
    const auto& typing = table_->lengthBeingTyped();
    if (!typing || (session_.document().contains(typing->operation) && !drafting_.typingEnded(session_))) return;
    table_->closeLength();
    drafting_.commitTyping(session_);
}

void MainWindow::cancelLength() {
    // All of the edit goes, and is forgotten: it must not be left to redo.
    drafting_.cancelTyping(session_);
    // A length moves corners and makes none, so the corners that are chosen stay chosen: the click that
    // left the bar may be the one that adds to them.
    const std::vector<CornerRef> corners = drafting_.selection.corners;
    drafting_.forget(session_.document());
    drafting_.selection.corners = corners;
    table_->cancel();
    say({});
    refresh();
}

QString MainWindow::lengthStatus(const session::Outcome::LengthField& what) const {
    if (!what.keeping) return QString::fromStdString("Type the " + what.label + ". Enter applies, Esc cancels.");
    const QString word = table_->anchorWord(*what.keeping);
    return (*what.keeping == SetLength::Keeping::Middle ? QString("Midpoint fixed · both ends move") : word + " end fixed · the other end moves") + ". Click another anchor to change it. Enter applies, Esc cancels.";
}

void MainWindow::on(const session::Gesture& gesture) {
    // Looking back at an earlier step, the pattern does not change; placing panels in space still does.
    const bool placesInSpace = std::holds_alternative<session::MoveInSpace>(gesture) || std::holds_alternative<session::TurnInSpace>(gesture);
    const bool changesNothing = std::holds_alternative<session::SelectPanel>(gesture) || std::holds_alternative<session::SelectPanels>(gesture) ||
                                std::holds_alternative<session::CopySelection>(gesture);
    if (viewUntil_ && !changesNothing && !placesInSpace) {
        table_->cancel();
        say("Showing an earlier step: press Escape to show all steps and change the pattern again.", true);
        return;
    }
    struct Running {
        bool& flag;
        explicit Running(bool& f) : flag(f) { flag = true; }
        ~Running() { flag = false; }
    } running(gesturing_);
    const bool lengthGesture = std::holds_alternative<session::EditLength>(gesture) || std::holds_alternative<session::UseShapeLength>(gesture);
    QElapsedTimer took;  // a drag's every step is a change, shown again in every dock
    took.start();
    const session::Outcome done = drafting_.handle(session_, gesture);
    if (!done.notice.empty()) say(QString::fromStdString(done.notice), done.refused);
    else if (std::holds_alternative<session::MoveCorner>(gesture) || std::holds_alternative<session::MovePanel>(gesture)) say({});
    shelf_->setTool(drafting_.tool);  // a drawing tool that has done its work is back to Select
    refresh();
    frameTimes_.took(session::FrameTimes::Work::Change, static_cast<double>(took.nsecsElapsed()) / 1e6);
    if (done.typeLength) {
        // The length is a field of a layer, but it is typed on the table, where the edge is: the
        // Attribute Editor is the back end of this, not the way to work.
        const session::Outcome::LengthField& wanted = *done.typeLength;
        const Field field = fields(session_.document().get(wanted.operation))[wanted.field.index];
        const std::optional<double> number = asNumber(field.value);
        const std::string now = number ? formatValue(*number, field.unit) : "= " + formatExpr(field.value, session_.document());
        lengthText_ = QString::fromStdString(now);
        table_->typeLength(wanted, lengthText_);
        if (done.notice.empty()) say(lengthStatus(wanted));  // a refusal's words stay
    } else if (lengthGesture && !table_->lengthEditor()) {
        drafting_.commitTyping(session_);  // nothing to type: the gesture was refused, or has nothing to say
    }
    gesturing_ = false;
    closeLengthIfOver();  // a bar left for a length that this gesture took away, or for one before it
}

// Delete, Ctrl+C, Ctrl+V and Ctrl+D typed in the 3D view do what they do on the
// table, to the selected panels. The 3D view shows no corners, so Delete takes
// whole panels; Paste has no place on the table to use, so it goes beside the originals.
bool MainWindow::editInSpace(QKeyCombination pressed) {
    const QKeySequence typed(pressed);
    if (pressed == QKeyCombination(Qt::Key_Delete) || pressed == QKeyCombination(Qt::Key_Backspace)) {
        on(session::DeleteSelection{.wholePanels = true});  // says so when nothing is selected
    } else if (typed == QKeySequence(QKeySequence::Copy)) {
        on(session::CopySelection{});
    } else if (typed == QKeySequence(QKeySequence::Paste)) {
        on(session::PastePanel{std::nullopt});
    } else if (pressed == QKeyCombination(Qt::ControlModifier, Qt::Key_D)) {
        on(session::DuplicateSelection{});
    } else {
        return false;
    }
    return true;
}

void MainWindow::useTool(Tool tool) {
    drafting_.tool = tool;
    shelf_->setTool(tool);
    refresh();
}

void MainWindow::stopPicking() {
    if (!picking_) return;
    picking_.reset();
    table_->choose(std::nullopt);
}

void MainWindow::say(const QString& words, bool problem) {
    if (problem) ++problemsSaid_;
    help_->setText(words);
    help_->setToolTip(words);
    help_->setStyleSheet(QString("color: %1").arg((problem ? theme::error : theme::text).name()));
}

QString MainWindow::helpLine() const { return help_->text(); }

QDockWidget* MainWindow::dock(const QString& name) const {
    for (QDockWidget* d : docks_)
        if (d->windowTitle() == name) return d;
    return nullptr;
}

void MainWindow::setViewLayout(ViewLayout layout) {
    layout_ = layout;
    shelf_->layout(layout)->setChecked(true);
    views_->setOrientation(layout == ViewLayout::Stacked ? Qt::Vertical : Qt::Horizontal);
    space_->setVisible(layout != ViewLayout::TableOnly);
    table_->setVisible(layout != ViewLayout::SpaceOnly);
    if (layout == ViewLayout::SideBySide || layout == ViewLayout::Stacked) views_->setSizes({1, 1});  // shared in half again
}

} // namespace twintailor::ui
