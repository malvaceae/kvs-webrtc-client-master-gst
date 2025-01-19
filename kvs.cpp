#include "kvs.hpp"

PKvsWebrtcConfig gKvsWebrtcConfig = nullptr;

VOID sigintHandler(INT32 sigNum)
{
  UNUSED_PARAM(sigNum);

  if (gKvsWebrtcConfig) {
    // 中断フラグをON
    ATOMIC_STORE_BOOL(&gKvsWebrtcConfig->isInterrupted, TRUE);

    // ブロックを解除
    if (IS_VALID_CVAR_VALUE(gKvsWebrtcConfig->cvar)) {
      CVAR_BROADCAST(gKvsWebrtcConfig->cvar);
    }
  }
}

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

STATUS createKvsWebrtcConfig(PCHAR pChannelName, UINT32 logLevel, PKvsWebrtcConfig& pKvsWebrtcConfig)
{
  STATUS retStatus = STATUS_SUCCESS;

  // KVS WebRTCの設定を初期化
  pKvsWebrtcConfig = new KvsWebrtcConfig;

  // 中断フラグ
  ATOMIC_STORE_BOOL(&pKvsWebrtcConfig->isInterrupted, FALSE);

  // ミューテックス
  pKvsWebrtcConfig->kvsWebrtcConfigObjLock = MUTEX_CREATE(TRUE);

  // 条件変数
  pKvsWebrtcConfig->cvar = CVAR_CREATE();

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
  CHK_STATUS(initCallbacks(reinterpret_cast<UINT64>(pKvsWebrtcConfig), pKvsWebrtcConfig->callbacks));

  // シグナリングクライアントに関するメトリクス初期化
  CHK_STATUS(initMetrics(pKvsWebrtcConfig->metrics));

CleanUp:

  if (STATUS_FAILED(retStatus)) {
    freeKvsWebrtcConfig(pKvsWebrtcConfig);
  }

  return retStatus;
}

STATUS freeKvsWebrtcConfig(PKvsWebrtcConfig& pKvsWebrtcConfig)
{
  ENTERS();
  STATUS retStatus = STATUS_SUCCESS;

  // ミューテックスを解放
  if (IS_VALID_MUTEX_VALUE(pKvsWebrtcConfig->kvsWebrtcConfigObjLock)) {
    MUTEX_FREE(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  }

  // 条件変数を解放
  if (IS_VALID_CVAR_VALUE(pKvsWebrtcConfig->cvar)) {
    CVAR_FREE(pKvsWebrtcConfig->cvar);
  }

  // 認証情報プロバイダーを解放
  freeIotCredentialProvider(&pKvsWebrtcConfig->pCredentialProvider);

  // KVS WebRTCの設定を解放
  delete pKvsWebrtcConfig;
  pKvsWebrtcConfig = nullptr;

CleanUp:

  LEAVES();
  return retStatus;
}

STATUS getCaCertPath(PCHAR& pCaCertPath)
{
  STATUS retStatus = STATUS_SUCCESS;

  // CA証明書のパス
  CHK_ERR(pCaCertPath = GETENV(CACERT_PATH_ENV_VAR), STATUS_INVALID_OPERATION, "環境変数「%s」は必須です。", CACERT_PATH_ENV_VAR);

CleanUp:

  return retStatus;
}

STATUS createCredentialProvider(PCHAR pCaCertPath, PAwsCredentialProvider& pCredentialProvider)
{
  STATUS retStatus = STATUS_SUCCESS;
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

STATUS initClientInfo(UINT32 logLevel, SignalingClientInfo& clientInfo)
{
  STATUS retStatus = STATUS_SUCCESS;

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

STATUS initChannelInfo(PCHAR pChannelName, PCHAR pCaCertPath, PCHAR pRegion, ChannelInfo& channelInfo)
{
  STATUS retStatus = STATUS_SUCCESS;

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

STATUS initCallbacks(UINT64 customData, SignalingClientCallbacks& callbacks)
{
  STATUS retStatus = STATUS_SUCCESS;

  // コールバックのバージョン
  callbacks.version = SIGNALING_CLIENT_CALLBACKS_CURRENT_VERSION;

  // カスタムデータ
  callbacks.customData = customData;

  // メッセージの受信時
  callbacks.messageReceivedFn = onMessageReceived;

  // クライアント状態の変化時
  callbacks.stateChangeFn = onStateChanged;

  // エラーの発生時
  callbacks.errorReportFn = onErrorReport;

CleanUp:

  return retStatus;
}

STATUS initMetrics(SignalingClientMetrics& metrics)
{
  STATUS retStatus = STATUS_SUCCESS;

  // シグナリングクライアントに関するメトリクスのバージョン
  metrics.version = SIGNALING_CLIENT_METRICS_CURRENT_VERSION;

CleanUp:

  return retStatus;
}

STATUS initSignaling(PKvsWebrtcConfig pKvsWebrtcConfig)
{
  STATUS retStatus = STATUS_SUCCESS;

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

  // SIGINTハンドラ用の変数に設定を格納
  gKvsWebrtcConfig = pKvsWebrtcConfig;

CleanUp:

  return retStatus;
}

STATUS deinitSignaling(PKvsWebrtcConfig pKvsWebrtcConfig)
{
  STATUS retStatus = STATUS_SUCCESS;

  // シグナリングクライアントを解放
  CHK_STATUS(freeSignalingClient(&pKvsWebrtcConfig->signalingHandle));

  // SIGINTハンドラ用の変数を初期化
  gKvsWebrtcConfig = nullptr;

CleanUp:

  return retStatus;
}

STATUS loopSignaling(PKvsWebrtcConfig pKvsWebrtcConfig)
{
  ENTERS();
  STATUS retStatus = STATUS_SUCCESS;
  BOOL isObjLocked = FALSE;

  // メインループ
  while (!ATOMIC_LOAD_BOOL(&pKvsWebrtcConfig->isInterrupted)) {
    // ロックを開始
    MUTEX_LOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
    isObjLocked = TRUE;

    // 5秒間スリープ
    CVAR_WAIT(pKvsWebrtcConfig->cvar, pKvsWebrtcConfig->kvsWebrtcConfigObjLock, (5 * HUNDREDS_OF_NANOS_IN_A_SECOND));

    // ロックを解除
    MUTEX_UNLOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
    isObjLocked = FALSE;
  }

CleanUp:

  CHK_LOG_ERR(retStatus);

  if (isObjLocked) {
    MUTEX_UNLOCK(pKvsWebrtcConfig->kvsWebrtcConfigObjLock);
  }

  LEAVES();
  return retStatus;
}

STATUS onMessageReceived(UINT64 customData, PReceivedSignalingMessage pReceivedSignalingMessage)
{
  UNUSED_PARAM(customData);
  STATUS retStatus = STATUS_SUCCESS;

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

CleanUp:

  return retStatus;
}

STATUS onStateChanged(UINT64 customData, SIGNALING_CLIENT_STATE state)
{
  UNUSED_PARAM(customData);
  STATUS retStatus = STATUS_SUCCESS;
  PCHAR pStateStr;

  // 状態を表す文字列を取得
  CHK_STATUS(signalingClientGetStateString(state, &pStateStr));

  // ログを出力
  DLOGV("state: %d (%s)", state, pStateStr);

CleanUp:

  return retStatus;
}

STATUS onErrorReport(UINT64 customData, STATUS status, PCHAR message, UINT32 messageLen)
{
  UNUSED_PARAM(customData);
  STATUS retStatus = STATUS_SUCCESS;

  // ログを出力
  DLOGW("status: %d, message: %s, messageLen: %d", status, message, messageLen);

CleanUp:

  return retStatus;
}
