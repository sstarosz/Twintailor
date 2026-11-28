#include "twintailor/session/Samples.h"

#include "twintailor/core/Drawing.h"
#include "twintailor/core/Evaluate.h"

namespace twintailor::session {

namespace {

Document waistbandAndSkirt() {
    Document doc;
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});

    PanelId     waistband = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect  = doc.add(waistband, Rect{.width = waist, .height = 4_cm});

    PanelCopy skirt = doc.copy(waistband, Panel{.name = "skirt"});
    doc.set(skirt[bandRect], &Rect::height, skirtLength);
    return doc;
}

Document circleSkirt() {
    Document doc;
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});
    Param fullness    = doc.add(Parameter{.name = "fullness", .value = 0.5, .unit = Unit::None});

    PanelId waistband = doc.add(Panel{.name = "waistband"});
    doc.add(waistband, Rect{.width = waist, .height = 4_cm});

    PanelId skirt = doc.add(Panel{.name = "skirt"});
    doc.add(skirt, RingSector{.innerLength = waist, .sweep = 360_deg * fullness, .depth = skirtLength});
    return doc;
}

Document asymmetricHem() {
    Document doc;
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});
    Param hemAngle    = doc.add(Parameter{.name = "hem_angle", .value = 25_deg, .unit = Unit::Angle});

    PanelId     skirt   = doc.add(Panel{.name = "skirt"});
    OpRef<Rect> rect    = doc.add(skirt, Rect{.width = waist, .height = skirtLength});
    OpRef<Line> hemLine = doc.add(skirt, Line{.from = rect.bottomLeft(), .angle = hemAngle});
    doc.add(skirt, Cut{.along = hemLine.line(), .keep = Cut::Side::Left});
    return doc;
}

Document mirroredSleeve() {
    Document doc;
    Param bicep        = doc.add(Parameter{.name = "bicep", .value = 30_cm});
    Param sleeveLength = doc.add(Parameter{.name = "sleeve_length", .value = 60_cm});
    Param capHeight    = doc.add(Parameter{.name = "cap_height", .value = 12_cm});
    Param wrist        = doc.add(Parameter{.name = "wrist", .value = 22_cm});

    // Half a sleeve; its left edge is the centre line. The crown is raised on
    // the centre line, and the cap runs to it as one S-shaped curve that is
    // level at both ends, so the two halves join smoothly.
    PanelId     leftSleeve = doc.add(Panel{.name = "left sleeve"});
    OpRef<Rect> half       = doc.add(leftSleeve, Rect{.width = bicep / 2, .height = sleeveLength});
    doc.add(leftSleeve, AdjustPoint{.corner = half.topLeft(), .dx = 0_cm, .dy = capHeight});
    doc.add(leftSleeve, Bezier{.edge = half.top(), .startHandle = {-bicep / 4, 0_cm}, .endHandle = {bicep / 4, 0_cm}});
    doc.add(leftSleeve, AdjustPoint{.corner = half.bottomRight(), .dx = (wrist - bicep) / 2, .dy = 0_cm});  // narrow to the wrist
    doc.add(leftSleeve, Mirror{.across = half.left()});

    PanelId rightSleeve = doc.add(Panel{.name = "right sleeve"});
    doc.add(rightSleeve, Instance{.source = leftSleeve, .mirrored = true});
    return doc;
}

Document sharedBase() {
    Document doc;
    Param halfChest = doc.add(Parameter{.name = "half_chest", .value = 25_cm});
    Param length    = doc.add(Parameter{.name = "bodice_length", .value = 45_cm});
    Param armhole   = doc.add(Parameter{.name = "armhole_depth", .value = 2_cm});
    Param hemAngle  = doc.add(Parameter{.name = "left_hem_angle", .value = 15_deg, .unit = Unit::Angle});
    Param neckDrop  = doc.add(Parameter{.name = "left_neck_drop", .value = 3_cm});

    PanelId     base = doc.add(Panel{.name = "bodice base", .construction = true});
    OpRef<Rect> body = doc.add(base, Rect{.width = halfChest, .height = length});
    doc.add(base, Curve{.edge = body.right(), .bulge = -armhole});

    PanelId rightFront = doc.add(Panel{.name = "right front"});
    doc.add(rightFront, Instance{.source = base});

    PanelId     leftFront = doc.add(Panel{.name = "left front"});
    doc.add(leftFront, Instance{.source = base, .mirrored = true});
    OpRef<Line> hemLine   = doc.add(leftFront, Line{.from = body.bottomRight(), .angle = hemAngle});
    doc.add(leftFront, Cut{.along = hemLine.line(), .keep = Cut::Side::Left});
    doc.add(leftFront, AdjustPoint{.corner = body.topLeft(), .dx = 0_cm, .dy = -neckDrop});
    return doc;
}

// What another sample needs of the T-shirt body to wear it with something else.
struct Shirt {
    Param ease, length;
    PanelId front, back;
};

// A T-shirt body, front and back, each a mirrored half shaped by adding
// points: a neck point on the top, an underarm point on the side. As
// examples/06_tshirt_body.cpp.
Shirt addTshirtBody(Document& doc) {
    Param chest         = doc.add(Parameter{.name = "chest", .value = 96_cm});
    Param ease          = doc.add(Parameter{.name = "ease", .value = 8_cm});
    Param length        = doc.add(Parameter{.name = "length", .value = 66_cm});
    Param neckWidth     = doc.add(Parameter{.name = "neck_width", .value = 8_cm});
    Param frontNeckDrop = doc.add(Parameter{.name = "front_neck_drop", .value = 8_cm});
    Param backNeckDrop  = doc.add(Parameter{.name = "back_neck_drop", .value = 2_cm});
    Param shoulderDrop  = doc.add(Parameter{.name = "shoulder_drop", .value = 4_cm});
    Param armholeDepth  = doc.add(Parameter{.name = "armhole_depth", .value = 22_cm});
    const Expr quarter = (chest + ease) / 4;

    struct Half {
        PanelId panel;
        OpRef<Rect> body;
        OpRef<Mirror> fold;
    };
    const auto half = [&](const char* name, Param neckDrop) {
        const PanelId panel = doc.add(Panel{.name = name});
        const OpRef<Rect> body = doc.add(panel, Rect{.width = quarter, .height = length});
        // The top runs from the side to the centre; the side from the hem up.
        const OpRef<AddPoint> neck = doc.add(panel, AddPoint{.at = {body.top(), (quarter - neckWidth) / quarter}});
        const OpRef<AddPoint> underarm = doc.add(panel, AddPoint{.at = {body.right(), (length - armholeDepth) / length}});
        doc.add(panel, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = -shoulderDrop});  // the shoulder slopes
        doc.add(panel, Curve{.edge = underarm.after(body.right()), .bulge = -2_cm});              // the armhole
        doc.add(panel, AdjustPoint{.corner = body.topLeft(), .dx = 0_cm, .dy = -neckDrop});         // the neckline drops
        doc.add(panel, Bezier{.edge = neck.after(body.top()), .startHandle = {0_cm, -neckDrop * 0.6}, .endHandle = {neckWidth / 2, 0_cm}});
        return Half{panel, body, doc.add(panel, Mirror{.across = body.left()})};
    };
    const Half front = half("front", frontNeckDrop);
    const Half back = half("back", backNeckDrop);
    // The back is turned round in space: the front's drawn half meets the back's mirrored half.
    const auto sew = [&](EdgeRef a, EdgeRef b) { doc.add(Seam{.a = {front.panel, a}, .b = {back.panel, b}}); };
    sew(front.body.right(), back.fold.image(back.body.right()));
    sew(front.fold.image(front.body.right()), back.body.right());
    sew(front.body.top(), back.fold.image(back.body.top()));
    sew(front.fold.image(front.body.top()), back.body.top());
    doc.arrange(front.panel, Arrangement{.placed = true, .at = {0, 104, 14}, .wrap = 165});
    doc.arrange(back.panel, Arrangement{.placed = true, .at = {0, 104, -14}, .turn = 180, .wrap = 165});
    return Shirt{.ease = ease, .length = length, .front = front.panel, .back = back.panel};
}

Document tshirtBody() {
    Document doc;
    addTshirtBody(doc);
    return doc;
}

// Trousers, as examples/07_trousers.cpp: a front and a back for the right
// leg, each a rectangle shaped by adding points, a crotch point on the inside
// edge moved out into the crotch and a hip point on the side, with the waist
// and the hem given lengths of their own (Set length), which a rectangle's
// width cannot say. The left leg is a mirrored instance of both, sewn as they
// are; the centre seams, each panel to its mirror image, are sewn by hand.
// Right and left are the wearer's: the right leg stands at -x, since the body
// faces +z.
void addTrousers(Document& doc) {
    Param waist       = doc.add(Parameter{.name = "waist", .value = 76_cm});
    Param hips        = doc.add(Parameter{.name = "hips", .value = 94_cm});
    Param ease        = doc.add(Parameter{.name = "ease", .value = 6_cm});
    Param length      = doc.add(Parameter{.name = "length", .value = 92_cm});
    Param rise        = doc.add(Parameter{.name = "rise", .value = 28_cm});
    Param ankle       = doc.add(Parameter{.name = "ankle", .value = 40_cm});
    Param frontCrotch = doc.add(Parameter{.name = "front_crotch", .value = 5_cm});
    Param backCrotch  = doc.add(Parameter{.name = "back_crotch", .value = 12_cm});
    const Expr quarter = (hips + ease) / 4;
    const Expr hipDepth = 18_cm;  // from the waist down to the hips

    struct Leg {
        PanelId panel;
        EdgeRef upperSide, lowerSide, inseam, rise;  // the side above and below the hip point
    };
    // The side is rounded over the hips: two arcs that meet at the hip point in
    // one line, so the outline has no corner there.
    const auto round = [&](PanelId panel, EdgeRef upperSide, EdgeRef lowerSide) {
        doc.add(panel, Curve{.edge = upperSide, .bulge = 1.6_cm});
        doc.add(panel, Curve{.edge = lowerSide, .bulge = 0.4_cm});
    };
    // Seen from outside, the front has the side on its left and the inside of
    // the leg on its right, and the back the other way round. A rectangle's
    // right edge runs from the hem up, its left edge from the waist down.
    const Leg front = [&] {
        const PanelId panel = doc.add(Panel{.name = "right front"});
        const OpRef<Rect> body = doc.add(panel, Rect{.width = quarter, .height = length});
        const OpRef<AddPoint> crotch = doc.add(panel, AddPoint{.at = {body.right(), (length - rise) / length}});
        const OpRef<AddPoint> hip = doc.add(panel, AddPoint{.at = {body.left(), hipDepth / length}});
        doc.add(panel, AdjustPoint{.corner = crotch.corner(), .dx = frontCrotch, .dy = 0_cm});
        doc.add(panel, Curve{.edge = crotch.after(body.right()), .bulge = -2_cm});                                   // the crotch curve
        doc.add(panel, SetLength{.edge = body.top(), .length = waist / 4, .keeping = SetLength::Keeping::Start});  // the centre front stays
        doc.add(panel, SetLength{.edge = body.bottom(), .length = ankle / 2});
        round(panel, body.left(), hip.after(body.left()));
        return Leg{.panel = panel, .upperSide = body.left(), .lowerSide = hip.after(body.left()), .inseam = body.right(), .rise = crotch.after(body.right())};
    }();
    const Leg back = [&] {
        const PanelId panel = doc.add(Panel{.name = "right back"});
        const OpRef<Rect> body = doc.add(panel, Rect{.width = quarter, .height = length});
        const OpRef<AddPoint> crotch = doc.add(panel, AddPoint{.at = {body.left(), rise / length}});
        const OpRef<AddPoint> hip = doc.add(panel, AddPoint{.at = {body.right(), (length - hipDepth) / length}});
        // A little lower than the front's, as patterns have it: the two inseams are then about as long.
        doc.add(panel, AdjustPoint{.corner = crotch.corner(), .dx = -backCrotch, .dy = -1.2_cm});
        doc.add(panel, Curve{.edge = body.left(), .bulge = -2_cm});
        doc.add(panel, SetLength{.edge = body.top(), .length = waist / 4, .keeping = SetLength::Keeping::End});  // the centre back stays
        doc.add(panel, SetLength{.edge = body.bottom(), .length = ankle / 2});
        round(panel, hip.after(body.right()), body.right());
        return Leg{.panel = panel, .upperSide = hip.after(body.right()), .lowerSide = body.right(), .inseam = crotch.after(body.left()), .rise = body.left()};
    }();
    const auto sew = [&](PanelId a, EdgeRef edgeA, PanelId b, EdgeRef edgeB) { doc.add(Seam{.a = {a, edgeA}, .b = {b, edgeB}}); };
    sew(front.panel, front.upperSide, back.panel, back.upperSide);
    sew(front.panel, front.lowerSide, back.panel, back.lowerSide);
    sew(front.panel, front.inseam, back.panel, back.inseam);

    const PanelId leftFront = doc.add(Panel{.name = "left front"});
    doc.add(leftFront, Instance{.source = front.panel, .mirrored = true});
    const PanelId leftBack = doc.add(Panel{.name = "left back"});
    doc.add(leftBack, Instance{.source = back.panel, .mirrored = true});
    doc.sewAsSource(leftFront);
    doc.sewAsSource(leftBack);
    sew(front.panel, front.rise, leftFront, front.rise);  // the centre front
    sew(back.panel, back.rise, leftBack, back.rise);      // the centre back

    // Each panel bent half round its leg, the fronts before the body and the
    // backs behind it, inside the arms, which hang beside the hips. The hem
    // starts 6 cm above the floor.
    const auto stand = [&](PanelId panel, double x, double z, double turn) {
        doc.arrange(panel, Arrangement{.placed = true, .at = {x, 52, z}, .turn = turn, .wrap = 180});
    };
    stand(front.panel, -11.5, 22, 0);
    stand(back.panel, -11.5, -23, 180);
    stand(leftFront, 11.5, 22, 0);
    stand(leftBack, 11.5, -23, 180);
}

Document trousers() {
    Document doc;
    addTrousers(doc);
    return doc;
}

// The T-shirt body worn over the trousers. It is shorter than on its own, and
// stands further from the body than the trousers do, so they reach the body
// first and the shirt comes to lie on them.
Document tshirtAndTrousers() {
    Document doc;
    const Shirt shirt = addTshirtBody(doc);
    // Both garments have an "ease" and a "length": the shirt's say whose they are.
    doc.rename(shirt.ease, "shirt_ease");
    doc.rename(shirt.length, "shirt_length");
    doc.set(shirt.length, 52_cm);
    for (PanelId panel : {shirt.front, shirt.back}) {
        Arrangement raised = doc.get(panel).arrangement;
        raised.at.y = 111;  // the shoulders stay where they were: half of what was taken off
        raised.at.z = raised.at.z > 0 ? 36 : -37;  // well outside the trousers' fronts and backs
        raised.wrap = 140;
        doc.arrange(panel, raised);
    }
    addTrousers(doc);
    return doc;
}

} // namespace

const std::vector<Sample>& samples() {
    constexpr const char* avatar = "avatarV1.obj";
    static const std::vector<Sample> all = {
        {"1. Waistband and skirt", waistbandAndSkirt, avatar},
        {"2. Circle skirt", circleSkirt, avatar},
        {"3. Asymmetric hem", asymmetricHem, avatar},
        {"4. Mirrored sleeve", mirroredSleeve, avatar},
        {"5. Shared base", sharedBase, avatar},
        {"6. T-shirt body", tshirtBody, avatar},
        {"7. Trousers", trousers, avatar},
        {"8. T-shirt and trousers", tshirtAndTrousers, avatar},
    };
    return all;
}

void layOutOnTheTable(Document& doc) {
    const std::vector<PanelId> panels = doc.panels();
    if (panels.size() < 2) return;
    for (PanelId panel : panels)
        if (doc.get(panel).at != Vec2{}) return;
    const Result laidOut = evaluate(doc);
    double nextX = 0.0;
    for (PanelId panel : panels) {
        const std::optional<Bounds> box = bounds(laidOut[panel]);
        if (!box) continue;
        doc.place(panel, {nextX - box->min.x, -box->min.y});
        nextX += box->max.x - box->min.x + 12_cm;
    }
}

} // namespace twintailor::session
