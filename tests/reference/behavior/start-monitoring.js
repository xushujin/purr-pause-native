function startMonitoring() {
  if (monitorInterval) return;
  detectScreenLock();
  monitorInterval = setInterval(() => {
    if (isOverlayShowing) return;
    if (isPaused) return;
    if (isScreenLocked) return;
    if (idleCheckPending) return;
    idleCheckPending = true;

    getIdleTimeMs((idleMs) => {
      idleCheckPending = false;
      const idleResetMs = 10 * 60 * 1000;
      if (idleMs > idleResetMs) {
        if (activeSeconds > 0) {
          logger.info('空闲超过10分钟, 计时归零 (空闲' + Math.round(idleMs / 1000) + '秒, 之前累计' + activeSeconds + '秒)');
        }
        activeSeconds = 0;
        updateTray();
        return;
      }

      if (idleMs < 180000) {
        activeSeconds += 10;
      }

      updateTray();

      if (activeSeconds >= (snoozeThreshold || config.thresholdMinutes * 60)) {
        snoozeThreshold = 0;
        logger.info('达到阈值, 触发动画 (累计' + activeSeconds + '秒)');
        triggerCat();
      }
    });
  }, 10000);
}

