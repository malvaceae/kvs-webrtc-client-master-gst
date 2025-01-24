#ifndef KVS_HPP_
#define KVS_HPP_

#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>
#include <unordered_map>
#include <string>

#define IOT_CORE_CREDENTIAL_ENDPOINT "AWS_IOT_CORE_CREDENTIAL_ENDPOINT"
#define IOT_CORE_CERT                "AWS_IOT_CORE_CERT"
#define IOT_CORE_PRIVATE_KEY         "AWS_IOT_CORE_PRIVATE_KEY"
#define IOT_CORE_ROLE_ALIAS          "AWS_IOT_CORE_ROLE_ALIAS"
#define IOT_CORE_THING_NAME          "AWS_IOT_CORE_THING_NAME"

#define CLIENT_ID "kvsWebrtcClientMasterGst"

#define VIDEO_CODEC RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE
#define AUDIO_CODEC RTC_CODEC_OPUS

#define VIDEO_STREAM_ID "kvsWebrtcStream"
#define AUDIO_STREAM_ID "kvsWebrtcStream"

#define VIDEO_TRACK_ID "kvsWebrtcVideoTrack"
#define AUDIO_TRACK_ID "kvsWebrtcAudioTrack"

struct KvsWebrtcConfig;
using PKvsWebrtcConfig = KvsWebrtcConfig*;

struct KvsWebrtcStreamingSession;
using PKvsWebrtcStreamingSession = KvsWebrtcStreamingSession*;

struct KvsWebrtcConfig {
  // 接続フラグ
  volatile ATOMIC_BOOL isConnected;

  // 中断フラグ
  volatile ATOMIC_BOOL isInterrupted;

  // 終了フラグ
  volatile ATOMIC_BOOL isTerminated;

  // ミューテックス
  MUTEX kvsWebrtcConfigObjLock;

  // 条件変数
  CVAR cvar;

  // ストリーミングセッションのマップ
  std::unordered_map<std::string, PKvsWebrtcStreamingSession>* pStreamingSessions;

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

struct KvsWebrtcStreamingSession {
  // 終了フラグ
  volatile ATOMIC_BOOL isTerminated;

  // KVS WebRTCの設定
  PKvsWebrtcConfig pKvsWebrtcConfig;

  // クライアントID
  CHAR peerClientId[MAX_SIGNALING_CLIENT_ID_LEN + 1];

  // ピア接続
  PRtcPeerConnection pPeerConnection;

  // トランシーバー
  PRtcRtpTransceiver pVideoRtcRtpTransceiver;
  PRtcRtpTransceiver pAudioRtcRtpTransceiver;

  // SDPアンサー
  RtcSessionDescriptionInit answerSessionDescriptionInit;
};

VOID setSigintHandler(PKvsWebrtcConfig&);
UINT32 setLogLevel();
STATUS createKvsWebrtcConfig(PCHAR, UINT32, PKvsWebrtcConfig&);
STATUS freeKvsWebrtcConfig(PKvsWebrtcConfig&);
STATUS createKvsWebrtcStreamingSession(PKvsWebrtcConfig&, PCHAR, PKvsWebrtcStreamingSession&);
STATUS freeKvsWebrtcStreamingSession(PKvsWebrtcStreamingSession&);
STATUS getCaCertPath(PCHAR&);
STATUS createCredentialProvider(PCHAR, PAwsCredentialProvider&);
STATUS initClientInfo(UINT32, SignalingClientInfo&);
STATUS initChannelInfo(PCHAR, PCHAR, PCHAR, ChannelInfo&);
STATUS initCallbacks(UINT64, SignalingClientCallbacks&);
STATUS initMetrics(SignalingClientMetrics&);
STATUS initPeerConnection(PKvsWebrtcConfig&, PRtcPeerConnection&);
STATUS initSignaling(PKvsWebrtcConfig);
STATUS deinitSignaling(PKvsWebrtcConfig);
STATUS loopSignaling(PKvsWebrtcConfig);
STATUS handleOffer(PKvsWebrtcConfig, PKvsWebrtcStreamingSession, SignalingMessage&);
STATUS sendAnswer(PKvsWebrtcStreamingSession);
STATUS onSignalingMessageReceived(UINT64, PReceivedSignalingMessage);
STATUS onSignalingClientStateChanged(UINT64, SIGNALING_CLIENT_STATE);
STATUS onSignalingClientError(UINT64, STATUS, PCHAR, UINT32);
VOID onIceCandidateHandler(UINT64, PCHAR);
VOID onConnectionStateChanged(UINT64, RTC_PEER_CONNECTION_STATE);

#endif
