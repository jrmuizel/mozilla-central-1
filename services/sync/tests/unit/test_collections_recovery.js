// Verify that we wipe the server if we have to regenerate keys.
Cu.import("resource://services-sync/service.js");

add_test(function test_missing_crypto_collection() {
  let johnHelper = track_collections_helper();
  let johnU      = johnHelper.with_updated_collection;
  let johnColls  = johnHelper.collections;
  
  let empty = false;
  function maybe_empty(handler) {
    return function (request, response) {
      if (empty) {
        let body = "{}";
        response.setStatusLine(request.httpVersion, 200, "OK");
        response.bodyOutputStream.write(body, body.length);
      } else {
        handler(request, response);
      }
    };
  }

  Service.serverURL = TEST_SERVER_URL;
  Service.clusterURL = TEST_CLUSTER_URL;
  Service.username = "johndoe";
  Service.password = "ilovejane";
  Service.passphrase = "a-aaaaa-aaaaa-aaaaa-aaaaa-aaaaa";

  let handlers = {
    "/1.1/johndoe/info/collections": maybe_empty(johnHelper.handler),
    "/1.1/johndoe/storage/crypto/keys": johnU("crypto", new ServerWBO("keys").handler()),
    "/1.1/johndoe/storage/meta/global": johnU("meta",   new ServerWBO("global").handler())
  };
  let collections = ["clients", "bookmarks", "forms", "history",
                     "passwords", "prefs", "tabs"];
  for each (let coll in collections) {
    handlers["/1.1/johndoe/storage/" + coll] =
      johnU(coll, new ServerCollection({}, true).handler());
  }
  let server = httpd_setup(handlers);

  try {
    let fresh = 0;
    let orig  = Service._freshStart;
    Service._freshStart = function() {
      _("Called _freshStart.");
      orig.call(Service);
      fresh++;
    };
    
    _("Startup, no meta/global: freshStart called once.");
    Service.sync();
    do_check_eq(fresh, 1);
    fresh = 0;

    _("Regular sync: no need to freshStart.");
    Service.sync();
    do_check_eq(fresh, 0);

    _("Simulate a bad info/collections.");
    delete johnColls.crypto;
    Service.sync();
    do_check_eq(fresh, 1);
    fresh = 0;

    _("Regular sync: no need to freshStart.");
    Service.sync();
    do_check_eq(fresh, 0);

  } finally {
    Svc.Prefs.resetBranch("");
    server.stop(run_next_test);
  }
});

function run_test() {
  initTestLogging("Trace");
  run_next_test();
}
