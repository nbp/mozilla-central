/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 60000;

SpecialPowers.addPermission("sms", true, document);
SpecialPowers.setBoolPref("dom.sms.enabled", true);

let manager = window.navigator.mozMobileMessage;
let numberMsgs = 10;
let smsList = new Array();

function verifyInitialState() {
  log("Verifying initial state.");
  ok(manager instanceof MozMobileMessageManager,
     "manager is instance of " + manager.constructor);
  // Ensure test is starting clean with no existing sms messages
  deleteAllMsgs(simulateIncomingSms);
}

function deleteAllMsgs(nextFunction) {
  let msgList = new Array();
  let filter = new MozSmsFilter;

  let cursor = manager.getMessages(filter, false);
  ok(cursor instanceof DOMCursor,
      "cursor is instanceof " + cursor.constructor);

  cursor.onsuccess = function(event) {
    // Check if message was found
    if (cursor.result) {
      msgList.push(cursor.result.id);
      // Now get next message in the list
      cursor.continue();
    } else {
      // No (more) messages found
      if (msgList.length) {
        log("Found " + msgList.length + " SMS messages to delete.");
        deleteMsgs(msgList, nextFunction);
      } else {
        log("No SMS messages found.");
        nextFunction();
      }
    }
  };

  cursor.onerror = function(event) {
    log("Received 'onerror' event.");
    ok(event.target.error, "domerror obj");
    log("manager.getMessages error: " + event.target.error.name);
    ok(false,"Could not get SMS messages");
    cleanUp();
  };
}

function deleteMsgs(msgList, nextFunction) {
  let smsId = msgList.shift();

  log("Deleting SMS (id: " + smsId + ").");
  let request = manager.delete(smsId);
  ok(request instanceof DOMRequest,
      "request is instanceof " + request.constructor);

  request.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    if (event.target.result) {
      // Message deleted, continue until none are left
      if (msgList.length) {
        deleteMsgs(msgList, nextFunction);
      } else {
        log("Finished deleting SMS messages.");
        nextFunction();
      }
    } else {
      log("SMS delete failed.");
      ok(false,"manager.delete request returned false");
      cleanUp();
    }
  };

  request.onerror = function(event) {
    log("Received 'onerror' smsrequest event.");
    ok(event.target.error, "domerror obj");
    ok(false, "manager.delete request returned unexpected error: "
        + event.target.error.name );
    cleanUp();
  };
}

function simulateIncomingSms() {
  let text = "Incoming SMS number " + (smsList.length + 1);
  let remoteNumber = "5552229797";

  log("Simulating incoming SMS number " + (smsList.length + 1) + " of "
      + numberMsgs + ".");

  // Simulate incoming sms sent from remoteNumber to our emulator
  rcvdEmulatorCallback = false;
  runEmulatorCmd("sms send " + remoteNumber + " " + text, function(result) {
    is(result[0], "OK", "emulator callback");
    rcvdEmulatorCallback = true;
  });
}

// Callback for incoming sms
manager.onreceived = function onreceived(event) {
  log("Received 'onreceived' sms event.");
  let incomingSms = event.message;
  log("Received SMS (id: " + incomingSms.id + ").");

  smsList.push(incomingSms);

  // Wait for emulator to catch up before continuing
  waitFor(nextRep,function() {
    return(rcvdEmulatorCallback);
  });
};

function nextRep() {
  if (smsList.length < numberMsgs) {
    simulateIncomingSms();
  } else {
    // Now test the filter
    getMsgs();
  }
}

function getMsgs() {
  var filter = new MozSmsFilter();
  let foundSmsList = new Array();

  // Set filter for start date yesterday and end date tomorrow
  let yesterday = new Date(Date.now() - 86400000); // 24 hours = 86400000 ms
  let tomorrow = new Date(Date.now() + 86400000);
  filter.startDate = yesterday;
  filter.endDate = tomorrow;

  log("Getting SMS messages with dates between " + yesterday + " and "
      + tomorrow +".");
  let cursor = manager.getMessages(filter, false);
  ok(cursor instanceof DOMCursor,
      "cursor is instanceof " + cursor.constructor);

  cursor.onsuccess = function(event) {
    log("Received 'onsuccess' event.");

    if (cursor.result) {
      // Another message found
      log("Got SMS (id: " + cursor.result.id + ").");
      // Store found message
      foundSmsList.push(cursor.result);
      // Now get next message in the list
      cursor.continue();
    } else {
      // No more messages; ensure correct number found
      if (foundSmsList.length == smsList.length) {
        log("SMS getMessages returned " + foundSmsList.length +
            " messages as expected.");
        verifyFoundMsgs(foundSmsList);
      } else {
        log("SMS getMessages returned " + foundSmsList.length +
            " messages, but expected " + smsList.length + ".");
        ok(false, "Incorrect number of messages returned by manager.getMessages");
        deleteAllMsgs(cleanUp);
      }
    }
  };

  cursor.onerror = function(event) {
    log("Received 'onerror' event.");
    ok(event.target.error, "domerror obj");
    log("manager.getMessages error: " + event.target.error.name);
    ok(false,"Could not get SMS messages");
    cleanUp();
  };
}

function verifyFoundMsgs(foundSmsList) {
  for (var x = 0; x < foundSmsList.length; x++) {
    is(foundSmsList[x].id, smsList[x].id, "id");
    is(foundSmsList[x].timestamp.getTime(), smsList[x].timestamp.getTime(),
        "timestmap");
  }
  deleteAllMsgs(cleanUp);
}

function cleanUp() {
  manager.onreceived = null;
  SpecialPowers.removePermission("sms", document);
  SpecialPowers.clearUserPref("dom.sms.enabled");
  finish();
}

// Start the test
verifyInitialState();
