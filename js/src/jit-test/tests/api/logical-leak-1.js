
function testSimpleChain() {
    var b = { a: watchForLeak({ value : "simple-chain", toString: function () { return "a" } }), toString: function () { return "b" } };

    assertEq(liveWatchedObjects().length, 1);
    assertEq(liveWatchedObjects()[0][0], b.a);
    assertEq(liveWatchedObjects()[0][1], b);
    stopWatchingLeaks();
}

testSimpleChain();

function testWatchMultiple() {
    var c = {
       a: watchForLeak({ value : "a" }),
       b: watchForLeak({ value : "b" }),
       value: "c"
    };

    assertEq(liveWatchedObjects().length, 2);
    assertEq(liveWatchedObjects()[0][1], c);
    assertEq(liveWatchedObjects()[1][1], c);
    stopWatchingLeaks();
}

testWatchMultiple();

function testAliasedVar() {
    function wrap(x) {
        return function () { return x; };
    }

    var f = wrap(watchForLeak({ value : "aliased-var" }));
    assertEq(liveWatchedObjects().length, 1);
    assertEq(liveWatchedObjects()[0][0], f());
    assertEq(liveWatchedObjects()[0][1], f);
    stopWatchingLeaks();
}

testAliasedVar();
