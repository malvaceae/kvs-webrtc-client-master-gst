#include "kvs.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

INT32 main(INT32 argc, CHAR* argv[])
{
  STATUS retStatus = STATUS_SUCCESS;
  PKvsWebrtcConfig pKvsWebrtcConfig = nullptr;
  PCHAR pChannelName;
  UINT32 logLevel;

  SET_INSTRUMENTED_ALLOCATORS();

  // ログレベル
  logLevel = setLogLevel();

  // SIGINTハンドラを設定
  setSigintHandler(pKvsWebrtcConfig);

  // チャネル名
  CHK_ERR(argc > 1, STATUS_INVALID_OPERATION, "チャネル名は必須です。");
  pChannelName = argv[1];

  // GStreamerを初期化
  gst_init(&argc, &argv);

  // KVS WebRTCの設定を作成
  CHK_STATUS(createKvsWebrtcConfig(pChannelName, logLevel, pKvsWebrtcConfig));

  // KVS WebRTCを初期化
  CHK_STATUS(initKvsWebRtc());

  // シグナリングクライアントを初期化
  CHK_STATUS(initSignaling(pKvsWebrtcConfig));

  // メインループ
  CHK_STATUS(loopSignaling(pKvsWebrtcConfig));

CleanUp:

  if (STATUS_FAILED(retStatus)) {
    DLOGE("ステータスコード「0x%08x」で終了しました。", retStatus);
  }

  if (pKvsWebrtcConfig) {
    // シグナリングクライアントを解放
    deinitSignaling(pKvsWebrtcConfig);

    // KVS WebRTCを終了
    deinitKvsWebRtc();

    // KVS WebRTCの設定を解放
    freeKvsWebrtcConfig(pKvsWebrtcConfig);
  }

  RESET_INSTRUMENTED_ALLOCATORS();

  if (STATUS_FAILED(retStatus)) {
    return EXIT_FAILURE;
  } else {
    return EXIT_SUCCESS;
  }
}
