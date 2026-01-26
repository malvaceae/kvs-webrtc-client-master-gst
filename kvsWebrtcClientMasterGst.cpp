#include "common.hpp"

INT32 main(INT32 argc, CHAR* argv[])
{
  auto retStatus = STATUS_SUCCESS;
  std::unique_ptr<KvsWebrtcConfig> pKvsWebrtcConfig;
  PCHAR pChannelName;

  SET_INSTRUMENTED_ALLOCATORS();

  // ログレベル
  auto logLevel = setLogLevel();

  // チャネル名
  CHK_ERR(argc > 1, STATUS_INVALID_OPERATION, "チャネル名は必須です。");
  pChannelName = argv[1];

  // GStreamerを初期化
  gst_init(&argc, &argv);

  // KVS WebRTCの設定を作成
  CHK_STATUS(createKvsWebrtcConfig(pChannelName, logLevel, pKvsWebrtcConfig));

  // SIGINTハンドラを設定
  setSigintHandler(pKvsWebrtcConfig.get());

  // KVS WebRTCを初期化
  CHK_STATUS(initKvsWebRtc());

  // シグナリングクライアントを初期化
  CHK_STATUS(initSignaling(pKvsWebrtcConfig.get()));

  // メインループ
  CHK_STATUS(loopSignaling(pKvsWebrtcConfig.get()));

CleanUp:

  if (STATUS_FAILED(retStatus)) {
    DLOGE("ステータスコード「0x%08x」で終了しました。", retStatus);
  }

  // シグナリングクライアントを解放
  deinitSignaling(pKvsWebrtcConfig.get());

  // KVS WebRTCを終了
  deinitKvsWebRtc();

  // KVS WebRTCの設定を解放
  freeKvsWebrtcConfig(pKvsWebrtcConfig);

  // GStreamerを終了
  gst_deinit();

  RESET_INSTRUMENTED_ALLOCATORS();

  if (STATUS_FAILED(retStatus)) {
    return EXIT_FAILURE;
  } else {
    return EXIT_SUCCESS;
  }
}
