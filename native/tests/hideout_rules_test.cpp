#include "../core/hideout_rules.hpp"
#include "../core/hideout_history.hpp"
#include "../core/hideout_tools.hpp"
#include "../core/hideout_catalog.hpp"
#include <cstdio>
#include <limits>

int main() {
    using namespace ep::hideout::rules;
    int failed = 0, total = 0;
    const auto check = [&](bool good, const char* what) { ++total; if (!good) { ++failed; std::printf("FAIL %s\n", what); } };
    LayoutHistory history;history.Reset("A");
    check(!history.CanUndo()&&!history.CanRedo(),"new history has no phantom undo");
    check(!history.Record("A"),"no-op saves do not create edits");
    history.Record("B");history.Record("C");
    check(history.UndoCount()==2&&history.UndoTarget()=="B","completed edits form a multi-step history");
    check(history.Current()=="C","reading an undo target does not advance history before persistence succeeds");
    history.CommitUndo();check(history.Current()=="B"&&history.RedoTarget()=="C","undo exposes redo");
    history.CommitUndo();check(history.Current()=="A"&&history.RedoCount()==2,"second undo returns to the baseline");
    const ObjectIdentity identityA{1,1,-100,"assetA","Column"},identityB{2,1,-100,"assetA","Column"},identityC{3,2,1,"","Door"};
    check(MatchRestoredObjects({identityA,identityB,identityC},{identityC,identityB,identityA})==std::vector<int>{2,1,0},"cancel preserves survivor instances even after list reordering");
    check(MatchRestoredObjects({identityA,identityB,identityC},{identityB,identityC})==std::vector<int>{-1,0,1},"cancel recreates only the missing duplicate, keeping the exact-ID survivor");
    check(MatchRestoredObjects({identityA,identityC},{identityA,identityB,identityC})==std::vector<int>{0,2},"cancel discards only newly added objects");
    const ObjectIdentity newA{10,1,-100,"assetA","Column"},newB{11,1,-100,"assetA","Column"};
    check(MatchRestoredObjects({identityA,identityB,identityC},{newA,newB,identityC})==std::vector<int>{0,1,2},"cancel can reuse equivalent instances after undo regenerated IDs");
    const ObjectIdentity other{1,1,-100,"assetB","Different"};
    check(MatchRestoredObjects({identityA},{other})==std::vector<int>{-1},"a recycled ID never reuses a different catalog model");
    check(MatchRestoredObjects({identityC},{{3,2,2,"","Door"}})==std::vector<int>{-1},"functional door actions are distinct identities");
    history.CommitRedo();check(history.Current()=="B","redo reapplies exactly one edit");
    check(!history.Record("B")&&history.CanRedo(),"no-op after undo retains the redo branch");
    history.Record("D");check(!history.CanRedo()&&history.UndoTarget()=="B","a new edit replaces the abandoned redo branch");
    history.ReplaceCurrent("D fitted");check(history.Current()=="D fitted"&&history.UndoCount()==2,"asynchronous model fitting does not create a user edit");
    history.Reset("0");for(int i=1;i<=80;++i)history.Record(std::to_string(i));
    check(history.UndoCount()==50,"history retains the latest fifty edits");
    for(int i=0;i<50;++i)history.CommitUndo();check(history.Current()=="30"&&!history.CanUndo(),"bounded history has a stable oldest entry");
    for(int i=0;i<50;++i)history.CommitRedo();check(history.Current()=="80"&&!history.CanRedo(),"bounded history can redo all retained edits");
    LayoutHistory identities;identities.Reset("A",{1,2});identities.Record("B",{1,2,3});
    check(identities.UndoIds()==std::vector<int>{1,2}&&identities.CurrentIds()==std::vector<int>{1,2,3},"history retains chronological object identities without touching the durable layout format");
    const auto retained=identities.UndoIds();
    check(identities.Current()=="B"&&identities.CurrentIds()==std::vector<int>{1,2,3},"reading restore metadata does not advance history before persistence");
    identities.CommitUndo();identities.ReplaceCurrent("A",{10,2});
    check(identities.RedoIds()==std::vector<int>{1,2,3}&&identities.CurrentIds()==std::vector<int>{10,2},"recreated objects update only the restored history entry's live IDs");
    identities.Record("C",{10,2,4});check(!identities.CanRedo()&&identities.CurrentIds()==std::vector<int>{10,2,4},"new edits discard the same redo branch in layout and identity history");
    identities.Reset("0",{0});for(int i=1;i<=80;++i)identities.Record(std::to_string(i),{i});
    for(int i=0;i<50;++i)identities.CommitUndo();check(identities.Current()=="30"&&identities.CurrentIds()==std::vector<int>{30},"bounded history keeps layout text and identity metadata aligned");
    (void)retained;
    check(Snap(3.24f,.5f)==3&&Snap(-3.26f,.5f)==-3.5f&&Snap(3.24f,0)==3.24f,"grid snapping respects positive/negative positions and off");
    const auto follow=Follow({2,0,1,20,2},{0,0,0,0,1},{10,5,2,90,2});
    check(std::abs(follow.x-10)<.0001f&&std::abs(follow.z-1)<.0001f&&follow.lift==4&&follow.yaw==110&&follow.size==4,"group transform rotates, scales and lifts around its anchor");
    const auto identity=Follow({2,3,4,5,6},{0,0,0,0,1},{0,0,0,0,1});
    check(identity.x==2&&identity.z==3&&identity.lift==4&&identity.yaw==5&&identity.size==6,"unchanged group anchor preserves member transforms");
    check(FinitePlacement(0,0,0,0,1),"a normal placement is valid");
    check(!FinitePlacement(0,0,0,0,0),"zero scale is rejected");
    check(!FinitePlacement(0,0,0,0,-1),"negative scale is rejected");
    check(!FinitePlacement(0,0,std::numeric_limits<float>::infinity(),0,1),"infinite height is rejected");
    check(!FinitePlacement(std::numeric_limits<float>::quiet_NaN(),0,0,0,1),"NaN position is rejected");
    check(!Inside(kRound,38,38,0),"uneven edge allowance is enforced");
    const Rotation base{.70710678f,0,0,.70710678f};
    const auto noTurn=YawOnBase(0,base),turned=YawOnBase(90,base);
    check(std::abs(noTurn.x-base.x)<.00001f&&std::abs(noTurn.w-base.w)<.00001f,"zero yaw preserves imported pitch, rather than laying a door flat");
    check(std::abs(turned.x-.5f)<.00001f&&std::abs(turned.y-.5f)<.00001f&&std::abs(turned.z+.5f)<.00001f&&std::abs(turned.w-.5f)<.00001f,"yaw composes with imported door orientation");
    float hitX=0,hitZ=0;
    check(GroundRay({2,10,3},{.2f,-1,.4f},0,hitX,hitZ)&&std::abs(hitX-4)<.00001f&&std::abs(hitZ-7)<.00001f,"cursor ray intersects the ground independently of hover colliders");
    check(!GroundRay({0,3,0},{1,0,0},0,hitX,hitZ),"parallel cursor ray rejected");
    check(!GroundRay({0,3,0},{0,1,0},0,hitX,hitZ),"behind-camera ground intersection rejected");
    check(std::abs(RayBounds({0,5,-10},{0,0,1},{0,5,0},{1,1,1})-9)<.0001f,"model picked by actual visual bounds");
    check(RayBounds({4,5,-10},{0,0,1},{0,5,0},{1,1,1})<0,"nearby anchor is not mistaken for a ray hit");
    check(Inside(kRound,38,34,0,2),"bounded footprint stays inside");
    for (int shape = 0; shape < kShapes; ++shape) {
        check(Outline(shape, 38, 0, 0) > 5, "the middle is inside");
        check(Outline(shape, 38, 500, 500) < 0, "far away is outside");
        check(Outline(shape, 38, MaxExtent(shape, 38) + 1, MaxExtent(shape, 38) + 1) < 0, "the extent holds it");
    }
    check(Outline(kRound, 38, 37, 0) > 0 && Outline(kRound, 38, 39, 0) < 0, "a circle's edge");
    check(Outline(kHall, 38, 50, 0) > 0 && Outline(kHall, 38, 0, 30) < 0, "a hall is long");
    check(Outline(kCross, 38, 40, 0) > 0 && Outline(kCross, 38, 0, 40) > 0 && Outline(kCross, 38, 30, 30) < 0, "a cross has arms");
    check(Outline(kIslands, 38, 51, 0) > 0 && Outline(kIslands, 38, 34, 0) > 0, "islands are joined");
    check(Outline(kIslands, 38, 34, 10) < 0, "beside the path is outside");
    check(Height(3, 16) == 0, "flat inside");
    check(Height(-kBand, 16) == -16, "the rock's full depth");
    check(Height(-1, 16) < -3, "it falls fast");
    check(Mirrored(0, 12) == 0 && Mirrored(12, 12) == 1 && Mirrored(24, 12) == 0 && Mirrored(6, 12) == .5f && Mirrored(18, 12) == .5f, "mirrored repeats");
    check(Mirrored(-6, 12) == .5f, "both sides of the middle");
    const Point laid = GridPoint(6, 18, false);
    check(laid.x == 6 && laid.z == 18, "a laid floor stays straight");
    const Point origin = GridPoint(0, 0, true);
    check(std::abs(origin.x) > 3 && std::abs(origin.z) > 3, "the mirror's middle is not the ground's");
    // the grid never folds over itself: a step along it always goes on the same way
    bool sameWay = true;
    for (float a = -200; a < 200; a += 7) for (float b = -200; b < 200; b += 7) {
        const Point p = GridPoint(a, b, true), e = GridPoint(a + 2, b, true), n = GridPoint(a, b + 2, true);
        if ((e.x - p.x) * (n.z - p.z) - (e.z - p.z) * (n.x - p.x) <= 0) sameWay = false;
    }
    check(sameWay, "the bent grid does not fold");
    // every ready layout keeps every boss door on its ground, away from the edge and from the other doors
    const struct { int shape; float size; int layout; } ready[]{{kHall, 38, kLongRows}, {kSquare, 55, kRing}, {kRound, 55, kRing}, {kCross, 55, kArms}, {kRound, 38, kRows}, {kIslands, 38, kInnerRing}};
    for (const auto& r : ready) {
        bool onGround = true, apart = true;
        for (int i = 0; i < 10; ++i) {
            const Point a = BossDoor(r.layout, i, r.size);
            if (Outline(r.shape, r.size, a.x, a.z) < 3) onGround = false;
            for (int k = 0; k < i; ++k) { const Point b = BossDoor(r.layout, k, r.size); if (std::hypot(a.x - b.x, a.z - b.z) < 6) apart = false; }
        }
        check(onGround, "a layout's doors stand on its ground");
        check(apart, "a layout's doors stand apart");
    }
    check(PrimaryMouse(false)==0x01 && CancelMouse(false)==0x02,"normal mouse places with primary and cancels with secondary");
    check(PrimaryMouse(true)==0x02 && CancelMouse(true)==0x01,"swapped mouse keeps placement and cancellation distinct");
    KeyLatch latch;
    check(latch.Pressed(true,false),"a press works when another reader consumed the recent-press bit");
    check(!latch.Pressed(true,false),"held cancel does not repeat");
    check(!latch.Pressed(false,false),"release does not count as a press");
    check(latch.Pressed(false,true),"a short tap can use the recent-press fallback");
    check(!latch.Pressed(false,false),"short tap is not replayed");
    EditorInputGate input;
    check(!input.Allow(false,true),"background input cannot edit");
    check(!input.Allow(true,true)&&!input.armed,"held key from another window waits for release");
    check(!input.Allow(true,false)&&input.armed,"first clean frame drains stale key transitions");
    check(input.Allow(true,true),"fresh keys work after rearming");
    check(!input.Allow(false,false)&&!input.armed,"opening a menu disarms editor input");
    check(!input.Allow(true,false)&&input.Allow(true,false),"closing a menu rearms without replaying keys");
    check(kRotationKeys==std::array<int,4>{'Q','E',0x25,0x27},"Q/E rotate and arrows remain aliases; R is not reassigned");
    EditorRotation rotation;
    check(rotation.Step(1,1,100)==-15,"Q turns left immediately");
    check(rotation.Step(1,0,209)==0&&rotation.Step(1,0,210)==-15,"holding rotation repeats at a bounded interval");
    check(rotation.Step(2,2,211)==15,"E turns right immediately after Q");
    check(rotation.Step(3,3,212)==0&&rotation.Step(3,0,1000)==0,"opposite directions cancel without oscillating");
    check(rotation.Step(5,5,1001)==-15,"Q and Left together count as one turn");
    check(rotation.Step(10,10,1002)==15,"E and Right together count as one turn");
    check(rotation.Step(0,0,1003)==0&&rotation.Step(0,1,1004)==-15,"release is neutral and a short Q tap works");
    rotation={};check(rotation.Step(0,2,0)==15,"fresh editor rotation has no stale repeat delay");
    check(!PreemptPreview(false,false,30000),"background previews do not continually restart each other");
    check(!PreemptPreview(true,true,1000),"polling the active priority preview does not restart it");
    check(!PreemptPreview(true,false,249)&&PreemptPreview(true,false,250),"new hovered preview receives bounded preemption");
    const Point cell[4]{{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    Point clipped[8];
    const float all[4]{1, 1, 1, 1}, none[4]{-1, -1, -1, -1}, half[4]{1, -1, -1, 1};
    check(ClipCell(cell, all, clipped) == 4, "a cell inside stays whole");
    check(ClipCell(cell, none, clipped) == 0, "a cell outside goes");
    check(ClipCell(cell, half, clipped) == 4 && clipped[1].x == 1 && clipped[2].x == 1, "the edge runs where the distance runs out");
    check(ValidName("My hall 2") && ValidName("a_b-c"), "good names");
    check(!ValidName("") && !ValidName("../x") && !ValidName("a:b") && !ValidName(" a") && !ValidName("a "), "names that would leave the folder or trouble Windows");
    check(CatalogTag(CatalogLower("Garden_Road_Straight_1"),"floor"),"catalog road is a floor");
    check(!CatalogTag(CatalogLower("Garden_Pillar_Big"),"floor")&&CatalogTag(CatalogLower("Garden_Pillar_Big"),"column"),"pillars are not mouse-filtered as floors");
    check(!CatalogTag(CatalogLower("Fern1Standard"),"floor")&&CatalogTag(CatalogLower("Fern1Standard"),"plant"),"walkable plants remain selectable");
    std::printf("%s %d/%d hideout rules: outlines, edge, mirrored texture\n", failed ? "FAIL" : "PASS", total - failed, total);
    return failed ? 1 : 0;
}
