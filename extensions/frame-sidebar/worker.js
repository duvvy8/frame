async function enableActionToggle() {
  await chrome.sidePanel.setPanelBehavior({openPanelOnActionClick: true});
}

chrome.runtime.onInstalled.addListener(enableActionToggle);
chrome.runtime.onStartup.addListener(enableActionToggle);
enableActionToggle();
