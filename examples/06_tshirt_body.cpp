// Example 6: the body of a T-shirt, front and back, made by adding points.
//
//   1. a half front: a rectangle, with a point added on the top for the neck
//      and one on the side for the underarm. The top corner moved in and
//      down makes the shoulder slope; the edge from the underarm up to it
//      is the armhole, curved inwards; the centre dropped below the neck
//      point makes the neckline, a free curve that arrives level at the
//      centre so the two halves join smoothly. Mirrored across the centre.
//   2. the back: the same, with a shallower neckline.
//   3. sewn at the sides and the shoulders, front to back.
//   4. chest = 104  ->  both panels widen; the neck and the armholes keep
//                       their size, the seams still match

#include <cmath>
#include <cstdio>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "Expect.h"

using namespace twintailor;

namespace {

// What one half panel is made of, to name its edges afterwards.
struct Half {
    PanelId panel;
    OpRef<Rect> body;
    OpRef<AddPoint> neck, underarm;
    OpRef<Mirror> fold;
};

Half makeHalf(Document& doc, const char* name, Expr quarter, Expr length, Expr neckWidth, Expr neckDrop, Expr shoulderDrop,
              Expr armholeDepth) {
    Half half;
    half.panel = doc.add(Panel{.name = name});
    half.body = doc.add(half.panel, Rect{.width = quarter, .height = length});
    // The top runs from the side to the centre; the side from the hem up.
    half.neck = doc.add(half.panel, AddPoint{.at = {half.body.top(), (quarter - neckWidth) / quarter}});
    half.underarm = doc.add(half.panel, AddPoint{.at = {half.body.right(), (length - armholeDepth) / length}});
    doc.add(half.panel, AdjustPoint{.corner = half.body.topRight(), .dx = -3_cm, .dy = -shoulderDrop});
    doc.add(half.panel, Curve{.edge = half.underarm.after(half.body.right()), .bulge = -2_cm});
    doc.add(half.panel, AdjustPoint{.corner = half.body.topLeft(), .dx = 0_cm, .dy = -neckDrop});
    doc.add(half.panel, Bezier{.edge = half.neck.after(half.body.top()),
                               .startHandle = {0_cm, -neckDrop * 0.6},
                               .endHandle = {neckWidth / 2, 0_cm}});
    half.fold = doc.add(half.panel, Mirror{.across = half.body.left()});
    return half;
}

} // namespace

int main() {
    Document doc;
    Param chest         = doc.add(Parameter{.name = "chest", .value = 96_cm});
    Param ease          = doc.add(Parameter{.name = "ease", .value = 8_cm});
    Param length        = doc.add(Parameter{.name = "length", .value = 66_cm});
    Param neckWidth     = doc.add(Parameter{.name = "neck_width", .value = 8_cm});
    Param frontNeckDrop = doc.add(Parameter{.name = "front_neck_drop", .value = 8_cm});
    Param backNeckDrop  = doc.add(Parameter{.name = "back_neck_drop", .value = 2_cm});
    Param shoulderDrop  = doc.add(Parameter{.name = "shoulder_drop", .value = 4_cm});
    Param armholeDepth  = doc.add(Parameter{.name = "armhole_depth", .value = 22_cm});
    const Expr quarter = (chest + ease) / 4;

    // 1-2. The two panels.
    const Half front = makeHalf(doc, "front", quarter, length, neckWidth, frontNeckDrop, shoulderDrop, armholeDepth);
    const Half back = makeHalf(doc, "back", quarter, length, neckWidth, backNeckDrop, shoulderDrop, armholeDepth);

    // 3. The back is turned round in space, so the front's drawn half meets
    //    the back's mirrored half. A mirrored edge runs the other way round,
    //    so each pair runs opposite ways, as a seam expects.
    const auto sew = [&](EdgeRef a, EdgeRef b) { doc.add(Seam{.a = {front.panel, a}, .b = {back.panel, b}}); };
    sew(front.body.right(), back.fold.image(back.body.right()));
    sew(front.fold.image(front.body.right()), back.body.right());
    sew(front.body.top(), back.fold.image(back.body.top()));
    sew(front.fold.image(front.body.top()), back.body.top());
    doc.arrange(front.panel, Arrangement{.placed = true, .at = {0, 104, 14}, .wrap = 165});
    doc.arrange(back.panel, Arrangement{.placed = true, .at = {0, 104, -14}, .turn = 180, .wrap = 165});

    Result shapes = evaluate(doc);
    const Pattern* f = &shapes[front.panel];
    const Pattern* b = &shapes[back.panel];

    std::printf("front and back\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("corners: 6 a half, 2 shared on the centre", static_cast<double>(f->vertices.size()), 10);
    expect("neck point from the centre", f->position(front.neck.corner()).x, 8_cm);
    expect("underarm height", f->position(front.underarm.corner()).y, 44_cm);
    expect("shoulder tip", f->position(front.body.topRight()).x, 23_cm);
    expect("shoulder slope", f->position(front.body.topRight()).y, 62_cm);
    expect("front neck centre", f->position(front.body.topLeft()).y, 58_cm);
    expect("back neck centre", b->position(back.body.topLeft()).y, 64_cm);
    expect("neckline arrives level at the centre", f->edge(front.neck.after(front.body.top())).cubic->control2.y, 58_cm);
    expect("armhole is curved", f->edge(front.underarm.after(front.body.right())).arc.has_value(), true);
    expect("side seams match", f->length(front.body.right()), b->length(back.fold.image(back.body.right())));
    expect("shoulder seams match", f->length(front.body.top()), b->length(back.fold.image(back.body.top())));
    expect("seams found", static_cast<double>(shapes.seams.size()), 4);

    // 4. A bigger chest.
    doc.set(chest, 104_cm);
    shapes = evaluate(doc);
    f = &shapes[front.panel];
    b = &shapes[back.panel];

    std::printf("chest 104\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("half width", f->position(front.body.bottomRight()).x, 28_cm);
    expect("neck point stays", f->position(front.neck.corner()).x, 8_cm);
    expect("underarm height stays", f->position(front.underarm.corner()).y, 44_cm);
    expect("shoulder tip moves out", f->position(front.body.topRight()).x, 25_cm);
    expect("shoulder seams still match", f->length(front.body.top()), b->length(back.fold.image(back.body.top())));

    return failures == 0 ? 0 : 1;
}
