/*
 * app.c - auvCAN board template.
 *
 * Copy boards/template/ to boards/<your_board>/, edit board_config.h,
 * then add your board logic in the marked places.
 *
 * Out of the box this node:
 *   - joins the bus at 1 Mbps (CAN 2.0B) with hardware filters
 *   - publishes NodeStatus at 1 Hz, answers GetNodeInfo and RestartNode
 *     (it appears in the DroneCAN GUI Tool with its name, versions and UID)
 *   - if BOARD_IS_ACTUATOR: boots KILLED, arms only on fresh cuauv.Kill from node 1
 */
#include "app.h"

#include "main.h"                 /* CubeMX: HAL, hfdcan1, Error_Handler */
#include "board_config.h"
#include "auvcan.h"
#include "auvcan_port_g4.h"

extern FDCAN_HandleTypeDef hfdcan1;

static auvcan_g4_t g4;
static auvcan_t    can;

/* ------------------------------------------------------------------------- */
/* Safety                                                                     */
/* ------------------------------------------------------------------------- */

#if BOARD_IS_ACTUATOR
/* Called on every killed<->armed edge, from auvcan_poll() context.
 * Put outputs in their safe state HERE, not just in app_loop(). */
static void on_kill_change(auvcan_t *c, bool killed, void *user)
{
    (void)c; (void)user;
    if (killed) {
        /* TODO(board): outputs to safe state (e.g. ESC neutral, rails off) */
    }
}
#endif

/* ------------------------------------------------------------------------- */
/* Example: command subscription with staleness (thruster pattern)            */
/* ------------------------------------------------------------------------- */
/*
 * #include <uavcan.equipment.esc.RawCommand.h>
 *
 * static auvcan_fresh_t cmd_fresh;
 * static struct uavcan_equipment_esc_RawCommand cmd;
 *
 * static void on_rawcmd(auvcan_t *c, CanardRxTransfer *t, void *u) {
 *     if (!uavcan_equipment_esc_RawCommand_decode(t, &cmd))
 *         auvcan_fresh_feed(&cmd_fresh, auvcan_micros(c));
 * }
 *
 * setup:  auvcan_fresh_init(&cmd_fresh, AUVCAN_RAWCMD_STALE_MS);
 *         auvcan_subscribe_hp(&can, UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_ID,
 *                             UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_SIGNATURE, on_rawcmd, NULL);
 * loop:   bool run = !auvcan_is_killed(&can) && auvcan_fresh_ok(&cmd_fresh, auvcan_micros(&can));
 *         drive_outputs(run ? cmd.cmd : NEUTRAL);   // through the ramp limiter
 *         auvcan_set_health(&can, run || auvcan_is_killed(&can) ? HEALTH_OK : HEALTH_WARNING);
 */

/* ------------------------------------------------------------------------- */

void app_setup(void)
{
    if (auvcan_g4_init(&g4, &hfdcan1, FDCAN1, BOARD_FDCAN_CLOCK_HZ) != 0) {
        Error_Handler();          /* -1: no exact 1 Mbps timing for this clock */
    }

    const auvcan_config_t cfg = {
        .node_id        = BOARD_NODE_ID,
        .name           = BOARD_NAME,
        .hw_major       = BOARD_HW_MAJOR,
        .hw_minor       = BOARD_HW_MINOR,
        .sw_major       = BOARD_SW_MAJOR,
        .sw_minor       = BOARD_SW_MINOR,
        .sw_vcs_commit  = BOARD_GIT_HASH,
#if BOARD_IS_ACTUATOR
        .kill_timeout_ms = AUVCAN_KILL_TIMEOUT_MS,
        .on_kill_change  = on_kill_change,
#else
        .kill_timeout_ms = 0,
#endif
        .port           = auvcan_g4_port(&g4),
    };
    if (auvcan_init(&can, &cfg) != 0) {
        Error_Handler();
    }

    /* TODO(board): auvcan_subscribe(...) / auvcan_serve(...) here, BEFORE start. */

    if (auvcan_start(&can) != 0) {
        Error_Handler();
    }
}

void app_loop(void)
{
    auvcan_poll(&can);

    /* TODO(board): read sensors / drive outputs / publish telemetry.
     * Keep each iteration short (< 1 ms): the RX rings hold 32 frames each. */
}
