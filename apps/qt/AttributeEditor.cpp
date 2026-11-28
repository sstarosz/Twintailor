#include "AttributeEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include "Icons.h"
#include "Lists.h"
#include "Section.h"
#include "ValueField.h"
#include "twintailor/core/Text.h"

namespace twintailor::ui {

namespace {

// A row that is being replaced may be in the middle of its own signal: a list
// reporting a pick goes on to use itself once the slot has returned, and the
// slot is what made the rows again. So the old row is hidden and let go now,
// and deleted when the signal is over.
void retire(QWidget* row) {
    if (!row) return;
    row->hide();
    row->setParent(nullptr);
    row->deleteLater();
}

Icon iconOf(const OpData& op) {
    return std::visit([](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, Rect>) return Icon::Rectangle;
        else if constexpr (std::is_same_v<T, Circle>) return Icon::Circle;
        else if constexpr (std::is_same_v<T, RingSector>) return Icon::Ring;
        else if constexpr (std::is_same_v<T, Instance>) return Icon::Instance;
        else if constexpr (std::is_same_v<T, Offcut>) return Icon::Split;
        else if constexpr (std::is_same_v<T, Curve> || std::is_same_v<T, Bezier>) return Icon::Curve;
        else if constexpr (std::is_same_v<T, Line> || std::is_same_v<T, LineBetween>) return Icon::Line;
        else if constexpr (std::is_same_v<T, Cut>) return Icon::Cut;
        else if constexpr (std::is_same_v<T, Mirror>) return Icon::Mirror;
        else if constexpr (std::is_same_v<T, AddPoint>) return Icon::Point;
        else if constexpr (std::is_same_v<T, Scale>) return Icon::Scale;
        else if constexpr (std::is_same_v<T, SetLength>) return Icon::Lengths;  // an edge with its ends marked
        else return Icon::Select;  // AdjustPoint: a corner moved
    }, op);
}

bool appliedLast(const OpData& op) {
    const Mirror* mirror = std::get_if<Mirror>(&op);
    return mirror && mirror->appliedLast;
}

// A layer's value at a glance: its first field with its unit.
QString summaryOf(const OpData& op, const Document& doc, const Result& shapes) {
    if (appliedLast(op)) return "on top";
    const std::vector<Field> all = fields(op);
    if (all.empty()) return {};
    const std::optional<double> value = valueOf(all[0].value, shapes);
    if (!value) return "?";
    QString text = QString::fromStdString(formatValue(*value, all[0].unit));
    if (all.size() == 2 && all[0].unit == Unit::Length && all[1].unit == Unit::Length && std::string_view(label(op)) == "Rectangle")
        if (const std::optional<double> second = valueOf(all[1].value, shapes))
            text = QString::fromStdString(formatValue(*value, Unit::None) + " × " + formatValue(*second, Unit::Length));
    (void)doc;
    return text;
}

// The colour of a layer's summary: a value set by a formula is in the formula colour.
QString summaryStyleOf(const OpData& op) {
    const bool driven = !fields(op).empty() && !asNumber(fields(op)[0].value);
    return QString("color: %1").arg((driven ? theme::formula : theme::quiet).name());
}

// What a layer uses and chooses, in words: when it changes (undo, a choice),
// the rows are made again so that they show it.
std::string choicesOf(const OpData& op, const Document& doc) {
    std::string words;
    for (const ReferenceField& reference : references(op)) words += "|" + nameOf(reference.value, doc);
    if (const Cut* cut = std::get_if<Cut>(&op)) words += cut->keep == Cut::Side::Left ? "|L" : "|R";
    if (const SetLength* set = std::get_if<SetLength>(&op)) words += "|keeps" + std::to_string(static_cast<int>(set->keeping));
    if (const Mirror* mirror = std::get_if<Mirror>(&op)) words += mirror->appliedLast ? "|last" : "|inplace";
    if (const Instance* instance = std::get_if<Instance>(&op)) words += instance->mirrored ? "|mirrored" : "|plain";
    return words;
}

// A layer's card: lighter when open, and outlined brighter while the panel is shown as of that step.
QString cardStyle(const QString& selector, bool open, bool lookedBackTo) {
    return QString("%1 { background: %2; border: 1px solid %3; border-radius: 6px; }")
        .arg(selector, open ? theme::header.name() : theme::panel.name(), (lookedBackTo ? theme::cardEdgeViewed : open ? theme::cardEdgeOpen : theme::cardEdge).name());
}

QToolButton* smallButton(Icon picture, QColor colour, const QString& tip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setIcon(icon(picture, colour));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

} // namespace

AttributeEditor::AttributeEditor(QWidget* parent) : QScrollArea(parent) {
    setObjectName("attributeEditor");
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    page_ = new QWidget(this);
    auto* column = new QVBoxLayout(page_);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // Where it is in space: lengths in cm, turns in degrees.
    spaceSection_ = new Section("In space", page_);
    auto* space = new QGridLayout(spaceSection_->content());
    space->setContentsMargins(8, 6, 8, 6);
    space->setSpacing(4);
    // As in Maya: one row per attribute, its name on the left and its numbers
    // side by side, without saying which axis each is (the tooltip does).
    const char* spaceRows[] = {"Position", "Rotation", "Wrap"};
    for (int row = 0; row < 3; ++row) {
        auto* label = new QLabel(spaceRows[row], spaceSection_->content());
        label->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
        space->addWidget(label, row, 0);
    }
    space->setColumnStretch(1, 1);
    space->setColumnStretch(2, 1);
    space->setColumnStretch(3, 1);
    // Position x y z, rotation turn tilt roll, wrap alone; a pull sideways changes half a
    // centimetre or a degree a pixel.
    struct InSpace {
        session::SpaceField field;
        const char* tip;
        Unit unit;
        double dragStep;
        int row, place;
    };
    using session::SpaceField;
    for (const InSpace& in : {InSpace{SpaceField::X, "x", Unit::Length, 0.5, 0, 0}, InSpace{SpaceField::Y, "y", Unit::Length, 0.5, 0, 1},
                              InSpace{SpaceField::Z, "z", Unit::Length, 0.5, 0, 2}, InSpace{SpaceField::Turn, "Turn (about y)", Unit::Angle, 1.0, 1, 0},
                              InSpace{SpaceField::Tilt, "Tilt", Unit::Angle, 1.0, 1, 1}, InSpace{SpaceField::Roll, "Roll", Unit::Angle, 1.0, 1, 2},
                              InSpace{SpaceField::Wrap, "Wrap", Unit::Angle, 1.0, 2, 0}}) {
        auto* field = new ValueField(in.unit, spaceSection_->content());
        field->setToolTip(in.tip);
        space->addWidget(field, in.row, 1 + in.place);
        space_.push_back(field);
        field->setDragStep(in.dragStep);
        connect(field, &ValueField::typed, this, [this, which = in.field](QString text) {
            if (panel_) emit spaceTyped(*panel_, which, text);
        });
        connect(field, &ValueField::dragged, this, [this, which = in.field](double value, session::DragPhase phase) {
            if (panel_) emit spaceDragged(*panel_, which, value, phase);
        });
    }
    spaceSection_->setOpen(false);
    column->addWidget(spaceSection_);

    tableSection_ = new Section("On the table", page_);
    auto* table = new QGridLayout(tableSection_->content());
    table->setContentsMargins(8, 6, 8, 6);
    table->setSpacing(4);
    for (int row = 0; row < 2; ++row) {
        auto* label = new QLabel(row == 0 ? "Position" : "Rotation", tableSection_->content());
        label->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
        table->addWidget(label, row, 0);
    }
    table->setColumnStretch(1, 1);
    table->setColumnStretch(2, 1);
    table->setColumnStretch(3, 1);
    struct OnTable {
        session::TableField field;
        const char* tip;
        Unit unit;
        double dragStep;
        int row, place;
    };
    using session::TableField;
    for (const OnTable& on : {OnTable{TableField::X, "x", Unit::Length, 0.5, 0, 0}, OnTable{TableField::Y, "y", Unit::Length, 0.5, 0, 1},
                              OnTable{TableField::Angle, "Angle", Unit::Angle, 1.0, 1, 0}}) {
        auto* field = new ValueField(on.unit, tableSection_->content());
        field->setToolTip(on.tip);
        table->addWidget(field, on.row, 1 + on.place);
        table_.push_back(field);
        field->setDragStep(on.dragStep);
        connect(field, &ValueField::typed, this, [this, which = on.field](QString text) {
            if (panel_) emit tableTyped(*panel_, which, text);
        });
        connect(field, &ValueField::dragged, this, [this, which = on.field](double value, session::DragPhase phase) {
            if (panel_) emit tableDragged(*panel_, which, value, phase);
        });
    }
    tableSection_->setOpen(false);
    column->addWidget(tableSection_);

    // How fine this panel's cloth is. What the mesh came to is shown beneath.
    particlesSection_ = new Section("Particles", page_);
    auto* particles = new QGridLayout(particlesSection_->content());
    particles->setContentsMargins(8, 6, 8, 6);
    particles->setSpacing(4);
    auto* distanceLabel = new QLabel("Distance", particlesSection_->content());
    distanceLabel->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
    particles->addWidget(distanceLabel, 0, 0);
    distance_ = new ValueField(Unit::Length, particlesSection_->content());
    distance_->setObjectName("particleDistance");
    distance_->setToolTip("Roughly how far apart this panel's cloth particles are. Smaller is finer and slower. Where a seam joins panels of different distances, the finer one is used.");
    distance_->setDragStep(0.05);
    particles->addWidget(distance_, 0, 1);
    particles->setColumnStretch(1, 1);
    connect(distance_, &ValueField::typed, this, [this](QString text) {
        if (panel_) emit particleDistanceTyped(*panel_, text);
    });
    connect(distance_, &ValueField::dragged, this, [this](double value, session::DragPhase phase) {
        if (panel_) emit particleDistanceDragged(*panel_, value, phase);
    });
    meshCounts_ = new QLabel(particlesSection_->content());
    meshCounts_->setObjectName("meshCounts");
    meshCounts_->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
    particles->addWidget(meshCounts_, 1, 0, 1, 2);
    particlesSection_->setOpen(false);
    column->addWidget(particlesSection_);

    seamsSection_ = new Section("Seams", page_);
    seamRows_ = new QVBoxLayout(seamsSection_->content());
    seamRows_->setContentsMargins(8, 6, 8, 6);
    seamRows_->setSpacing(6);
    column->addWidget(seamsSection_);

    // The layers come last: they are what grows as the panel is worked on.
    layersSection_ = new Section("Layers", page_);
    auto* layersColumn = new QVBoxLayout(layersSection_->content());
    layersColumn->setContentsMargins(0, 0, 0, 0);
    layersColumn->setSpacing(0);
    // Operations that have no tool, or that are easier to add from a list: a
    // shape for an empty panel, then changes to it.
    auto* add = new QToolButton(layersSection_->content());
    add->setObjectName("addOperation");
    add->setText("Add operation");
    add->setIcon(icon(Icon::Plus));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setAutoRaise(true);
    add->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(add);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        for (const session::Offered& offer : offers_) {
            QAction* action = menu->addAction(QString(offer.name) + "   —   " + offer.hint);
            const session::Offer which = offer.offer;
            connect(action, &QAction::triggered, this, [this, which] { addOperation(which); });
        }
    });
    add->setMenu(menu);
    layersColumn->addWidget(add, 0, Qt::AlignLeft);
    // The layers scroll on their own, so that a long list does not push the rest away.
    layerScroll_ = new QScrollArea(layersSection_->content());
    layerScroll_->setObjectName("layerScroll");
    layerScroll_->setWidgetResizable(true);
    layerScroll_->setFrameShape(QFrame::NoFrame);
    layerScroll_->setMinimumHeight(240);
    layerPage_ = new QWidget(layerScroll_);
    auto* pageColumn = new QVBoxLayout(layerPage_);
    pageColumn->setContentsMargins(4, 6, 4, 6);
    pageColumn->setSpacing(0);
    layerRows_ = new QVBoxLayout();
    layerRows_->setContentsMargins(0, 0, 0, 0);
    layerRows_->setSpacing(8);
    pageColumn->addLayout(layerRows_);
    pageColumn->addStretch(1);
    layerScroll_->setWidget(layerPage_);
    layersColumn->addWidget(layerScroll_, 1);
    column->addWidget(layersSection_, 1);
    setWidget(page_);
}

void AttributeEditor::show(const Document& doc, const Result& shapes, std::optional<PanelId> panel, std::optional<OpId> viewed) {
    if (panel && !doc.contains(*panel)) panel.reset();
    if (panel != panel_) closed_.clear();
    panel_ = panel;
    viewed_ = viewed;
    offers_ = panel ? session::offersFor(doc, *panel) : std::vector<session::Offered>{};
    page_->setVisible(panel.has_value());
    if (!panel) return;
    const twintailor::Panel& p = doc.get(*panel);  // QFrame has a Panel of its own
    rebuildLayers(doc, shapes);

    const Arrangement& in = p.arrangement;
    const double space[] = {in.at.x, in.at.y, in.at.z, in.turn, in.tilt, in.roll, in.wrap};
    for (std::size_t i = 0; i < space_.size(); ++i) space_[i]->show(Expr(space[i]), doc, shapes);
    const double table[] = {p.at.x, p.at.y, p.angle};
    for (std::size_t i = 0; i < table_.size(); ++i) table_[i]->show(Expr(table[i]), doc, shapes);
    // A base for instances is not cut from cloth: it has no particles.
    particlesSection_->setVisible(!p.construction);
    distance_->show(Expr(p.particleDistance.value_or(defaultParticleDistance)), doc, shapes);

    rebuildSeams(doc, shapes);
}

void AttributeEditor::showMesh(std::optional<MeshCounts> counts) {
    meshCounts_->setText(counts ? QString("%1 particles · %2 triangles · %3 stitches").arg(counts->particles).arg(counts->triangles).arg(counts->stitches) : QString());
}

QString AttributeEditor::meshShown() const { return meshCounts_->text(); }

void AttributeEditor::showOpenSeams(const std::vector<sim::OpenSeam>& open) {
    if (open == openSeams_) return;
    openSeams_ = open;
    sayOpenSeams();
}

void AttributeEditor::sayOpenSeams() {
    for (std::size_t i = 0; i < seamCards_.size() && i < seams_.size(); ++i) {
        QLabel* label = seamCards_[i].open;
        if (!label) continue;
        QString words;
        for (const sim::OpenSeam& open : openSeams_)
            if (open.seam == seams_[i]) {
                const double rounded = std::round(open.widest * 10.0) / 10.0;  // a tenth of a cm: it settles quietly
                words = QString::fromStdString("Left open: " + formatValue(rounded, Unit::Length) + " apart. Move the panels closer, then Reset.");
            }
        if (label->text() != words) label->setText(words);
        label->setVisible(!words.isEmpty());
    }
}

QString AttributeEditor::openShown(SeamId seam) const {
    for (std::size_t i = 0; i < seamCards_.size() && i < seams_.size(); ++i)
        if (seams_[i] == seam && seamCards_[i].open && !seamCards_[i].open->isHidden()) return seamCards_[i].open->text();
    return {};
}

// The seams that touch the panel, one small card each: the panel it is sewn
// to, both lengths and what is gathered, and a button that twists it. Which
// edges are sewn is in the card's tooltip; removing a seam is the Outliner's.
void AttributeEditor::rebuildSeams(const Document& doc, const Result& shapes) {
    std::vector<SeamId> mine;
    for (SeamId id : doc.seams())
        if (doc.get(id).a.panel == *panel_ || doc.get(id).b.panel == *panel_) mine.push_back(id);
    const auto sewnOf = [&](SeamId id) -> const SeamResult* {
        for (const SeamResult& r : shapes.seams)
            if (r.seam == id) return &r;
        return nullptr;
    };
    const auto edgeName = [&](const SeamSide& side) {
        const std::string panel = doc.contains(side.panel) ? doc.get(side.panel).name : std::string("a removed panel");
        return QString::fromStdString(nameOf(Reference(side.edge), doc) + " on " + panel);
    };
    // The words each card shows; the cards are made again only when they change.
    struct Shown {
        SeamId id;
        QString partner, lengths, gathered, edges;
        bool broken, reversed;
        bool uneven = false;  // the edges differ in length: one side is gathered
    };
    std::vector<Shown> rows;
    std::string signature = std::to_string(panel_->value);
    for (SeamId id : mine) {
        const Seam& seam = doc.get(id);
        const bool swap = !(seam.a.panel == *panel_);  // this panel's edge first
        const SeamSide& here = swap ? seam.b : seam.a;
        const SeamSide& there = swap ? seam.a : seam.b;
        const SeamResult* sewn = sewnOf(id);
        Shown row{.id = id,
                  .partner = there.panel == *panel_   ? QString("Itself")
                             : doc.contains(there.panel) ? QString::fromStdString(doc.get(there.panel).name)
                                                         : QString("A removed panel"),
                  .edges = edgeName(here) + "\nsewn to " + edgeName(there),
                  .broken = sewn && !sewn->sewn(),
                  .reversed = seam.reversed};
        if (row.broken) {
            row.lengths = QString::fromStdString("Broken: " + sewn->problem + ".");
            row.gathered = "It comes back if the edge does.";
        } else if (sewn) {
            const double mineLong = swap ? sewn->lengthB : sewn->lengthA, otherLong = swap ? sewn->lengthA : sewn->lengthB;
            if (std::abs(mineLong - otherLong) < 0.05) {
                row.lengths = QString::fromStdString(formatValue(mineLong, Unit::Length));
                row.gathered = "even";
            } else {
                row.lengths = QString::fromStdString(formatValue(mineLong, Unit::Length) + "  →  " + formatValue(otherLong, Unit::Length));
                row.gathered = QString::fromStdString(formatValue(std::abs(mineLong - otherLong), Unit::Length) + " gathered");
                row.uneven = true;
            }
        }
        // The numbers are not in it: a card whose only change is its numbers is set in place.
        signature += "," + std::to_string(id.value) + row.partner.toStdString() + row.edges.toStdString() + (row.reversed ? "r" : "") + (row.broken ? "b" : "") + (row.uneven ? "u" : "");
        rows.push_back(row);
    }
    seamsSection_->setVisible(!rows.empty());
    if (signature == shownSeams_) {
        for (std::size_t i = 0; i < rows.size() && i < seamCards_.size(); ++i) {
            if (seamCards_[i].lengths->text() != rows[i].lengths) seamCards_[i].lengths->setText(rows[i].lengths);
            if (seamCards_[i].gathered->text() != rows[i].gathered) seamCards_[i].gathered->setText(rows[i].gathered);
        }
        return;
    }
    shownSeams_ = signature;
    seams_ = mine;
    seamCards_.clear();
    while (QLayoutItem* item = seamRows_->takeAt(0)) {
        retire(item->widget());
        delete item;
    }
    for (const Shown& row : rows) {
        auto* card = new QFrame(seamsSection_->content());
        const QString name = QString("seam%1").arg(row.id.value);
        card->setObjectName(name);
        card->setStyleSheet(cardStyle("QFrame#" + name, false, false));
        card->setToolTip(row.edges);
        auto* grid = new QGridLayout(card);
        grid->setContentsMargins(6, 4, 4, 4);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(0);
        auto* sew = new QLabel(card);
        sew->setPixmap(icon(Icon::Sew, row.broken ? theme::error : theme::seam).pixmap(16, 16));
        auto* partner = new QLabel(row.partner, card);
        partner->setStyleSheet(QString("color: %1").arg((row.broken ? theme::error : theme::seam).name()));
        // Uneven lengths in orange, so a gathered seam stands out from an even one.
        const QColor said = row.broken ? theme::error : row.uneven ? theme::uneven : theme::text;
        auto* lengths = new QLabel(row.lengths, card);
        lengths->setObjectName("seamLengths");
        lengths->setWordWrap(true);
        lengths->setStyleSheet(QString("color: %1").arg(said.name()));
        auto* gathered = new QLabel(row.gathered, card);
        gathered->setWordWrap(true);
        gathered->setStyleSheet(QString("color: %1").arg((row.uneven ? theme::uneven : theme::quiet).name()));
        // Checked: start meets start, which twists the pieces.
        auto* reversed = new QToolButton(card);
        reversed->setObjectName("reversed");
        reversed->setCheckable(true);
        reversed->setChecked(row.reversed);
        reversed->setAutoRaise(true);
        reversed->setIcon(icon(Icon::Twist, row.reversed ? theme::seam : theme::quiet));
        reversed->setToolTip("Reverse: swap which ends of the two edges meet.\n"
                             "The dotted links on the table show which do.");
        const SeamId id = row.id;
        connect(reversed, &QToolButton::clicked, this, [this, id](bool on) { reverseSeam(id, on); });
        grid->addWidget(sew, 0, 0, Qt::AlignVCenter);
        grid->addWidget(partner, 0, 1);
        grid->addWidget(reversed, 0, 2, 2, 1, Qt::AlignVCenter);
        auto* numbers = new QHBoxLayout();
        numbers->setSpacing(10);
        numbers->addWidget(lengths);
        numbers->addWidget(gathered, 1);
        grid->addLayout(numbers, 1, 1);
        grid->setColumnStretch(1, 1);
        // A broken seam sews nothing, so the cloth has nothing to say of it.
        QLabel* open = nullptr;
        if (!row.broken) {
            open = new QLabel(card);
            open->setObjectName("seamOpen");
            open->setWordWrap(true);
            open->setStyleSheet(QString("color: %1").arg(theme::error.name()));
            open->hide();
            grid->addWidget(open, 2, 1, 1, 2);
        }
        seamRows_->addWidget(card);
        seamCards_.push_back({lengths, gathered, open});
    }
    sayOpenSeams();
}

void AttributeEditor::reverseSeam(SeamId seam, bool reversed) { emit seamReversed(seam, reversed); }

void AttributeEditor::rebuildLayers(const Document& doc, const Result& shapes) {
    // Newest on top; a mirror applied last is on top of everything.
    std::vector<OpId> order = doc.operations(*panel_);
    std::reverse(order.begin(), order.end());
    std::stable_partition(order.begin(), order.end(), [&](OpId op) { return appliedLast(doc.get(op)); });
    const std::vector<OpId> made = doc.operations(*panel_);  // oldest first: the order they are applied in
    // What each layer's problem is now, if it has one.
    std::vector<std::pair<OpId, QString>> problems;
    for (const Error& error : shapes.errors)
        if (const OpId* where = std::get_if<OpId>(&error.where); where && doc.contains(*where) && doc.panelOf(*where) == *panel_)
            problems.push_back({*where, QString::fromStdString(error.message)});
    const auto problemOf = [&](OpId op) {
        for (const auto& [which, words] : problems)
            if (which == op) return words;
        return QString();
    };
    // A number does not say which operation it is: numbers start again in another pattern, and after an undo.
    // So each row's kind is part of what it was made from, or its fields would be read from another operation.
    std::string signature = std::to_string(panel_->value) + "@" + (viewed_ ? std::to_string(viewed_->value) : "-");
    for (OpId op : order)
        signature += "," + std::to_string(op.value) + ":" + std::to_string(doc.get(op).index()) + (doc.enabled(op) ? "+" : "-") + (isOpen(op) ? "o" : "c") +
                     problemOf(op).toStdString() + choicesOf(doc.get(op), doc);
    if (signature != shownLayers_) {
        // Everything is made, laid out and then painted at once, so that the rows never show blank.
        page_->setUpdatesEnabled(false);
        shownLayers_ = signature;
        layers_ = order;
        made_ = made;
        onTop_.clear();
        for (OpId op : order)
            if (appliedLast(doc.get(op))) onTop_.push_back(op);
        problems_ = problems;
        for (QPointer<QPropertyAnimation>& slide : slides_)
            if (slide) slide->stop();  // its row is about to go
        slides_.clear();
        while (QLayoutItem* item = layerRows_->takeAt(0)) {
            retire(item->widget());
            delete item;
        }
        openFields_.clear();
        summaries_.clear();
        const OpId base = made.empty() ? OpId{} : made.front();
        const auto indexOf = [&](OpId op) { return static_cast<int>(std::find(made.begin(), made.end(), op) - made.begin()); };
        const int viewedIndex = viewed_ ? indexOf(*viewed_) : static_cast<int>(made.size());
        if (viewed_) {
            // Looking back: say so, and how to come back.
            auto* words = new QLabel("Showing an earlier step  ·  Esc shows all", layerPage_);
            words->setContentsMargins(6, 2, 2, 4);
            words->setStyleSheet(QString("color: %1").arg(theme::formula.name()));
            layerRows_->addWidget(words);
        }
        for (OpId op : order) {
            const OpData& operation = doc.get(op);
            const bool on = doc.enabled(op);
            const bool showsFields = isOpen(op);
            const int index = indexOf(op);
            const bool afterViewed = index > viewedIndex;  // left out of what the table shows now
            const QString problem = problemOf(op);
            // Every layer is a card: a little lighter when it shows its fields.
            auto* box = new QFrame(layerPage_);
            box->setObjectName(QString("layer%1").arg(op.value));
            box->setStyleSheet(cardStyle("#" + box->objectName(), showsFields, viewed_ && *viewed_ == op));
            auto* boxColumn = new QVBoxLayout(box);
            boxColumn->setContentsMargins(0, 0, 0, showsFields ? 8 : 0);
            boxColumn->setSpacing(4);
            // The icon and the name sit on a band a little darker than the card.
            auto* head = new QFrame(box);
            head->setObjectName("layerHead");
            head->setStyleSheet(QString("#layerHead { background: %1; border-radius: 5px; }").arg((showsFields ? theme::cardBandOpen : theme::cardBand).name()));
            auto* row = new QHBoxLayout(head);
            row->setContentsMargins(8, 2, 4, 2);
            row->setSpacing(4);
            boxColumn->addWidget(head);
            QToolButton* eye = op == base ? smallButton(Icon::Lock, theme::quiet, "The base shape", box)
                                          : smallButton(on ? Icon::Eye : Icon::EyeShut, on ? theme::text : theme::quiet, on ? "Switch off" : "Switch on", box);
            eye->setObjectName("eye");
            eye->setEnabled(op != base);
            connect(eye, &QToolButton::clicked, this, [this, op, on] { emit layerSwitched(op, !on); });
            QColor colour = !problem.isEmpty() ? theme::error : appliedLast(operation) ? theme::symmetry : on && !afterViewed ? theme::text : theme::quiet;
            auto* name = new QPushButton(icon(iconOf(operation), colour), QString(label(operation).data()), box);
            name->setObjectName("layerName");
            name->setFlat(true);
            name->setStyleSheet(QString("text-align: left; color: %1; %2").arg(colour.name(), on ? "" : "text-decoration: line-through;"));
            name->setToolTip("Click: close or open.  Alt+click: show the panel as it was at this step.  Drag: move the layer.");
            // The name gives way before the buttons beside it, so a narrow dock needs no scroll bar.
            name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            // Taken by its name and moved; the base shape and a mirror applied last stay where they are.
            if (op != base && !appliedLast(operation)) {
                name->setCursor(Qt::OpenHandCursor);
                name->setProperty("layer", op.value);
                name->installEventFilter(this);
            }
            connect(name, &QPushButton::clicked, this, [this, op] {
                if (QGuiApplication::keyboardModifiers() & Qt::AltModifier) viewStep(op);
                else toggleLayer(op);
            });
            auto* summary = new QLabel(summaryOf(operation, doc, shapes), box);
            summary->setStyleSheet(summaryStyleOf(operation));
            summaries_.push_back({op, summary});
            // The base shape stays first and stays.
            QToolButton* bin = smallButton(Icon::Delete, theme::quiet, "Remove this layer", box);
            bin->setObjectName("remove");
            bin->setEnabled(op != base);
            connect(bin, &QToolButton::clicked, this, [this, op] { removeLayer(op); });
            row->addWidget(name, 1);
            row->addWidget(summary);
            row->addWidget(bin);
            row->addWidget(eye);  // top right, where the eye of a layer is expected
            if (!problem.isEmpty()) {
                auto* why = new QLabel(problem, box);
                why->setObjectName("problem");
                why->setWordWrap(true);
                why->setContentsMargins(26, 0, 8, 2);
                why->setStyleSheet(QString("color: %1").arg(theme::error.name()));
                boxColumn->addWidget(why);
            }
            if (showsFields) {
                auto* grid = new QGridLayout();
                grid->setContentsMargins(26, 2, 8, 0);
                grid->setSpacing(4);
                grid->setColumnStretch(1, 1);  // what is chosen or typed has the room the names leave
                int line = 0;
                for (const ReferenceField& reference : references(operation)) {
                    grid->addWidget(new QLabel(QString(reference.label.data()), box), line, 0);
                    const QString used = QString::fromStdString(nameOf(reference.value, doc));
                    if (std::holds_alternative<PanelId>(reference.value)) {
                        // Any other panel can be chosen; one that would follow itself is refused when chosen.
                        auto* list = new QComboBox(box);
                        list->setObjectName("panelReference");
                        const PanelId own = *panel_;
                        int current = -1;
                        for (PanelId other : doc.panels()) {
                            if (other == own) continue;
                            if (Reference(other) == reference.value) current = list->count();
                            list->addItem(QString::fromStdString(doc.get(other).name), QVariant::fromValue(other.value));
                        }
                        if (current < 0) list->insertItem(0, used), current = 0;  // nothing chosen, or a panel removed
                        list->setCurrentIndex(current);
                        takeWheelOnlyWithFocus(*list);
                        fitItsPlace(*list);
                        const ReferenceId id = reference.id;
                        connect(list, &QComboBox::activated, this, [this, op, id, list](int chosen) {
                            const QVariant value = list->itemData(chosen);
                            if (value.isValid()) emit panelReferenceChosen(op, id, PanelId{value.toUInt()});
                        });
                        grid->addWidget(list, line++, 1);
                        continue;
                    }
                    // An edge, corner or line: clicking it chooses another on the table.
                    auto* what = new QPushButton(used, box);
                    what->setFlat(true);
                    what->setCursor(Qt::PointingHandCursor);
                    what->setToolTip(used + "\nClick, then click another on the table");
                    // A long name is cut off at the dock's edge (the tooltip has it all), not scrolled to.
                    what->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                    what->setStyleSheet(QString("text-align: left; color: %1; text-decoration: underline;").arg(theme::seam.name()));
                    const ReferenceId id = reference.id;
                    connect(what, &QPushButton::clicked, this, [this, op, id] { emit referenceClicked(op, id); });
                    grid->addWidget(what, line++, 1);
                }
                // Settings that are one of a few choices.
                if (const Cut* cut = std::get_if<Cut>(&operation)) {
                    grid->addWidget(new QLabel("Keep", box), line, 0);
                    auto* keep = new QComboBox(box);
                    keep->setObjectName("keep");
                    keep->addItems({"left of the line", "right of the line"});
                    keep->setCurrentIndex(cut->keep == Cut::Side::Left ? 0 : 1);
                    takeWheelOnlyWithFocus(*keep);
                    fitItsPlace(*keep);
                    connect(keep, &QComboBox::activated, this, [this, op](int chosen) { emit cutSideChosen(op, chosen == 0 ? Cut::Side::Left : Cut::Side::Right); });
                    grid->addWidget(keep, line++, 1);
                }
                if (const SetLength* set = std::get_if<SetLength>(&operation)) {
                    // The ends of the edge are named by their corners, where the outline still has the edge: in
                    // few words in the list, which layer they come from in an entry's tooltip.
                    QString start = "start", end = "end", startInFull = "the start corner", endInFull = "the end corner";
                    const PanelId panel = doc.panelOf(op);
                    if (doc.contains(panel) && shapes[panel].contains(set->edge)) {
                        const Pattern& shape = shapes[panel];
                        const Edge& edge = shape.edge(set->edge);
                        const CornerRef& first = shape.vertices[edge.start].name;
                        const CornerRef& last = shape.vertices[edge.end].name;
                        startInFull = "the " + QString::fromStdString(nameOf(Reference(first), doc));
                        endInFull = "the " + QString::fromStdString(nameOf(Reference(last), doc));
                        start = QString::fromStdString(shortName(first, doc));
                        end = QString::fromStdString(shortName(last, doc));
                        // Two that are alike in few words, as two added points are, are told apart by their layers.
                        if (start == end) start = startInFull, end = endInFull;
                    }
                    grid->addWidget(new QLabel("Keeps", box), line, 0);
                    auto* keeps = new QComboBox(box);
                    keeps->setObjectName("keeps");
                    keeps->addItems({"both ends move evenly", start + " stays", end + " stays"});
                    keeps->setItemData(1, startInFull + " stays", Qt::ToolTipRole);
                    keeps->setItemData(2, endInFull + " stays", Qt::ToolTipRole);
                    keeps->setCurrentIndex(static_cast<int>(set->keeping));
                    takeWheelOnlyWithFocus(*keeps);
                    fitItsPlace(*keeps);
                    keeps->setToolTip("Which corner of the edge stays where it is while the edge gets its length. "
                                      "On a half panel mirrored across an edge, keep the corner on the mirror.");
                    connect(keeps, &QComboBox::activated, this, [this, op](int chosen) { emit lengthKeepingChosen(op, static_cast<SetLength::Keeping>(chosen)); });
                    grid->addWidget(keeps, line++, 1);
                }
                if (const Mirror* mirror = std::get_if<Mirror>(&operation)) {
                    auto* last = new QCheckBox("Applied last", box);
                    last->setObjectName("appliedLast");
                    last->setChecked(mirror->appliedLast);
                    last->setToolTip("Ticked: the mirror waits for every other step, so later edits stay symmetric.\n"
                                     "Unticked: it runs in its place, and later steps work on the whole panel.");
                    connect(last, &QCheckBox::clicked, this, [this, op](bool ticked) { emit mirrorLastChanged(op, ticked); });
                    grid->addWidget(last, line++, 0, 1, 2);
                }
                if (const Instance* instance = std::get_if<Instance>(&operation)) {
                    auto* mirrored = new QCheckBox("Mirrored", box);
                    mirrored->setObjectName("mirrored");
                    mirrored->setChecked(instance->mirrored);
                    mirrored->setToolTip("Follows the source flipped left to right, as the other sleeve does.");
                    connect(mirrored, &QCheckBox::clicked, this, [this, op](bool ticked) { emit instanceMirroredChanged(op, ticked); });
                    grid->addWidget(mirrored, line++, 0, 1, 2);
                }
                for (const Field& f : fields(operation)) {
                    grid->addWidget(new QLabel(QString(f.label.data()), box), line, 0);
                    auto* value = new ValueField(f.unit, box);
                    value->show(f.value, doc, shapes);
                    const FieldId id = f.id;
                    connect(value, &ValueField::typed, this, [this, op, id](QString text) { emit fieldTyped(op, id, text); });
                    connect(value, &ValueField::dragged, this,
                            [this, op, id](double number, session::DragPhase phase) { emit fieldDragged(op, id, number, phase); });
                    grid->addWidget(value, line++, 1);
                    openFields_.push_back({op, id, value});
                }
                boxColumn->addLayout(grid);
            }
            layerRows_->addWidget(box);
        }
        layerPage_->layout()->activate();
        page_->layout()->activate();
        page_->setUpdatesEnabled(true);
    } else {
        // Same rows: only the values change, and the open fields keep what is being typed.
        for (const OpenField& open : openFields_)
            if (doc.contains(open.layer)) open.value->show(fields(doc.get(open.layer))[open.id.index].value, doc, shapes);
        for (const LayerSummary& one : summaries_) {
            if (!doc.contains(one.layer)) continue;
            const QString words = summaryOf(doc.get(one.layer), doc, shapes), style = summaryStyleOf(doc.get(one.layer));
            if (one.label->text() != words) one.label->setText(words);
            if (one.label->styleSheet() != style) one.label->setStyleSheet(style);
        }
    }
}

bool AttributeEditor::isOpen(OpId layer) const { return std::find(closed_.begin(), closed_.end(), layer) == closed_.end(); }

void AttributeEditor::openLayer(OpId layer) {
    if (isOpen(layer)) return;
    toggleLayer(layer);
}

void AttributeEditor::toggleLayer(OpId layer) {
    if (isOpen(layer)) closed_.push_back(layer);
    else closed_.erase(std::find(closed_.begin(), closed_.end(), layer));
    shownLayers_.clear();
    emit layerOpened();
}

void AttributeEditor::switchLayer(OpId layer, bool on) { emit layerSwitched(layer, on); }

void AttributeEditor::moveLayer(OpId layer, std::size_t to) { emit layerMoved(layer, to); }

QRect AttributeEditor::rowOf(OpId layer) const {
    const QString name = QString("layer%1").arg(layer.value);
    for (int i = 0; i < layerRows_->count(); ++i)
        if (const QWidget* row = layerRows_->itemAt(i)->widget(); row && row->objectName() == name) return layerRows_->itemAt(i)->geometry();
    return {};
}

// Where a dragged layer lands is read off the rows as the layout places them (not
// as they slide): the free rows whose middle is above `y` are newer than it, the
// rest older. A mirror applied last never moves, and nothing goes below the base.
std::optional<AttributeEditor::Drop> AttributeEditor::dropAt(OpId layer, int y) const {
    std::vector<OpId> rows;  // the other layers that can be passed, newest first
    for (OpId op : layers_)
        if (!(op == layer) && std::find(onTop_.begin(), onTop_.end(), op) == onTop_.end()) rows.push_back(op);
    if (rows.empty()) return std::nullopt;
    std::size_t newer = 0;
    while (newer + 1 < rows.size() && rowOf(rows[newer]).center().y() < y) ++newer;
    std::vector<OpId> others = made_;  // without the dragged one: where it goes is counted among these
    others.erase(std::remove(others.begin(), others.end(), layer), others.end());
    // Right after the newest layer that stays older.
    return Drop{static_cast<std::size_t>(std::find(others.begin(), others.end(), rows[newer]) - others.begin()) + 1, rows[newer]};
}

// The dragged row moves to just above `above` in the layout and every row that
// has to move slides to its new place, so that the gap follows the mouse.
void AttributeEditor::makeRoom(QWidget* row, OpId above) {
    const QString aboveName = QString("layer%1").arg(above.value);
    QWidget* target = nullptr;
    for (int i = 0; i < layerRows_->count(); ++i)
        if (QWidget* other = layerRows_->itemAt(i)->widget(); other && other->objectName() == aboveName) target = other;
    if (!target || layerRows_->indexOf(row) == layerRows_->indexOf(target) - 1) return;
    std::vector<std::pair<QWidget*, QPoint>> before;
    for (int i = 0; i < layerRows_->count(); ++i)
        if (QWidget* other = layerRows_->itemAt(i)->widget()) before.push_back({other, other->pos()});
    layerRows_->removeWidget(row);
    layerRows_->insertWidget(layerRows_->indexOf(target), row);
    layerPage_->layout()->activate();
    for (const auto& [other, from] : before) {
        const QPoint to = other->pos();
        if (from == to) continue;
        for (QPointer<QPropertyAnimation>& old : slides_)
            if (old && old->targetObject() == other) old->stop();
        std::erase_if(slides_, [](const QPointer<QPropertyAnimation>& slide) { return slide.isNull(); });
        other->move(from);
        auto* slide = new QPropertyAnimation(other, "pos", this);
        slide->setDuration(140);
        slide->setEasingCurve(QEasingCurve::OutCubic);
        slide->setStartValue(from);
        slide->setEndValue(to);
        slide->start(QAbstractAnimation::DeleteWhenStopped);
        slides_.push_back(slide);
    }
}

void AttributeEditor::endDrag(bool drop) {
    if (!dragging_) return;
    const Dragging was = *dragging_;
    dragging_.reset();
    if (was.picture) was.picture->deleteLater();
    if (!was.started) return;
    QGuiApplication::restoreOverrideCursor();
    if (was.row) was.row->setGraphicsEffect(nullptr);  // the gap is full again, where it will land
    const std::size_t now = static_cast<std::size_t>(std::find(made_.begin(), made_.end(), was.layer) - made_.begin());
    // Reported after this event: the rows are made again, and this button goes with them.
    const auto later = [this](auto what) { QMetaObject::invokeMethod(this, what, Qt::QueuedConnection); };
    if (drop && was.drop && was.drop->to != now) later([this, layer = was.layer, to = was.drop->to] { moveLayer(layer, to); });
    else later([this] { shownLayers_.clear(); emit layerOpened(); });  // put back as it was
}

bool AttributeEditor::eventFilter(QObject* watched, QEvent* event) {
    const QVariant layerOf = watched->property("layer");
    if (!layerOf.isValid()) return QScrollArea::eventFilter(watched, event);
    const OpId layer{layerOf.toUInt()};
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove && type != QEvent::MouseButtonRelease) return false;
    const auto* mouse = static_cast<QMouseEvent*>(event);
    const QPoint where = mouse->globalPosition().toPoint();
    if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
        dragging_ = Dragging{.layer = layer, .from = where};
    } else if (type == QEvent::MouseMove && dragging_ && (mouse->buttons() & Qt::LeftButton)) {
        if (!dragging_->started && (where - dragging_->from).manhattanLength() >= QGuiApplication::styleHints()->startDragDistance()) {
            QWidget* row = layerPage_->findChild<QWidget*>(QString("layer%1").arg(layer.value));
            if (!row) return false;
            dragging_->started = true;
            dragging_->row = row;
            dragging_->grabbed = layerPage_->mapFromGlobal(dragging_->from).y() - row->y();
            // A picture of the row follows the mouse; the row stays behind as a dim gap.
            auto* picture = new QLabel(layerPage_);
            picture->setPixmap(row->grab());
            picture->setAttribute(Qt::WA_TransparentForMouseEvents);
            picture->setStyleSheet(QString("border: 1px solid %1; border-radius: 6px; background: %2;").arg(theme::cardEdgeViewed.name(), theme::header.name()));
            picture->setGeometry(row->geometry());
            auto* fade = new QGraphicsOpacityEffect(picture);
            fade->setOpacity(0.9);
            picture->setGraphicsEffect(fade);
            picture->show();
            picture->raise();
            dragging_->picture = picture;
            auto* dim = new QGraphicsOpacityEffect(row);
            dim->setOpacity(0.25);
            row->setGraphicsEffect(dim);
            QGuiApplication::setOverrideCursor(Qt::ClosedHandCursor);
        }
        if (dragging_->started) {
            if (!dragging_->row || !dragging_->picture) return true;  // the rows were made again meanwhile
            // Near the top or bottom edge of the list, it scrolls on.
            const int inView = layerScroll_->viewport()->mapFromGlobal(where).y();
            QScrollBar* bar = layerScroll_->verticalScrollBar();
            if (inView < 28) bar->setValue(bar->value() - 14);
            else if (inView > layerScroll_->viewport()->height() - 28) bar->setValue(bar->value() + 14);
            const int top = layerPage_->mapFromGlobal(where).y() - dragging_->grabbed;
            const QRect room = layerRows_->geometry();
            const int height = dragging_->picture->height();
            dragging_->picture->move(dragging_->picture->x(), std::clamp(top, room.top(), std::max(room.top(), room.bottom() - height)));
            if (const std::optional<Drop> at = dropAt(layer, dragging_->picture->geometry().center().y())) {
                dragging_->drop = at;
                makeRoom(dragging_->row, at->above);
            }
            return true;
        }
    } else if (type == QEvent::MouseButtonRelease && dragging_) {
        const bool dragged = dragging_->started;
        endDrag(true);
        return dragged;  // a drag is not a click
    }
    return false;
}

void AttributeEditor::removeLayer(OpId layer) { emit layerRemoved(layer); }

std::vector<session::Offer> AttributeEditor::offered() const {
    std::vector<session::Offer> offers;
    for (const session::Offered& offer : offers_) offers.push_back(offer.offer);
    return offers;
}

void AttributeEditor::addOperation(session::Offer offer) {
    if (panel_) emit operationOffered(*panel_, offer);
}

void AttributeEditor::viewStep(OpId layer) { emit stepViewed(viewed_ && *viewed_ == layer ? std::nullopt : std::optional<OpId>(layer)); }

void AttributeEditor::showAllSteps() { emit stepViewed(std::nullopt); }

QString AttributeEditor::problemShown(OpId layer) const {
    for (const auto& [which, words] : problems_)
        if (which == layer) return words;
    return {};
}

ValueField* AttributeEditor::field(OpId layer, FieldId id) const {
    for (const OpenField& open : openFields_)
        if (open.layer == layer && open.id == id) return open.value;
    return nullptr;
}

ValueField* AttributeEditor::spaceField(session::SpaceField field) const { return space_[static_cast<std::size_t>(field)]; }

ValueField* AttributeEditor::tableField(session::TableField field) const { return table_[static_cast<std::size_t>(field)]; }

} // namespace twintailor::ui
