module.exports = function (localStorage, messageKeys) {
function payloadValue(payload, name) {
  if (payload[name] !== undefined) return payload[name];
  var key = messageKeys[name];
  if (key !== undefined && payload[key] !== undefined) return payload[key];
  if (key !== undefined && payload[String(key)] !== undefined) {
    return payload[String(key)];
  }
  return undefined;
}

function normalizeGetmeUrl(url) {
  url = String(url || '').replace(/^\s+|\s+$/g, '');
  if (!/^https?:\/\//i.test(url)) return '';
  if (url.indexOf('?') === -1 && url.indexOf('#') === -1 &&
      !/\/$/.test(url) && !/\/[^\/]+\.[^\/]+$/.test(url)) {
    url += '/';
  }
  return url;
}

function getConfiguredUrl() {
  // Clay is the settings source of truth. The separate URL is a legacy cache
  // and must not override a newly saved (or deliberately cleared) setting.
  try {
    var claySettings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
    if (claySettings &&
        Object.prototype.hasOwnProperty.call(claySettings, 'getmeUrl')) {
      return normalizeGetmeUrl(claySettings.getmeUrl);
    }
  } catch (e) {}
  return normalizeGetmeUrl(localStorage.getItem('getmeUrl') || '');
}


return { url: getConfiguredUrl, value: payloadValue };
};
