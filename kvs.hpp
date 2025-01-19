#ifndef __KVS_INCLUDE__
#define __KVS_INCLUDE__

#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>

#define IOT_CORE_CREDENTIAL_ENDPOINT "AWS_IOT_CORE_CREDENTIAL_ENDPOINT"
#define IOT_CORE_CERT                "AWS_IOT_CORE_CERT"
#define IOT_CORE_PRIVATE_KEY         "AWS_IOT_CORE_PRIVATE_KEY"
#define IOT_CORE_ROLE_ALIAS          "AWS_IOT_CORE_ROLE_ALIAS"
#define IOT_CORE_THING_NAME          "AWS_IOT_CORE_THING_NAME"

#define CLIENT_ID "kvsWebrtcClientMasterGst"

struct KvsWebrtcConfig {
  // 中断フラグ
  volatile ATOMIC_BOOL isInterrupted;

  // ミューテックス
  MUTEX kvsWebrtcConfigObjLock;

  // 条件変数
  CVAR cvar;

  // CA証明書のパス
  PCHAR pCaCertPath;

  // 認証情報プロバイダー
  PAwsCredentialProvider pCredentialProvider;

  // クライアント情報
  SignalingClientInfo clientInfo;

  // チャネル情報
  ChannelInfo channelInfo;

  // コールバック
  SignalingClientCallbacks callbacks;

  // シグナリングクライアントに関するメトリクス
  SignalingClientMetrics metrics;

  // シグナリングクライアント
  SIGNALING_CLIENT_HANDLE signalingHandle;
};

using PKvsWebrtcConfig = KvsWebrtcConfig*;

VOID sigintHandler(INT32);
UINT32 setLogLevel();
STATUS createKvsWebrtcConfig(PCHAR, UINT32, PKvsWebrtcConfig&);
STATUS freeKvsWebrtcConfig(PKvsWebrtcConfig&);
STATUS getCaCertPath(PCHAR&);
STATUS createCredentialProvider(PCHAR, PAwsCredentialProvider&);
STATUS initClientInfo(UINT32, SignalingClientInfo&);
STATUS initChannelInfo(PCHAR, PCHAR, PCHAR, ChannelInfo&);
STATUS initCallbacks(UINT64, SignalingClientCallbacks&);
STATUS initMetrics(SignalingClientMetrics&);
STATUS initSignaling(PKvsWebrtcConfig);
STATUS deinitSignaling(PKvsWebrtcConfig);
STATUS loopSignaling(PKvsWebrtcConfig);
STATUS onMessageReceived(UINT64, PReceivedSignalingMessage);
STATUS onStateChanged(UINT64, SIGNALING_CLIENT_STATE);
STATUS onErrorReport(UINT64, STATUS, PCHAR, UINT32);

#endif
