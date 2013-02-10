
function testSimpleChain() {
    var b = { a: watchForLeak({ value : "simple-chain", toString: function () { return "a" } }), toString: function () { return "b" } };

    assertEq(liveWatchedObjects().length, 1);
    assertEq(liveWatchedObjects()[0][0], b.a);
    assertEq(liveWatchedObjects()[0][1], b);
    stopWatchingLeaks();
}

testSimpleChain();

function testAliasedVar() {
    function wrap(x) {
        return function () { return x; };
    }

    var f = wrap(watchForLeak({ value : "aliased-var" }));
    assertEq(liveWatchedObjects().length, 1);
    assertEq(liveWatchedObjects()[0][0], f());
    // Hum, this is confusing, the scope object has all the properties of the
    // scope but it has a null proto which means it is printed as being null,
    // but it is not null.  Also, attempt to print the scope object cause SEGV,
    // better considering it as a non-object.
    assertEq(liveWatchedObjects()[0][1] === null, false);
    assertEq(liveWatchedObjects()[0][1].x, f());
    stopWatchingLeaks();
}

testAliasedVar();