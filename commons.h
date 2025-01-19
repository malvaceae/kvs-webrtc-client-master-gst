#ifndef __COMMONS_INCLUDE__
#define __COMMONS_INCLUDE__

#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>

#define IOT_CORE_CREDENTIAL_ENDPOINT ((PCHAR) "AWS_IOT_CORE_CREDENTIAL_ENDPOINT")
#define IOT_CORE_CERT                ((PCHAR) "AWS_IOT_CORE_CERT")
#define IOT_CORE_PRIVATE_KEY         ((PCHAR) "AWS_IOT_CORE_PRIVATE_KEY")
#define IOT_CORE_ROLE_ALIAS          ((PCHAR) "AWS_IOT_CORE_ROLE_ALIAS")
#define IOT_CORE_THING_NAME          ((PCHAR) "AWS_IOT_CORE_THING_NAME")

#define CLIENT_ID ((PCHAR) "kvsWebrtcClientMasterGst")

typedef struct {
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
} KvsWebrtcConfig, *PKvsWebrtcConfig;

VOID sigintHandler(INT32);
UINT32 setLogLevel();
STATUS createKvsWebrtcConfig(PCHAR, UINT32, PKvsWebrtcConfig*);
STATUS freeKvsWebrtcConfig(PKvsWebrtcConfig*);
STATUS getCaCertPath(PCHAR*);
STATUS createCredentialProvider(PCHAR, PAwsCredentialProvider*);
STATUS initClientInfo(UINT32, PSignalingClientInfo);
STATUS initChannelInfo(PCHAR, PCHAR, PCHAR, PChannelInfo);
STATUS initCallbacks(UINT64, PSignalingClientCallbacks);
STATUS initSignaling(PKvsWebrtcConfig);
STATUS loopSignaling(PKvsWebrtcConfig);
STATUS onMessageReceived(UINT64, PReceivedSignalingMessage);
STATUS onStateChanged(UINT64, SIGNALING_CLIENT_STATE);
STATUS onErrorReport(UINT64, STATUS, PCHAR, UINT32);

#endif
