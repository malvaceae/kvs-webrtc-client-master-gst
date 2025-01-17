#include "commons.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

INT32 main(INT32 argc, CHAR* argv[])
{
  STATUS retStatus = STATUS_SUCCESS;
  PKvsWebrtcConfig pKvsWebrtcConfig = NULL;
  PCHAR pChannelName;

  SET_INSTRUMENTED_ALLOCATORS();

  // ログレベル
  UINT32 logLevel = setLogLevel();

  // SIGINTハンドラを設定
  signal(SIGINT, sigintHandler);

  // チャネル名
  CHK_ERR(argc > 1, STATUS_INVALID_OPERATION, "チャネル名は必須です。");
  pChannelName = argv[1];

  // GStreamerを初期化
  gst_init(&argc, &argv);

  // KVS WebRTCの設定を作成
  CHK_STATUS(createKvsWebrtcConfig(pChannelName, logLevel, &pKvsWebrtcConfig));

  // KVS WebRTCを初期化
  CHK_STATUS(initKvsWebRtc());

  // シグナリングクライアントを初期化
  CHK_STATUS(initSignaling(pKvsWebrtcConfig));

  // メインループ
  while (!ATOMIC_LOAD_BOOL(&pKvsWebrtcConfig->isInterrupted)) {
    // ロックを開始
    MUTEX_LOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);

    // 5秒間スリープ
    CVAR_WAIT(pKvsWebrtcConfig->cvar,
              pKvsWebrtcConfig->kvsWebrtcConfigObjLock,
              5 * HUNDREDS_OF_NANOS_IN_A_SECOND);

    // ロックを解除
    MUTEX_UNLOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  }

CleanUp:

  if (STATUS_FAILED(retStatus)) {
    DLOGE("Terminated with status code: 0x%08x", retStatus);
  }

  if (pKvsWebrtcConfig) {
    // シグナリングクライアントを解放
    retStatus = freeSignalingClient(&pKvsWebrtcConfig->signalingHandle);

    if (STATUS_FAILED(retStatus)) {
      DLOGE("freeSignalingClient(): operation returned status code: 0x%08x", retStatus);
    }

    // KVS WebRTCの設定を解放
    retStatus = freeKvsWebrtcConfig(&pKvsWebrtcConfig);

    if (STATUS_FAILED(retStatus)) {
      DLOGE("freeKvsWebrtcConfig(): operation returned status code: 0x%08x", retStatus);
    }
  }

  RESET_INSTRUMENTED_ALLOCATORS();

  if (STATUS_FAILED(retStatus)) {
    return EXIT_FAILURE;
  } else {
    return EXIT_SUCCESS;
  }
}
