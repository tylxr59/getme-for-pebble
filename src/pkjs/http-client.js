module.exports = function (XHR) {
  return function (url, body, callback) {
    var req = new XHR();
    var finished = false;
    function finish(error, data) {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      callback(error, data);
    }
    function temporary(message) {
      var error = new Error(message);
      error.retryable = true;
      return error;
    }
    var timer = setTimeout(function () {
      finish(temporary('Network timeout; still pending'));
      try { req.abort(); } catch (e) {}
    }, 15000);
    req.open('POST', url, true);
    req.setRequestHeader('Content-Type', 'application/json');
    req.onload = function () {
      var data;
      try { data = JSON.parse(req.responseText); }
      catch (e) { return finish(temporary('Bad server response')); }
      if (req.status < 200 || req.status >= 300 || !data.success) {
        var error = new Error(data.error || ('HTTP ' + req.status));
        error.retryable = req.status >= 500 || req.status === 0 || req.status === 429;
        error.code = data.code;
        return finish(error);
      }
      if (data.version !== 1) return finish(new Error('Update the getme server'));
      finish(null, data);
    };
    req.onerror = function () { finish(temporary('Network error; still pending')); };
    try { req.send(JSON.stringify(body)); }
    catch (e) { finish(temporary('Network error; still pending')); }
  };
};
