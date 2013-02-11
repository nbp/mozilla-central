
function testSimpleChain() {
    var b = { a: watchForLeak({ value : "simple-chain" }) };

    var result = liveWatchedObjects();
    assertEq(result.length, 1);
    assertEq(result[0].watched, b.a);
    assertEq(result[0].holder, b);
    stopWatchingLeaks();
}

testSimpleChain();

function testWatchMultiple() {
    var c = {
       a: watchForLeak({ value : "a" }),
       b: watchForLeak({ value : "b" }),
       value: "c"
    };

    var result = liveWatchedObjects();
    assertEq(result.length, 2);
    assertEq(result[0].holder, c);
    assertEq(result[1].holder, c);
    stopWatchingLeaks();
}

testWatchMultiple();

function testAliasedVar() {
    function wrap(x) {
        return function () { return x; };
    }

    var f = wrap(watchForLeak({ value : "aliased-var" }));

    var result = liveWatchedObjects();
    assertEq(result.length, 1);
    assertEq(result[0].watched, f());
    assertEq(result[0].holder, f);
    stopWatchingLeaks();
}

testAliasedVar();
