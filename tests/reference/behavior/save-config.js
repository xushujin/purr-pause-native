ipcMain.on('save-config', (event, newConfig) => {
  newConfig = newConfig || {};
  const hasMessageConfig = Object.prototype.hasOwnProperty.call(newConfig, 'messageNotifyEnabled')
    || Object.prototype.hasOwnProperty.call(newConfig, 'messageApiBaseUrl')
    || Object.prototype.hasOwnProperty.call(newConfig, 'messageAuthHeaders')
    || Object.prototype.hasOwnProperty.call(newConfig, 'messageSources')
    || Object.prototype.hasOwnProperty.call(newConfig, 'messagePollInterval')
    || Object.prototype.hasOwnProperty.call(newConfig, 'messageMaxCacheItems')
    || Object.prototype.hasOwnProperty.call(newConfig, 'messageNotifyDir')
    || Object.prototype.hasOwnProperty.call(newConfig, 'messageNotifyVideo');
  if (hasMessageConfig) {
    const messageResult = configLib.applyMessageConfig(config, newConfig, assets.generateMessageNotifyReadmeInDir);
    if (!messageResult.success) {
      event.sender.send('save-config-result', messageResult);
      return;
    }
  }

  if (newConfig.thresholdMinutes !== undefined) {
    config.thresholdMinutes = Math.max(1, Math.min(480, parseInt(newConfig.thresholdMinutes) || 45));
  }
  if (newConfig.breakMinutes !== undefined) {
    config.breakMinutes = Math.max(1, Math.min(60, parseInt(newConfig.breakMinutes) || 5));
  }
  const validModes = ['walk-zoom', 'fade-center', 'walk-flat', 'walk-center'];
  if (newConfig.animationMode !== undefined && validModes.includes(newConfig.animationMode)) {
    const licStatus = license.checkStatus(app.getPath('userData'));
    if (licStatus.status === 'active') {
      config.animationMode = newConfig.animationMode;
    }
  }
  if (newConfig.autoLaunch !== undefined) {
    autostart.setEnabled(!!newConfig.autoLaunch);
  }
  if (newConfig.customWebmDir !== undefined) {
    const licenseStatus = license.checkStatus(app.getPath('userData'));
    if (licenseStatus.status === 'active') {
      if (newConfig.customWebmDir) {
        try {
          const stat = fs.statSync(newConfig.customWebmDir);
          if (stat.isDirectory()) {
            config.customWebmDir = newConfig.customWebmDir;
            assets.generateReadmeInDir(newConfig.customWebmDir);
          }
        } catch (e) {}
      } else {
        config.customWebmDir = '';
      }
    }
  }
  if (newConfig.logEnabled !== undefined) {
    config.logEnabled = !!newConfig.logEnabled;
    logger.setEnabled(config.logEnabled);
  }
  configLib.save({ userConfigPath: USER_CONFIG_PATH, config, isDev: process.argv.includes('--dev') });
  if (messageNotify) messageNotify.updateConfig(config);
  messageBadgeCount = messageNotify ? messageNotify.getBadgeCount() : 0;
  updateTray();
  rebuildTrayMenu();
  logger.info('配置已保存: thresholdMinutes=' + config.thresholdMinutes + ', breakMinutes=' + config.breakMinutes + ', animationMode=' + config.animationMode);
  event.sender.send('save-config-result', { success: true });
});

