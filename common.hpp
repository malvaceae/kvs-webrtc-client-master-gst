#ifndef COMMON_HPP_
#define COMMON_HPP_

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>
#include <unordered_map>
#include <string>
#include <memory>

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
  std::unordered_map<std::string, std::unique_ptr<KvsWebrtcStreamingSession>> streamingSessions;

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

  // 設定されたICEサーバーの数
  UINT32 iceUriCount;
};

struct KvsWebrtcStreamingSession {
  // 終了フラグ
  volatile ATOMIC_BOOL isTerminated;

  // ICE候補収集完了フラグ
  volatile ATOMIC_BOOL candidateGatheringDone;

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

  // リモートがTrickle ICEをサポートしているか
  BOOL remoteCanTrickleIce;
};

// ============================================================================
// ユーティリティ
// ============================================================================

/**
 * @brief ログレベルを設定する
 */
UINT32 setLogLevel();

/**
 * @brief SIGINTハンドラを設定する
 */
VOID setSigintHandler(PKvsWebrtcConfig);

/**
 * @brief CA証明書のパスを取得する
 */
STATUS getCaCertPath(PCHAR&);

// ============================================================================
// KvsWebrtcConfig 管理
// ============================================================================

/**
 * @brief KVS WebRTCの設定を作成する
 */
STATUS createKvsWebrtcConfig(PCHAR, UINT32, std::unique_ptr<KvsWebrtcConfig>&);

/**
 * @brief KVS WebRTCの設定を解放する
 */
STATUS freeKvsWebrtcConfig(std::unique_ptr<KvsWebrtcConfig>&);

// ============================================================================
// KvsWebrtcStreamingSession 管理
// ============================================================================

/**
 * @brief ストリーミングセッションを作成する
 */
STATUS createKvsWebrtcStreamingSession(PKvsWebrtcConfig, PCHAR, std::unique_ptr<KvsWebrtcStreamingSession>&);

/**
 * @brief ストリーミングセッションを解放する
 */
STATUS freeKvsWebrtcStreamingSession(std::unique_ptr<KvsWebrtcStreamingSession>&);

// ============================================================================
// 初期化
// ============================================================================

/**
 * @brief 認証情報プロバイダーを作成する
 */
STATUS createCredentialProvider(PCHAR, PAwsCredentialProvider&);

/**
 * @brief クライアント情報を初期化する
 */
STATUS initClientInfo(UINT32, SignalingClientInfo&);

/**
 * @brief チャネル情報を初期化する
 */
STATUS initChannelInfo(PCHAR, PCHAR, PCHAR, ChannelInfo&);

/**
 * @brief コールバックを初期化する
 */
STATUS initCallbacks(UINT64, SignalingClientCallbacks&);

/**
 * @brief メトリクスを初期化する
 */
STATUS initMetrics(SignalingClientMetrics&);

/**
 * @brief ピア接続を初期化する
 */
STATUS initPeerConnection(PKvsWebrtcConfig, PRtcPeerConnection&);

// ============================================================================
// シグナリング
// ============================================================================

/**
 * @brief シグナリングクライアントを初期化する
 */
STATUS initSignaling(PKvsWebrtcConfig);

/**
 * @brief シグナリングクライアントを解放する
 */
STATUS deinitSignaling(PKvsWebrtcConfig);

/**
 * @brief シグナリングのメインループ
 */
STATUS loopSignaling(PKvsWebrtcConfig);

// ============================================================================
// WebRTCセッション処理
// ============================================================================

/**
 * @brief SDPオファーを処理する
 */
STATUS handleOffer(PKvsWebrtcConfig, PKvsWebrtcStreamingSession, SignalingMessage&);

/**
 * @brief SDPアンサーを送信する
 */
STATUS sendAnswer(PKvsWebrtcStreamingSession);

/**
 * @brief ICE候補を送信する
 */
STATUS sendIceCandidate(PKvsWebrtcStreamingSession, PCHAR);

/**
 * @brief リモートからのICE候補を処理する
 */
STATUS handleRemoteCandidate(PKvsWebrtcStreamingSession, SignalingMessage&);

// ============================================================================
// コールバック
// ============================================================================

/**
 * @brief シグナリングクライアントの状態が変化した際のコールバック
 */
STATUS onSignalingClientStateChanged(UINT64, SIGNALING_CLIENT_STATE);

/**
 * @brief シグナリングメッセージを受信した際のコールバック
 */
STATUS onSignalingMessageReceived(UINT64, PReceivedSignalingMessage);

/**
 * @brief ICE Candidateを受信した際のコールバック
 */
VOID onIceCandidateHandler(UINT64, PCHAR);

/**
 * @brief ピア接続の状態が変化した際のコールバック
 */
VOID onConnectionStateChanged(UINT64, RTC_PEER_CONNECTION_STATE);

/**
 * @brief シグナリングクライアントでエラーが発生した際のコールバック
 */
STATUS onSignalingClientError(UINT64, STATUS, PCHAR, UINT32);

#endif
