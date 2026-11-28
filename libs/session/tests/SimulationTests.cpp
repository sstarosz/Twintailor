// Unit tests of Simulation.cpp: the cloth stepped on request, and on its own thread.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/session/Simulation.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

std::shared_ptr<const sim::ClothMesh> aSheet(Document& doc, double width = 30) {
    const PanelId panel = doc.add(Panel{.name = "sheet"});
    doc.add(panel, Rect{.width = Expr(width), .height = 30_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 100, 0}});
    return std::make_shared<const sim::ClothMesh>(sim::meshOf(doc, evaluate(doc), {.particleDistance = 3_cm}));
}

double lowest(const sim::Placement& positions) {
    double low = 1e9;
    for (const auto& panel : positions)
        for (const Vec3& p : panel) low = std::min(low, p.y);
    return low;
}

} // namespace

TEST_CASE("stepped on request, the cloth moves one step at a time")
{
    Document doc;
    const auto mesh = aSheet(doc);
    Simulation simulation(Threading::OneStepPerAdvance);
    simulation.start(mesh, sim::arranged(doc, *mesh), sim::Body{});
    Simulation::News news;
    EXPECT(simulation.fetch(news), "starting is news");
    EXPECT((news.mesh == mesh && news.time == 0.0), "of the mesh it was given, at time zero");
    simulation.advance();
    EXPECT(!simulation.fetch(news), "not running, it does not step");
    simulation.run(true);
    for (int i = 0; i < 30; ++i) simulation.advance();
    EXPECT(simulation.fetch(news), "running, it does");
    EXPECT_NEAR(news.time, 0.5, "thirty steps are half a second");
    EXPECT(lowest(news.positions) < 85.0 - 1.0, "and the sheet has fallen");
}

TEST_CASE("a changed mesh is followed and the cloth carries on")
{
    Document doc;
    const auto mesh = aSheet(doc);
    Simulation simulation(Threading::OneStepPerAdvance);
    simulation.start(mesh, sim::arranged(doc, *mesh), sim::Body{});
    simulation.run(true);
    for (int i = 0; i < 20; ++i) simulation.advance();
    Simulation::News before;
    simulation.fetch(before);

    Document wider;
    const auto widerMesh = aSheet(wider, 36);
    simulation.follow(widerMesh, sim::arranged(wider, *widerMesh), {});
    Simulation::News after;
    simulation.fetch(after);
    EXPECT(after.mesh == widerMesh, "the news is of the new mesh");
    EXPECT(after.time == before.time, "time carries on");
    EXPECT(std::abs(lowest(after.positions) - lowest(before.positions)) < 3.0, "the cloth is still where it had fallen to");
}

TEST_CASE("stopped, there is no cloth")
{
    Document doc;
    const auto mesh = aSheet(doc);
    Simulation simulation(Threading::OneStepPerAdvance);
    simulation.start(mesh, sim::arranged(doc, *mesh), sim::Body{});
    simulation.stop();
    Simulation::News news;
    simulation.fetch(news);
    EXPECT((!simulation.exists() && news.mesh == nullptr && news.positions.empty()), "nothing is left");
}

TEST_CASE("on its own thread, the cloth moves without being asked")
{
    Document doc;
    const auto mesh = aSheet(doc);
    Simulation simulation(Threading::OwnThread);
    simulation.start(mesh, sim::arranged(doc, *mesh), sim::Body{});
    simulation.run(true);
    Simulation::News news;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until && news.time < 0.25) {
        simulation.fetch(news);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT(news.time >= 0.25, "a quarter of a second of cloth time comes on its own");
    EXPECT((news.time < 2.0), "at about the speed of the clock, not as fast as it can");
    simulation.run(false);
}

TEST_CASE("pulled by a hand that moves every 2 ms, the cloth keeps the clock's pace, and carries on at once when let go")
{
    // #107: every move of the hand woke the cloth's thread, which then
    // stepped at once and put its next step a sixtieth of a second further
    // off each time. The cloth ran ahead of the clock while pulled, and when
    // let go stood still until the clock caught up with it.
    Document doc;
    const auto mesh = aSheet(doc);
    Simulation simulation(Threading::OwnThread);
    simulation.start(mesh, sim::arranged(doc, *mesh), sim::Body{});
    simulation.run(true);
    Simulation::News news;
    const auto clock = [] { return std::chrono::steady_clock::now(); };
    const auto until = [&](double seconds, const auto& done) {
        const auto limit = clock() + std::chrono::duration<double>(seconds);
        while (clock() < limit && !done()) {
            simulation.fetch(news);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        simulation.fetch(news);
    };
    until(5.0, [&] { return news.time >= 0.1; });
    REQUIRE(news.time >= 0.1);

    const Vec3 corner = news.positions.front()[0];
    const double startTime = news.time;
    const auto began = clock();
    for (int i = 0; i < 250; ++i) {  // half a second of a hand moving every 2 ms
        simulation.hold(0, 0, corner + Vec3{0.0, 0.0, 0.04 * i});
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    simulation.fetch(news);
    const double pulledFor = std::chrono::duration<double>(clock() - began).count();
    std::printf("      pulled for %.2f s of the clock: %.2f s of cloth time\n", pulledFor, news.time - startTime);
    EXPECT((news.time - startTime <= pulledFor + 0.1), "while pulled, the cloth's time moves on no faster than the clock's");

    simulation.letGo();
    const double letGoAt = news.time;
    until(0.5, [] { return false; });
    std::printf("      in the half second after letting go: %.2f s of cloth time\n", news.time - letGoAt);
    EXPECT((news.time >= letGoAt + 0.3), "let go, it carries on at once instead of standing still until the clock catches up");
    simulation.run(false);
}

TEST_CASE("a held cloth runs though it was not told to, and stops again when let go")
{
    // #106: so that a paused cloth moves as soon as it is taken hold of, not
    // at the next frame of the window.
    Document doc;
    const auto mesh = aSheet(doc);
    Simulation simulation(Threading::OwnThread);
    simulation.start(mesh, sim::arranged(doc, *mesh), sim::Body{});
    Simulation::News news;
    simulation.fetch(news);
    for (int i = 0; i < 200 && !news.mesh; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        simulation.fetch(news);
    }
    REQUIRE(news.mesh);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    simulation.fetch(news);
    EXPECT((news.time == 0.0), "not told to run, it stands still");

    simulation.hold(0, 0, news.positions.front()[0] + Vec3{0.0, 0.0, 5.0});
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < limit && news.time < 0.05) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        simulation.fetch(news);
    }
    EXPECT((news.time >= 0.05), "held, it runs at once");

    simulation.letGo();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    simulation.fetch(news);
    const double stopped = news.time;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    simulation.fetch(news);
    EXPECT((news.time == stopped), "let go, it stands still again");

    // Stepped frame by frame, the same.
    Simulation stepped(Threading::OneStepPerAdvance);
    stepped.start(mesh, sim::arranged(doc, *mesh), sim::Body{});
    stepped.advance();
    Simulation::News steppedNews;
    stepped.fetch(steppedNews);
    EXPECT((steppedNews.time == 0.0), "stepped on request but not running, it does not move");
    stepped.hold(0, 0, steppedNews.positions.front()[0] + Vec3{0.0, 0.0, 5.0});
    stepped.advance();
    stepped.fetch(steppedNews);
    EXPECT((steppedNews.time > 0.0), "held, each request is a step");
}

TEST_CASE("a panel left out stays put in every cloth until it is taken back")
{
    Document doc;
    const auto mesh = aSheet(doc);
    const sim::Placement start = sim::arranged(doc, *mesh);
    Simulation simulation(Threading::OneStepPerAdvance);
    simulation.leaveOut({mesh->panels[0].panel});
    simulation.start(mesh, start, sim::Body{});
    simulation.run(true);
    Simulation::News news;
    const auto steps = [&](int count) {
        for (int i = 0; i < count; ++i) simulation.advance();
        simulation.fetch(news);
        return news.positions[0][0].y;
    };
    EXPECT(steps(20) == start[0][0].y, "told before there was a cloth, it stands still in it");
    simulation.follow(mesh, start, {});
    EXPECT(steps(20) == start[0][0].y, "and in the cloth carried over");
    simulation.restore(mesh, start, 1.0, false);
    EXPECT(steps(20) == start[0][0].y, "and in a cloth brought back");
    simulation.leaveOut({});
    EXPECT(steps(20) < start[0][0].y, "taken back, it falls");
}

TEST_CASE("on its own thread, waiting for the changes gives news of what was handed over")
{
    Document doc;
    const auto mesh = aSheet(doc);
    Simulation simulation(Threading::OwnThread);
    Simulation::News news;
    for (int i = 0; i < 20; ++i) {
        const auto next = std::make_shared<const sim::ClothMesh>(*mesh);
        simulation.start(next, sim::arranged(doc, *next), sim::Body{});
        simulation.waitForChanges();
        REQUIRE(simulation.fetch(news));
        REQUIRE(news.mesh == next);
    }
    simulation.run(true);  // while it steps, too
    const auto last = std::make_shared<const sim::ClothMesh>(*mesh);
    simulation.follow(last, sim::arranged(doc, *last), {});
    simulation.waitForChanges();
    REQUIRE(simulation.fetch(news));
    EXPECT(news.mesh == last, "the cloth has taken the new mesh");

    Simulation stepped(Threading::OneStepPerAdvance);
    stepped.waitForChanges();  // nothing to wait for: it returns
}
