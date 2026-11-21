/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * transfer_contract.h - credential-free commands and copied transfer observations.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TRANSFER_CONTRACT_H
#define SURFACE_TRANSFER_CONTRACT_H
#include <stdint.h>
/** @brief Provider outcomes; PENDING and BUSY retain an outstanding command token. */
enum { SURFACE_TRANSFER_OK=0,SURFACE_TRANSFER_PENDING=1,SURFACE_TRANSFER_BUSY=-2,
       SURFACE_TRANSFER_UNAVAILABLE=-3,SURFACE_TRANSFER_STALE=-4,SURFACE_TRANSFER_INVALID=-5,
       SURFACE_TRANSFER_FAILED=-6,SURFACE_TRANSFER_CANCELLED=-7 };
/** @brief Configuration and command state, separate from current listener readiness. */
enum { SURFACE_TRANSFER_UNCONFIGURED,SURFACE_TRANSFER_STOPPED,SURFACE_TRANSFER_STARTING,
       SURFACE_TRANSFER_READY,SURFACE_TRANSFER_STOPPING,SURFACE_TRANSFER_ERROR };
/** @brief Commands consume a provisioned configuration generation; no credentials cross this boundary. */
typedef enum { SURFACE_TRANSFER_ENABLE,SURFACE_TRANSFER_STOP } surface_transfer_command_t;
/** @brief Copied progress distinguishes accepted input, committed uploads and prepared reads. */
typedef struct {
    uint64_t generation,accepted,committed,expected,prepared,read_length;
    unsigned available,state,ready,connected,uploading,reading,recovering,cleanup_required;
    int error;
} surface_transfer_snapshot_t;
/** @brief Serialized provider; calls must not wait for storage or network work.
 * @note Observe copies state; submit returns OK with a token or retains nothing.
 * Take consumes a terminal result exactly once. PENDING/BUSY retain the token;
 * a negative terminal return invalidates it. Provider lifetime exceeds the shell
 * and its outstanding tokens. Application navigation does not stop the service. */
typedef struct {
    void *context;
    int (*observe)(void *,surface_transfer_snapshot_t *);
    int (*submit)(void *,surface_transfer_command_t,uint64_t generation,uint64_t *token);
    int (*take)(void *,uint64_t token,int *result);
} surface_transfer_provider_t;
#endif
