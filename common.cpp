#include "common.hpp"
#include <functional>

namespace {
  std::function<VOID(INT32)> sigintHandler;
}

// ============================================================================
// ユーティリティ
// ============================================================================

/**
 * @brief ログレベルを設定する
 */
UINT32 setLogLevel()
{
  PCHAR pLogLevel;
  UINT32 logLevel;

  if (!(pLogLevel = GETENV(DEBUG_LOG_LEVEL_ENV_VAR)) || STATUS_FAILED(STRTOUI32(pLogLevel, NULL, 10, &logLevel))) {
    logLevel = LOG_LEVEL_WARN;
  }

  SET_LOGGER_LOG_LEVEL(logLevel);
  return logLevel;
}

/**
 * @brief SIGINTハンドラを設定する
 */
VOID setSigintHandler(PKvsWebrtcConfig pKvsWebrtcConfig)
{
  // SIGINTハンドラ
  sigintHandler = [pKvsWebrtcConfig](INT32 sigNum) {
    UNUSED_PARAM(sigNum);

    if (pKvsWebrtcConfig) {
      // 中断フラグをON
      ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isInterrupted, TRUE);

      // ブロックを解除
      if (IS_VALID_CVAR_VALUE(pKvsWebrtcConfig->cvar)) {
        CVAR_BROADCAST(pKvsWebrtcConfig->cvar);
      }
    }
  };

  // SIGINTハンドラを設定
  signal(SIGINT, [](INT32 sigNum) {
    sigintHandler(sigNum);
  });
}

/**
 * @brief CA証明書のパスを取得する
 */
STATUS getCaCertPath(PCHAR& pCaCertPath)
{
  auto retStatus = STATUS_SUCCESS;

  // CA証明書のパス
  CHK_ERR(pCaCertPath = GETENV(CACERT_PATH_ENV_VAR), STATUS_INVALID_OPERATION, "環境変数「%s」は必須です。", CACERT_PATH_ENV_VAR);

CleanUp:

  return retStatus;
}

// ============================================================================
// KvsWebrtcConfig 管理
// ============================================================================

/**
 * @brief KVS WebRTCの設定を作成する
 */
STATUS createKvsWebrtcConfig(PCHAR pChannelName, UINT32 logLevel, std::unique_ptr<KvsWebrtcConfig>& pKvsWebrtcConfig)
{
  auto retStatus = STATUS_SUCCESS;

  // KVS WebRTCの設定を初期化
  pKvsWebrtcConfig = std::make_unique<KvsWebrtcConfig>();

  // 接続フラグ
  ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isConnected, FALSE);

  // 中断フラグ
  ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isInterrupted, FALSE);

  // 終了フラグ
  ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isTerminated, FALSE);

  // ミューテックス
  pKvsWebrtcConfig->kvsWebrtcConfigObjLock = MUTEX_CREATE(TRUE);

  // 条件変数
  pKvsWebrtcConfig->cvar = CVAR_CREATE();

  // ICEサーバーの数
  pKvsWebrtcConfig->iceUriCount = 0;

  // CA証明書のパスを取得
  CHK_STATUS(getCaCertPath(pKvsWebrtcConfig->pCaCertPath));

  // 認証情報プロバイダーを作成
  CHK_STATUS(createCredentialProvider(pKvsWebrtcConfig->pCaCertPath, pKvsWebrtcConfig->pCredentialProvider));

  // クライアント情報を初期化
  CHK_STATUS(initClientInfo(logLevel, pKvsWebrtcConfig->clientInfo));

  // チャネル情報を初期化
  CHK_STATUS(initChannelInfo(pChannelName,
                             pKvsWebrtcConfig->pCaCertPath,
                             GETENV(DEFAULT_REGION_ENV_VAR),
                             pKvsWebrtcConfig->channelInfo));

  // コールバックを初期化
  CHK_STATUS(initCallbacks(reinterpret_cast<UINT64>(pKvsWebrtcConfig.get()), pKvsWebrtcConfig->callbacks));

  // シグナリングクライアントに関するメトリクス初期化
  CHK_STATUS(initMetrics(pKvsWebrtcConfig->metrics));

CleanUp:

  if (STATUS_FAILED(retStatus)) {
    freeKvsWebrtcConfig(pKvsWebrtcConfig);
  }

  return retStatus;
}

/**
 * @brief KVS WebRTCの設定を解放する
 */
STATUS freeKvsWebrtcConfig(std::unique_ptr<KvsWebrtcConfig>& pKvsWebrtcConfig)
{
  ENTERS();
  auto retStatus = STATUS_SUCCESS;

  // NULLチェック
  CHK(pKvsWebrtcConfig, retStatus);

  // ミューテックスを解放
  if (IS_VALID_MUTEX_VALUE(pKvsWebrtcConfig->kvsWebrtcConfigObjLock)) {
    MUTEX_FREE(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  }

  // 条件変数を解放
  if (IS_VALID_CVAR_VALUE(pKvsWebrtcConfig->cvar)) {
    CVAR_FREE(pKvsWebrtcConfig->cvar);
  }

  // ストリーミングセッションを解放
  for (auto&& value : pKvsWebrtcConfig->streamingSessions) {
    freeKvsWebrtcStreamingSession(value.second);
  }

  // ストリーミングセッションをクリア
  pKvsWebrtcConfig->streamingSessions.clear();

  // 認証情報プロバイダーを解放
  freeIotCredentialProvider(&pKvsWebrtcConfig->pCredentialProvider);

  // KVS WebRTCの設定を解放
  pKvsWebrtcConfig.reset();

CleanUp:

  LEAVES();
  return retStatus;
}

// ============================================================================
// KvsWebrtcStreamingSession 管理
// ============================================================================

/**
 * @brief ストリーミングセッションを作成する
 */
STATUS createKvsWebrtcStreamingSession(PKvsWebrtcConfig pKvsWebrtcConfig, PCHAR pPeerClientId, std::unique_ptr<KvsWebrtcStreamingSession>& pStreamingSession)
{
  auto retStatus = STATUS_SUCCESS;
  RtcMediaStreamTrack videoTrack;
  RtcMediaStreamTrack audioTrack;
  RtcRtpTransceiverInit videoRtpTransceiverInit;
  RtcRtpTransceiverInit audioRtpTransceiverInit;

  // 映像と音声のトラックを初期化
  MEMSET(&videoTrack, 0x00, SIZEOF(RtcMediaStreamTrack));
  MEMSET(&audioTrack, 0x00, SIZEOF(RtcMediaStreamTrack));

  // ストリーミングセッションを初期化
  pStreamingSession = std::make_unique<KvsWebrtcStreamingSession>();

  // KVS WebRTCの設定
  pStreamingSession->pKvsWebrtcConfig = pKvsWebrtcConfig;

  // クライアントID
  STRCPY(pStreamingSession->peerClientId, pPeerClientId);

  // 終了フラグ
  ATOMIC_STORE_BOOL(&pStreamingSession->isTerminated, FALSE);

  // ICE候補収集完了フラグ
  ATOMIC_STORE_BOOL(&pStreamingSession->candidateGatheringDone, FALSE);

  // Trickle ICEフラグ (handleOfferで設定される)
  pStreamingSession->remoteCanTrickleIce = FALSE;

  // ピア接続を初期化
  CHK_STATUS(initPeerConnection(pKvsWebrtcConfig, pStreamingSession->pPeerConnection));

  // ICE Candidateを受信した際のコールバックを設定
  CHK_STATUS(peerConnectionOnIceCandidate(pStreamingSession->pPeerConnection,
                                          reinterpret_cast<UINT64>(pStreamingSession.get()),
                                          onIceCandidateHandler));

  // ピア接続の状態が変化した際のコールバックを設定
  CHK_STATUS(peerConnectionOnConnectionStateChange(pStreamingSession->pPeerConnection,
                                                   reinterpret_cast<UINT64>(pStreamingSession.get()),
                                                   onConnectionStateChanged));

  // サポートされるコーデックを追加
  CHK_STATUS(addSupportedCodec(pStreamingSession->pPeerConnection, VIDEO_CODEC));
  CHK_STATUS(addSupportedCodec(pStreamingSession->pPeerConnection, AUDIO_CODEC));

  // トラックの種類
  videoTrack.kind = MEDIA_STREAM_TRACK_KIND_VIDEO;
  audioTrack.kind = MEDIA_STREAM_TRACK_KIND_AUDIO;

  // トラックのコーデック
  videoTrack.codec = VIDEO_CODEC;
  audioTrack.codec = AUDIO_CODEC;

  // トランシーバーの方向
  videoRtpTransceiverInit.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
  audioRtpTransceiverInit.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;

  // トラックのストリームID
  STRCPY(videoTrack.streamId, VIDEO_STREAM_ID);
  STRCPY(audioTrack.streamId, AUDIO_STREAM_ID);

  // トラックのID
  STRCPY(videoTrack.trackId, VIDEO_TRACK_ID);
  STRCPY(audioTrack.trackId, AUDIO_TRACK_ID);

  // トランシーバーを追加
  CHK_STATUS(addTransceiver(pStreamingSession->pPeerConnection, &videoTrack, &videoRtpTransceiverInit, &pStreamingSession->pVideoRtcRtpTransceiver));
  CHK_STATUS(addTransceiver(pStreamingSession->pPeerConnection, &audioTrack, &audioRtpTransceiverInit, &pStreamingSession->pAudioRtcRtpTransceiver));

CleanUp:

  if (STATUS_FAILED(retStatus)) {
    freeKvsWebrtcStreamingSession(pStreamingSession);
  }

  return retStatus;
}

/**
 * @brief ストリーミングセッションを解放する
 */
STATUS freeKvsWebrtcStreamingSession(std::unique_ptr<KvsWebrtcStreamingSession>& pStreamingSession)
{
  auto retStatus = STATUS_SUCCESS;

  // NULLチェック
  CHK(pStreamingSession, retStatus);

  // ピア接続を解放
  CHK_LOG_ERR(closePeerConnection(pStreamingSession->pPeerConnection));
  CHK_LOG_ERR(freePeerConnection(&pStreamingSession->pPeerConnection));

  // ストリーミングセッションを解放
  pStreamingSession.reset();

CleanUp:

  CHK_LOG_ERR(retStatus);

  return retStatus;
}

// ============================================================================
// 初期化
// ============================================================================

/**
 * @brief 認証情報プロバイダーを作成する
 */
STATUS createCredentialProvider(PCHAR pCaCertPath, PAwsCredentialProvider& pCredentialProvider)
{
  auto retStatus = STATUS_SUCCESS;
  PCHAR pIotCoreCredentialEndPoint;
  PCHAR pIotCoreCert;
  PCHAR pIotCorePrivateKey;
  PCHAR pIotCoreRoleAlias;
  PCHAR pIotCoreThingName;

  // 認証情報プロバイダー
  CHK_ERR(pIotCoreCredentialEndPoint = GETENV(IOT_CORE_CREDENTIAL_ENDPOINT), STATUS_INVALID_OPERATION, "環境変数「%s」は必須です。", IOT_CORE_CREDENTIAL_ENDPOINT);
  CHK_ERR(pIotCoreCert               = GETENV(IOT_CORE_CERT),                STATUS_INVALID_OPERATION, "環境変数「%s」は必須です。", IOT_CORE_CERT);
  CHK_ERR(pIotCorePrivateKey         = GETENV(IOT_CORE_PRIVATE_KEY),         STATUS_INVALID_OPERATION, "環境変数「%s」は必須です。", IOT_CORE_PRIVATE_KEY);
  CHK_ERR(pIotCoreRoleAlias          = GETENV(IOT_CORE_ROLE_ALIAS),          STATUS_INVALID_OPERATION, "環境変数「%s」は必須です。", IOT_CORE_ROLE_ALIAS);
  CHK_ERR(pIotCoreThingName          = GETENV(IOT_CORE_THING_NAME),          STATUS_INVALID_OPERATION, "環境変数「%s」は必須です。", IOT_CORE_THING_NAME);
  CHK_STATUS(createLwsIotCredentialProvider(pIotCoreCredentialEndPoint,
                                            pIotCoreCert,
                                            pIotCorePrivateKey,
                                            pCaCertPath,
                                            pIotCoreRoleAlias,
                                            pIotCoreThingName,
                                            &pCredentialProvider));

CleanUp:

  return retStatus;
}

/**
 * @brief クライアント情報を初期化する
 */
STATUS initClientInfo(UINT32 logLevel, SignalingClientInfo& clientInfo)
{
  auto retStatus = STATUS_SUCCESS;

  // クライアント情報のバージョン
  clientInfo.version = SIGNALING_CLIENT_INFO_CURRENT_VERSION;

  // クライアントID
  STRCPY(clientInfo.clientId, CLIENT_ID);

  // ログレベル
  clientInfo.loggingLevel = logLevel;

  // キャッシュファイルのパス
  clientInfo.cacheFilePath = NULL;

  // 再作成の試行回数
  clientInfo.signalingClientCreationMaxRetryAttempts = CREATE_SIGNALING_CLIENT_RETRY_ATTEMPTS_SENTINEL_VALUE;

CleanUp:

  return retStatus;
}

/**
 * @brief チャネル情報を初期化する
 */
STATUS initChannelInfo(PCHAR pChannelName, PCHAR pCaCertPath, PCHAR pRegion, ChannelInfo& channelInfo)
{
  auto retStatus = STATUS_SUCCESS;

  // チャネル情報のバージョン
  channelInfo.version = CHANNEL_INFO_CURRENT_VERSION;

  // チャネル名
  channelInfo.pChannelName = pChannelName;

  // リージョン
  channelInfo.pRegion = pRegion ? pRegion : const_cast<PCHAR>(DEFAULT_AWS_REGION);

  // KMSキーID
  channelInfo.pKmsKeyId = NULL;

  // ストリームに紐付くタグの数
  channelInfo.tagCount = 0;

  // ストリームに紐付くタグ
  channelInfo.pTags = NULL;

  // チャネルタイプ
  channelInfo.channelType = SIGNALING_CHANNEL_TYPE_SINGLE_MASTER;

  // チャネルロールタイプ
  channelInfo.channelRoleType = SIGNALING_CHANNEL_ROLE_TYPE_MASTER;

  // キャッシュポリシー
  channelInfo.cachingPolicy = SIGNALING_API_CALL_CACHE_TYPE_FILE;

  // キャッシュ期間
  channelInfo.cachingPeriod = SIGNALING_API_CALL_CACHE_TTL_SENTINEL_VALUE;

  // ICEサーバーの構成情報を非同期で取得する
  channelInfo.asyncIceServerConfig = TRUE;

  // エラーの発生時に再試行する
  channelInfo.retry = TRUE;

  // 再接続する
  channelInfo.reconnect = TRUE;

  // CA証明書のパス
  channelInfo.pCertPath = pCaCertPath;

  // メッセージのTTL (60ns)
  channelInfo.messageTtl = 0;

CleanUp:

  return retStatus;
}

/**
 * @brief コールバックを初期化する
 */
STATUS initCallbacks(UINT64 customData, SignalingClientCallbacks& callbacks)
{
  auto retStatus = STATUS_SUCCESS;

  // コールバックのバージョン
  callbacks.version = SIGNALING_CLIENT_CALLBACKS_CURRENT_VERSION;

  // カスタムデータ
  callbacks.customData = customData;

  // メッセージの受信時
  callbacks.messageReceivedFn = onSignalingMessageReceived;

  // クライアント状態の変化時
  callbacks.stateChangeFn = onSignalingClientStateChanged;

  // エラーの発生時
  callbacks.errorReportFn = onSignalingClientError;

CleanUp:

  return retStatus;
}

/**
 * @brief メトリクスを初期化する
 */
STATUS initMetrics(SignalingClientMetrics& metrics)
{
  auto retStatus = STATUS_SUCCESS;

  // シグナリングクライアントに関するメトリクスのバージョン
  metrics.version = SIGNALING_CLIENT_METRICS_CURRENT_VERSION;

CleanUp:

  return retStatus;
}

/**
 * @brief ピア接続を初期化する
 */
STATUS initPeerConnection(PKvsWebrtcConfig pKvsWebrtcConfig, PRtcPeerConnection& pPeerConnection)
{
  ENTERS();
  auto retStatus = STATUS_SUCCESS;
  RtcConfiguration configuration;
  UINT32 i, j, iceConfigCount, uriCount = 0;
  PIceConfigInfo pIceConfigInfo;

  // ピア接続の設定を初期化
  MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

  // ネットワークインターフェースを制限するためのフィルター関数
  configuration.kvsRtcConfiguration.iceSetInterfaceFilterFunc = NULL;

  // ICEモード
  configuration.iceTransportPolicy = ICE_TRANSPORT_POLICY_ALL;

  // STUNサーバーのエンドポイント
  SNPRINTF(configuration.iceServers[0].urls,
           MAX_ICE_CONFIG_URI_LEN,
           KINESIS_VIDEO_STUN_URL,
           pKvsWebrtcConfig->channelInfo.pRegion,
           KINESIS_VIDEO_STUN_URL_POSTFIX);

  // TURNサーバーの設定
  CHK_STATUS(signalingClientGetIceConfigInfoCount(pKvsWebrtcConfig->signalingHandle, &iceConfigCount));

  // TURNサーバーを1つだけ使用 (候補収集の遅延を最適化)
  for (uriCount = 0, i = 0; i < 1 && i < iceConfigCount; i++) {
    CHK_STATUS(signalingClientGetIceConfigInfo(pKvsWebrtcConfig->signalingHandle, i, &pIceConfigInfo));

    for (j = 0; j < pIceConfigInfo->uriCount; j++) {
      CHECK(uriCount < MAX_ICE_SERVERS_COUNT);

      DLOGD("TURN server %d urls: %s", j + 1, pIceConfigInfo->uris[j]);

      STRNCPY(configuration.iceServers[uriCount + 1].urls, pIceConfigInfo->uris[j], MAX_ICE_CONFIG_URI_LEN);
      STRNCPY(configuration.iceServers[uriCount + 1].credential, pIceConfigInfo->password, MAX_ICE_CONFIG_CREDENTIAL_LEN);
      STRNCPY(configuration.iceServers[uriCount + 1].username, pIceConfigInfo->userName, MAX_ICE_CONFIG_USER_NAME_LEN);

      uriCount++;
    }
  }

  // ICEサーバーの数を保存
  pKvsWebrtcConfig->iceUriCount = uriCount + 1;

  // ピア接続を作成
  CHK_STATUS(createPeerConnection(&configuration, &pPeerConnection));

CleanUp:

  LEAVES();
  return retStatus;
}

// ============================================================================
// シグナリング
// ============================================================================

/**
 * @brief シグナリングクライアントを初期化する
 */
STATUS initSignaling(PKvsWebrtcConfig pKvsWebrtcConfig)
{
  auto retStatus = STATUS_SUCCESS;

  // シグナリングクライアントを作成
  CHK_STATUS(createSignalingClientSync(&pKvsWebrtcConfig->clientInfo,
                                       &pKvsWebrtcConfig->channelInfo,
                                       &pKvsWebrtcConfig->callbacks,
                                       pKvsWebrtcConfig->pCredentialProvider,
                                       &pKvsWebrtcConfig->signalingHandle));

  // シグナリングクライアントの接続に必要な情報を取得
  CHK_STATUS(signalingClientFetchSync(pKvsWebrtcConfig->signalingHandle));

  // シグナリングクライアントを接続
  CHK_STATUS(signalingClientConnectSync(pKvsWebrtcConfig->signalingHandle));

  // シグナリングクライアントに関するメトリクスを取得
  CHK_STATUS(signalingClientGetMetrics(pKvsWebrtcConfig->signalingHandle, &pKvsWebrtcConfig->metrics));

  // ログを出力
  DLOGP(     "getTokenCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.getTokenCallTime);
  DLOGP(     "describeCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.describeCallTime);
  DLOGP("describeMediaCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.describeMediaCallTime);
  DLOGP(       "createCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.createCallTime);
  DLOGP(  "getEndpointCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.getEndpointCallTime);
  DLOGP( "getIceConfigCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.getIceConfigCallTime);
  DLOGP(      "connectCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.connectCallTime);
  DLOGP(  "joinSessionCallTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.joinSessionCallTime);
  DLOGP(     "createClientTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.createClientTime);
  DLOGP(      "fetchClientTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.fetchClientTime);
  DLOGP(    "connectClientTime: %" PRIu64 " ms", pKvsWebrtcConfig->metrics.signalingClientStats.connectClientTime);

CleanUp:

  return retStatus;
}

/**
 * @brief シグナリングクライアントを解放する
 */
STATUS deinitSignaling(PKvsWebrtcConfig pKvsWebrtcConfig)
{
  auto retStatus = STATUS_SUCCESS;

  // NULLチェック
  CHK(pKvsWebrtcConfig, retStatus);

  // 終了フラグをON
  ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isTerminated, TRUE);

  // シグナリングクライアントを解放
  CHK_STATUS(freeSignalingClient(&pKvsWebrtcConfig->signalingHandle));

CleanUp:

  return retStatus;
}

/**
 * @brief シグナリングのメインループ
 */
STATUS loopSignaling(PKvsWebrtcConfig pKvsWebrtcConfig)
{
  ENTERS();
  auto retStatus = STATUS_SUCCESS;
  auto isConfigObjLocked = FALSE;

  // メインループ
  while (!ATOMIC_LOAD_BOOL(&pKvsWebrtcConfig->isInterrupted)) {
    // ロックを開始
    MUTEX_LOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
    isConfigObjLocked = TRUE;

    // 終了したストリーミングセッションを解放
    for (auto&& value : pKvsWebrtcConfig->streamingSessions) {
      if (ATOMIC_LOAD_BOOL(&value.second->isTerminated)) {
        CHK_STATUS(freeKvsWebrtcStreamingSession(value.second));
      }
    }

    // 解放したストリーミングセッションをマップから削除
    std::erase_if(pKvsWebrtcConfig->streamingSessions, [](auto&& value) {
      return !value.second;
    });

    // 5秒間スリープ
    CVAR_WAIT(pKvsWebrtcConfig->cvar, pKvsWebrtcConfig->kvsWebrtcConfigObjLock, (5 * HUNDREDS_OF_NANOS_IN_A_SECOND));

    // ロックを解除
    MUTEX_UNLOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
    isConfigObjLocked = FALSE;
  }

CleanUp:

  CHK_LOG_ERR(retStatus);

  if (isConfigObjLocked) {
    MUTEX_UNLOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  }

  LEAVES();
  return retStatus;
}

// ============================================================================
// WebRTCセッション処理
// ============================================================================

/**
 * @brief SDPオファーを処理する
 */
STATUS handleOffer(PKvsWebrtcConfig pKvsWebrtcConfig, PKvsWebrtcStreamingSession pStreamingSession, SignalingMessage& signalingMessage)
{
  UNUSED_PARAM(pKvsWebrtcConfig);
  auto retStatus = STATUS_SUCCESS;
  RtcSessionDescriptionInit sessionDescriptionInit;
  NullableBool canTrickle;

  // セッション情報を初期化
  MEMSET(&sessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

  // セッション情報を取得
  CHK_STATUS(deserializeSessionDescriptionInit(signalingMessage.payload,
                                               signalingMessage.payloadLen,
                                               &sessionDescriptionInit));

  // リモートのピア接続を設定
  CHK_STATUS(setRemoteDescription(pStreamingSession->pPeerConnection, &sessionDescriptionInit));

  // リモートがTrickle ICEをサポートしているか確認
  canTrickle = canTrickleIceCandidates(pStreamingSession->pPeerConnection);

  // setRemoteDescription後はNULLにならない
  CHECK(!NULLABLE_CHECK_EMPTY(canTrickle));
  pStreamingSession->remoteCanTrickleIce = canTrickle.value;

  // ローカルのピア接続を設定
  CHK_STATUS(setLocalDescription(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit));

  // Trickle ICEをサポートしている場合は即座にアンサーを送信
  // サポートしていない場合はICE候補収集完了後に送信 (onIceCandidateHandlerで処理)
  if (pStreamingSession->remoteCanTrickleIce) {
    // SDPアンサーを作成
    CHK_STATUS(createAnswer(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit));

    // SDPアンサーを送信
    CHK_STATUS(sendAnswer(pStreamingSession));
  }

CleanUp:

  CHK_LOG_ERR(retStatus);

  return retStatus;
}

/**
 * @brief SDPアンサーを送信する
 */
STATUS sendAnswer(PKvsWebrtcStreamingSession pStreamingSession)
{
  auto retStatus = STATUS_SUCCESS;
  auto pKvsWebrtcConfig = pStreamingSession->pKvsWebrtcConfig;
  UINT32 signalingMessageLen = MAX_SIGNALING_MESSAGE_LEN;
  SignalingMessage signalingMessage;

  // SDPアンサーをシリアライズ
  CHK_STATUS(serializeSessionDescriptionInit(&pStreamingSession->answerSessionDescriptionInit,
                                             signalingMessage.payload,
                                             &signalingMessageLen));

  // シグナリングメッセージのバージョン
  signalingMessage.version = SIGNALING_MESSAGE_CURRENT_VERSION;

  // シグナリングメッセージのタイプ
  signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_ANSWER;

  // クライアントID
  STRNCPY(signalingMessage.peerClientId, pStreamingSession->peerClientId, MAX_SIGNALING_CLIENT_ID_LEN);

  // ペイロードの長さ
  signalingMessage.payloadLen = STRLEN(signalingMessage.payload);

  // 関連付けID
  SNPRINTF(signalingMessage.correlationId, MAX_CORRELATION_ID_LEN, "%llu", GETTIME());

  // シグナリングメッセージを送信
  CHK_STATUS(signalingClientSendMessageSync(pKvsWebrtcConfig->signalingHandle, &signalingMessage));

CleanUp:

  CHK_LOG_ERR(retStatus);

  return retStatus;
}

/**
 * @brief ICE候補を送信する
 */
STATUS sendIceCandidate(PKvsWebrtcStreamingSession pStreamingSession, PCHAR candidateJson)
{
  auto retStatus = STATUS_SUCCESS;
  auto pKvsWebrtcConfig = pStreamingSession->pKvsWebrtcConfig;
  SignalingMessage signalingMessage;

  // NULLチェック
  CHK(pStreamingSession && candidateJson, STATUS_NULL_ARG);

  // シグナリングメッセージのバージョン
  signalingMessage.version = SIGNALING_MESSAGE_CURRENT_VERSION;

  // シグナリングメッセージのタイプ
  signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE;

  // クライアントID
  STRNCPY(signalingMessage.peerClientId, pStreamingSession->peerClientId, MAX_SIGNALING_CLIENT_ID_LEN);

  // ペイロード
  signalingMessage.payloadLen = (UINT32) STRNLEN(candidateJson, MAX_SIGNALING_MESSAGE_LEN);
  STRNCPY(signalingMessage.payload, candidateJson, signalingMessage.payloadLen);

  // 関連付けID
  signalingMessage.correlationId[0] = '\0';

  // シグナリングメッセージを送信
  CHK_STATUS(signalingClientSendMessageSync(pKvsWebrtcConfig->signalingHandle, &signalingMessage));

CleanUp:

  CHK_LOG_ERR(retStatus);

  return retStatus;
}

/**
 * @brief リモートからのICE候補を処理する
 */
STATUS handleRemoteCandidate(PKvsWebrtcStreamingSession pStreamingSession, SignalingMessage& signalingMessage)
{
  auto retStatus = STATUS_SUCCESS;
  RtcIceCandidateInit iceCandidate;

  // NULLチェック
  CHK(pStreamingSession, STATUS_NULL_ARG);

  // ICE候補をデシリアライズ
  CHK_STATUS(deserializeRtcIceCandidateInit(signalingMessage.payload, signalingMessage.payloadLen, &iceCandidate));

  // ICE候補を追加
  CHK_STATUS(addIceCandidate(pStreamingSession->pPeerConnection, iceCandidate.candidate));

CleanUp:

  CHK_LOG_ERR(retStatus);

  return retStatus;
}

// ============================================================================
// コールバック
// ============================================================================

/**
 * @brief シグナリングクライアントの状態が変化した際のコールバック
 */
STATUS onSignalingClientStateChanged(UINT64 customData, SIGNALING_CLIENT_STATE state)
{
  UNUSED_PARAM(customData);
  auto retStatus = STATUS_SUCCESS;
  PCHAR pStateStr;

  // 状態を表す文字列を取得
  CHK_STATUS(signalingClientGetStateString(state, &pStateStr));

  // ログを出力
  DLOGV("state: %d (%s)", state, pStateStr);

CleanUp:

  return retStatus;
}

/**
 * @brief シグナリングメッセージを受信した際のコールバック
 */
STATUS onSignalingMessageReceived(UINT64 customData, PReceivedSignalingMessage pReceivedSignalingMessage)
{
  auto retStatus = STATUS_SUCCESS;
  auto pKvsWebrtcConfig = reinterpret_cast<PKvsWebrtcConfig>(customData);
  std::unique_ptr<KvsWebrtcStreamingSession> pStreamingSession;
  auto isConfigObjLocked = FALSE;
  PCHAR pPeerClientId;

  // ロックを開始
  MUTEX_LOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  isConfigObjLocked = TRUE;

  // ログを出力
  DLOGV("messageType: %d, correlationId: %s, peerClientId: %s, payloadLen: %d, payload: %s, statusCode: %d, errorType: %s, description: %s",
        pReceivedSignalingMessage->signalingMessage.messageType,
        pReceivedSignalingMessage->signalingMessage.correlationId,
        pReceivedSignalingMessage->signalingMessage.peerClientId,
        pReceivedSignalingMessage->signalingMessage.payloadLen,
        pReceivedSignalingMessage->signalingMessage.payload,
        pReceivedSignalingMessage->statusCode,
        pReceivedSignalingMessage->errorType,
        pReceivedSignalingMessage->description);

  // クライアントID
  pPeerClientId = pReceivedSignalingMessage->signalingMessage.peerClientId;

  // メッセージタイプ別の処理
  switch (pReceivedSignalingMessage->signalingMessage.messageType) {
    case SIGNALING_MESSAGE_TYPE_OFFER:
      // ストリーミングセッションの存在チェック
      CHK_ERR(!pKvsWebrtcConfig->streamingSessions.contains(pPeerClientId),
              STATUS_INVALID_OPERATION,
              "すでにクライアントID「%s」のピア接続は存在します。",
              pPeerClientId);

      // ストリーミングセッションを作成
      CHK_STATUS(createKvsWebrtcStreamingSession(pKvsWebrtcConfig, pPeerClientId, pStreamingSession));

      // SDPオファーを処理
      CHK_STATUS(handleOffer(pKvsWebrtcConfig, pStreamingSession.get(), pReceivedSignalingMessage->signalingMessage));

      // ストリーミングセッションを保存
      pKvsWebrtcConfig->streamingSessions.emplace(pPeerClientId, std::move(pStreamingSession));
      break;
    case SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE:
      // ストリーミングセッションの存在チェック
      CHK_ERR(pKvsWebrtcConfig->streamingSessions.contains(pPeerClientId),
              STATUS_INVALID_OPERATION,
              "クライアントID「%s」のピア接続が見つかりません。",
              pPeerClientId);

      // リモートからのICE候補を処理
      CHK_STATUS(handleRemoteCandidate(pKvsWebrtcConfig->streamingSessions[pPeerClientId].get(),
                                       pReceivedSignalingMessage->signalingMessage));
      break;
    default:
      break;
  }

  // ロックを解除
  MUTEX_UNLOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  isConfigObjLocked = FALSE;

CleanUp:

  CHK_LOG_ERR(retStatus);

  if (isConfigObjLocked) {
    MUTEX_UNLOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  }

  return retStatus;
}

/**
 * @brief ICE Candidateを受信した際のコールバック
 */
VOID onIceCandidateHandler(UINT64 customData, PCHAR candidateJson)
{
  auto retStatus = STATUS_SUCCESS;
  auto pStreamingSession = reinterpret_cast<PKvsWebrtcStreamingSession>(customData);

  // NULLチェック
  CHK(pStreamingSession, STATUS_NULL_ARG);

  if (candidateJson == NULL) {
    // ログを出力
    DLOGD("ICE candidate gathering done");

    // ICE候補収集完了
    ATOMIC_STORE_BOOL(&pStreamingSession->candidateGatheringDone, TRUE);

    // Trickle ICEをサポートしていない場合はここでアンサーを送信
    if (!pStreamingSession->remoteCanTrickleIce) {
      CHK_STATUS(createAnswer(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit));
      CHK_STATUS(sendAnswer(pStreamingSession));
    }
  } else if (pStreamingSession->remoteCanTrickleIce) {
    // ログを出力
    DLOGV("candidate: %s", candidateJson);

    // Trickle ICEをサポートしている場合はICE候補を送信
    CHK_STATUS(sendIceCandidate(pStreamingSession, candidateJson));
  }

CleanUp:

  CHK_LOG_ERR(retStatus);
}

/**
 * @brief ピア接続の状態が変化した際のコールバック
 */
VOID onConnectionStateChanged(UINT64 customData, RTC_PEER_CONNECTION_STATE state)
{
  auto retStatus = STATUS_SUCCESS;
  auto pStreamingSession = reinterpret_cast<PKvsWebrtcStreamingSession>(customData);
  auto pKvsWebrtcConfig = pStreamingSession->pKvsWebrtcConfig;

  // ログを出力
  DLOGI("state: %u", state);

  // 状態別の処理
  switch (state) {
    case RTC_PEER_CONNECTION_STATE_CONNECTED:
      // 接続フラグをON
      ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isConnected, TRUE);

      // ブロックを解除
      if (IS_VALID_CVAR_VALUE(pKvsWebrtcConfig->cvar)) {
        CVAR_BROADCAST(pKvsWebrtcConfig->cvar);
      }
      break;
    case RTC_PEER_CONNECTION_STATE_FAILED:
    case RTC_PEER_CONNECTION_STATE_CLOSED:
    case RTC_PEER_CONNECTION_STATE_DISCONNECTED:
      // 終了フラグをON
      ATOMIC_STORE_BOOL(&pStreamingSession->isTerminated, TRUE);

      // ブロックを解除
      if (IS_VALID_CVAR_VALUE(pKvsWebrtcConfig->cvar)) {
        CVAR_BROADCAST(pKvsWebrtcConfig->cvar);
      }
    default:
      // 接続フラグをOFF
      ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isConnected, FALSE);

      // ブロックを解除
      if (IS_VALID_CVAR_VALUE(pKvsWebrtcConfig->cvar)) {
        CVAR_BROADCAST(pKvsWebrtcConfig->cvar);
      }
      break;
  }

CleanUp:

  CHK_LOG_ERR(retStatus);
}

/**
 * @brief シグナリングクライアントでエラーが発生した際のコールバック
 */
STATUS onSignalingClientError(UINT64 customData, STATUS status, PCHAR message, UINT32 messageLen)
{
  UNUSED_PARAM(customData);
  auto retStatus = STATUS_SUCCESS;

  // ログを出力
  DLOGW("status: 0x%08x, message: %s, messageLen: %d", status, message, messageLen);

CleanUp:

  return retStatus;
}
