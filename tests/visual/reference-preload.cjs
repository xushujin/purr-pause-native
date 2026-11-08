const { contextBridge } = require('electron');

const argument = process.argv.find(value => value.startsWith('--purr-visual-fixture='));
if (!argument) throw new Error('Missing visual fixture');
const fixture = JSON.parse(Buffer.from(argument.slice('--purr-visual-fixture='.length), 'base64').toString('utf8'));
const noop = () => {};
contextBridge.exposeInMainWorld('electronAPI', {
  onLoadConfig: callback => queueMicrotask(() => callback(fixture.config)),
  onMessagesState: callback => queueMicrotask(() => callback(fixture.state)),
  onLicenseStatus: callback => queueMicrotask(() => callback(fixture.license)),
  onSaveConfigResult: noop, onWebmDirPicked: noop, onTestMessageConnectionResult: noop,
  onDownloadMessageApiSpecResult: noop, onActivationResult: noop,
  pickWebmDir: noop, saveConfig: noop, closeSettings: noop, testMessageConnection: noop,
  downloadMessageApiSpec: noop, refreshMessages: noop, openMessageTarget: noop,
  activate: noop, skipActivation: noop
});
