// Adapted from MacNativeGames games/wonder/runtime/offline_host.c (MIT, Copyright (c) 2026 MacNativeGames contributors)
// and nxnative dm/host/hle_offline.c, hle_friends.c, hle_net.c, hle_misc.c (MIT, Copyright (c) 2026 seedds contributors).
// Offline stubs for network, matchmaking, wireless, BCAT, web, eShop, telemetry, and profanity services.
#include <stdint.h>
#include <string.h>

#include "host.h"
#include "ov.h"

#define R_NIFM_UNAVAILABLE ((300u << 9) | 110u)
#define R_SSL_UNAVAILABLE  ((204u << 9) | 123u)
#define R_BCAT_DONE        ((1u << 9) | 122u)
#define R_BCAT_NOT_FOUND   ((2u << 9) | 122u)
#define R_FRIENDS_OK       0u
#define R_LDN_UNAVAILABLE  ((32u << 9) | 203u)
#define R_WEB_CANCELED     ((3u << 9) | 149u)
#define R_NSD_UNAVAILABLE  ((2u << 9) | 141u)

#define SOCKET_ENETDOWN    50
#define SOCKET_EAI_FAIL    4

static _Thread_local int tls_socket_errno;

static void ov_ret_zero(Cpu *c) { ret_x(c, 0); }
static void ov_ret_one(Cpu *c) { ret_x(c, 1); }
static void ov_noop(Cpu *c) { (void)c; }

// ---- nn::nifm
static void ov_nifm_avail(Cpu *c) { ret_x(c, 0); }
static void ov_nifm_unavail(Cpu *c) { ret_x(c, R_NIFM_UNAVAILABLE); }
static void ov_nifm_submit(Cpu *c) {
    (void)c;
    rt_log("nifm: network request declined (offline host)");
}
static void ov_nifm_proxy(Cpu *c) {
    void *out = arg_p(c, 0);
    if (out) memset(out, 0, 0xd8);
    ret_x(c, 0);
}

HOST_OVERRIDE("_ZN2nn4nifm10InitializeEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn4nifm18IsNetworkAvailableEv", ov_nifm_avail);
HOST_OVERRIDE("_ZN2nn4nifm19SetLocalNetworkModeEb", ov_noop);
HOST_OVERRIDE("_ZN2nn4nifm20CancelNetworkRequestEv", ov_noop);
HOST_OVERRIDE("_ZN2nn4nifm22GetCurrentIpConfigInfoEP7in_addrS2_S2_S2_S2_", ov_nifm_unavail);
HOST_OVERRIDE("_ZN2nn4nifm22GetCurrentProxySettingEPNS0_12ProxySettingE", ov_nifm_proxy);
HOST_OVERRIDE("_ZN2nn4nifm26GetCurrentPrimaryIpAddressEP7in_addr", ov_nifm_unavail);
HOST_OVERRIDE("_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv", ov_nifm_submit);
HOST_OVERRIDE("_ZN2nn4nifm31HandleNetworkRequestErrorResultEv", ov_nifm_unavail);

// ---- nn::socket
static void ov_sock_init(Cpu *c) {
    tls_socket_errno = 0;
    ret_x(c, 0);
}
static void ov_sock_down(Cpu *c) {
    tls_socket_errno = SOCKET_ENETDOWN;
    ret_x(c, (uint64_t)(int64_t)-1);
}
static void ov_sock_errno(Cpu *c) { ret_x(c, (uint64_t)(int64_t)tls_socket_errno); }
static void ov_sock_gai_str(Cpu *c) { ret_x(c, (uint64_t)(uintptr_t)"offline"); }
static void ov_sock_getaddrinfo(Cpu *c) {
    void **res = arg_p(c, 3);
    if (res) *res = NULL;
    tls_socket_errno = SOCKET_ENETDOWN;
    ret_x(c, SOCKET_EAI_FAIL);
}

HOST_OVERRIDE("_ZN2nn6socket10InitializeEPvmmi", ov_sock_init);
HOST_OVERRIDE("_ZN2nn6socket8FinalizeEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn6socket12GetLastErrnoEv", ov_sock_errno);
HOST_OVERRIDE("_ZN2nn6socket12GetLastErrorEv", ov_sock_errno);
HOST_OVERRIDE("_ZN2nn6socket11GAIStrErrorENS0_7AiErrnoE", ov_sock_gai_str);
HOST_OVERRIDE("_ZN2nn6socket6SocketEiii", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket5CloseEi", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn6socket8ShutdownEii", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn6socket4BindEiPK8sockaddrj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket4BindEiPKNS0_8SockAddrEj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket6ListenEii", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket7ConnectEiPK8sockaddrj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket6AcceptEiP8sockaddrPj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket4RecvEiPvmi", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket4SendEiPKvmi", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket6SendToEiPKvmNS0_7MsgFlagEPKNS0_8SockAddrEj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket4PollEP6pollfdmi", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket5FcntlEiiz", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket11GetSockNameEiP8sockaddrPj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket10GetSockOptEiiiPvPj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket10SetSockOptEiiiPKvj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj", ov_sock_down);
HOST_OVERRIDE("_ZN2nn6socket11GetAddrInfoEPKcS2_PK8addrinfoPPS3_", ov_sock_getaddrinfo);
HOST_OVERRIDE("_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_", ov_sock_getaddrinfo);
HOST_OVERRIDE("_ZN2nn6socket12FreeAddrInfoEP8addrinfo", ov_noop);
HOST_OVERRIDE("_ZN2nn6socket12FreeAddrInfoEPNS0_8AddrInfoE", ov_noop);

// ---- nn::ssl
static void ov_ssl_unavail(Cpu *c) { ret_x(c, R_SSL_UNAVAILABLE); }
HOST_OVERRIDE("_ZN2nn3ssl7Context6CreateENS1_10SslVersionE", ov_ssl_unavail);
HOST_OVERRIDE("_ZN2nn3ssl7Context15ImportServerPkiEPmPKcjNS0_17CertificateFormatE", ov_ssl_unavail);

// ---- nn::bcat
static void ov_bcat_not_found(Cpu *c) { ret_x(c, R_BCAT_NOT_FOUND); }
static void ov_bcat_dir_read(Cpu *c) {
    int *out = arg_p(c, 1);
    if (out) *out = 0;
    ret_x(c, 0);
}
static void ov_bcat_file_digest(Cpu *c) {
    c->x[0] = 0;
    c->x[1] = 0;
}
static void ov_bcat_prog_status(Cpu *c) { ret_x(c, 9); } /* Status_Done */
static void ov_bcat_prog_result(Cpu *c) { ret_x(c, R_BCAT_DONE); }

HOST_OVERRIDE("_ZN2nn4bcat10InitializeEv", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat25MountDeliveryCacheStorageEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn4bcat27UnmountDeliveryCacheStorageEv", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat17DeliveryCacheFileC1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat17DeliveryCacheFileD1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat17DeliveryCacheFile4OpenERKNS0_13DirectoryNameERKNS0_8FileNameE", ov_bcat_not_found);
HOST_OVERRIDE("_ZN2nn4bcat17DeliveryCacheFile4ReadEPmlPvm", ov_bcat_not_found);
HOST_OVERRIDE("_ZN2nn4bcat17DeliveryCacheFile9GetDigestEv", ov_bcat_file_digest);
HOST_OVERRIDE("_ZN2nn4bcat22DeliveryCacheDirectoryC1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat22DeliveryCacheDirectoryD1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat22DeliveryCacheDirectory4OpenEPKc", ov_bcat_not_found);
HOST_OVERRIDE("_ZN2nn4bcat22DeliveryCacheDirectory4ReadEPiPNS0_27DeliveryCacheDirectoryEntryEi", ov_bcat_dir_read);
HOST_OVERRIDE("_ZN2nn4bcat22DeliveryCacheDirectory5CloseEv", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressE", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn4bcat21DeliveryCacheProgressC1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat21DeliveryCacheProgressD1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn4bcat21DeliveryCacheProgress6UpdateEv", ov_noop);
HOST_OVERRIDE("_ZNK2nn4bcat21DeliveryCacheProgress9GetStatusEv", ov_bcat_prog_status);
HOST_OVERRIDE("_ZNK2nn4bcat21DeliveryCacheProgress9GetResultEv", ov_bcat_prog_result);

// ---- nn::friends
static void ov_friends_list(Cpu *c) {
    int *out = arg_p(c, 0);
    if (out) *out = 0;
    ret_x(c, R_FRIENDS_OK);
}
static void ov_friends_reg_key(Cpu *c) {
    void *key = arg_p(c, 0);
    if (key) memset(key, 0, 0x40);
    ret_x(c, R_FRIENDS_OK);
}

HOST_OVERRIDE("_ZN2nn7friends13GetFriendListEPiPNS0_6FriendERKNS_7account3UidEiiRKNS0_12FriendFilterE", ov_friends_list);
HOST_OVERRIDE("_ZN2nn7friends14AddPlayHistoryERKNS_7account3UidERKNS0_26PlayHistoryRegistrationKeyERKNS0_15InAppScreenNameESA_", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn7friends28DeclareOpenOnlinePlaySessionERKNS_7account3UidE", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn7friends29DeclareCloseOnlinePlaySessionERKNS_7account3UidE", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn7friends29GetPlayHistoryRegistrationKeyEPNS0_26PlayHistoryRegistrationKeyERKNS_7account3UidEb", ov_friends_reg_key);
HOST_OVERRIDE("_ZN2nn7friends6Friend6UpdateEv", ov_ret_zero);

// ---- nn::ldn
static void ov_ldn_unavail(Cpu *c) { ret_x(c, R_LDN_UNAVAILABLE); }
static void ov_ldn_scan(Cpu *c) {
    int *out = arg_p(c, 1);
    if (out) *out = 0;
    ret_x(c, 0);
}
static void ov_ldn_attach_ev(Cpu *c) {
    uint64_t ev = arg_x(c, 0);
    if (ev) kern_bind_system_event(c, ev, 0);
    ret_x(c, 0);
}

HOST_OVERRIDE("_ZN2nn3ldn10InitializeEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn8FinalizeEv", ov_noop);
HOST_OVERRIDE("_ZN2nn3ldn8GetStateEv", ov_ret_one);
HOST_OVERRIDE("_ZN2nn3ldn22AttachStateChangeEventEPNS_2os15SystemEventTypeE", ov_ldn_attach_ev);
HOST_OVERRIDE("_ZN2nn3ldn15OpenAccessPointEv", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn16CloseAccessPointEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn11OpenStationEv", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn12CloseStationEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn13CreateNetworkERKNS0_13NetworkConfigERKNS0_14SecurityConfigERKNS0_10UserConfigE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn20CreateNetworkPrivateERKNS0_13NetworkConfigERKNS0_14SecurityConfigERKNS0_17SecurityParameterERKNS0_10UserConfigEiPKNS0_12AddressEntryE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn14DestroyNetworkEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn7ConnectERKNS0_11NetworkInfoERKNS0_14SecurityConfigERKNS0_10UserConfigEiNS0_13ConnectOptionE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn14ConnectPrivateERKNS0_13NetworkConfigERKNS0_14SecurityConfigERKNS0_17SecurityParameterERKNS0_10UserConfigEiNS0_13ConnectOptionE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn10DisconnectEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn4ScanEPNS0_11NetworkInfoEPiiRKNS0_10ScanFilterEi", ov_ldn_scan);
HOST_OVERRIDE("_ZN2nn3ldn11ScanPrivateEPNS0_11NetworkInfoEPiiRKNS0_10ScanFilterEi", ov_ldn_scan);
HOST_OVERRIDE("_ZN2nn3ldn14GetNetworkInfoEPNS0_11NetworkInfoE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn16GetNetworkConfigEPNS0_13NetworkConfigE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn20GetSecurityParameterEPNS0_17SecurityParameterE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn14GetIpv4AddressEPNS0_11Ipv4AddressEPNS0_10SubnetMaskE", ov_ldn_unavail);
HOST_OVERRIDE("_ZN2nn3ldn16SetAdvertiseDataEPKvm", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn22SetStationAcceptPolicyENS0_12AcceptPolicyE", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn6RejectENS0_11Ipv4AddressE", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ldn19GetDisconnectReasonEv", ov_ret_zero);

// ---- nn::prepo (Initialize / GetTransmissionStatus only; PlayReport::Save* live in ov_system.c)
static void ov_prepo_status(Cpu *c) {
    int32_t *out = arg_p(c, 0);
    if (out) *out = 0;
    ret_x(c, 0);
}
HOST_OVERRIDE("_ZN2nn5prepo10InitializeEv", ov_noop);
HOST_OVERRIDE("_ZN2nn5prepo21GetTransmissionStatusEPNS0_18TransmissionStatusE", ov_prepo_status);

// ---- nn::web, nn::ec, nn::nsd
static void ov_web_show_lobby(Cpu *c) {
    uint64_t *rv = arg_p(c, 0);
    if (rv) *rv = 0;
    ret_x(c, R_WEB_CANCELED);
}
static void ov_nsd_resolve_ex(Cpu *c) {
    char *out = arg_p(c, 0);
    const char *in = arg_p(c, 1);
    if (out && in) {
        size_t i = 0;
        for (; i < 255 && in[i]; i++) out[i] = in[i];
        out[i] = 0;
    }
    ret_x(c, R_NSD_UNAVAILABLE);
}
HOST_OVERRIDE("_ZN2nn3web13ShowLobbyPageEPNS0_20LobbyPageReturnValueERKNS0_16ShowLobbyPageArgE", ov_web_show_lobby);
HOST_OVERRIDE("_ZN2nn2ec30ShowShopApplicationInformationENS_13ApplicationIdE", ov_noop);
HOST_OVERRIDE("_ZN2nn3nsd9ResolveExEPNS0_4FqdnERKS1_", ov_nsd_resolve_ex);

// ---- nn::ngc::ProfanityFilter
static void ov_ngc_mask(Cpu *c) {
    int *out_count = arg_p(c, 1);
    if (out_count) *out_count = 0;
    ret_x(c, 0);
}
static void ov_ngc_check(Cpu *c) {
    uint32_t *masks = arg_p(c, 1);
    uint32_t count = (uint32_t)arg_x(c, 2);
    if (masks && count) memset(masks, 0, count * sizeof *masks);
    ret_x(c, 0);
}

HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilterC1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilterD1Ev", ov_noop);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter10InitializeEPvm", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter10InitializeEPvmb", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter24MaskProfanityWordsInTextEPiPDsj", ov_ngc_mask);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter24MaskProfanityWordsInTextEPiPtj", ov_ngc_mask);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter24MaskProfanityWordsInTextEPiPcj", ov_ngc_mask);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKDsm", ov_ngc_check);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKcm", ov_ngc_check);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter11SetMaskModeENS1_8MaskModeE", ov_noop);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter17GetContentVersionEv", ov_ret_zero);
HOST_OVERRIDE("_ZN2nn3ngc15ProfanityFilter8FinalizeEv", ov_noop);
