/* Compile-time lock on payload sizes: a DSDL edit that changes a frame count
 * fails the build here, before it silently changes the bus budget. */
#include <cuauv.Kill.h>
#include <cuauv.HostHeartbeat.h>
#include <cuauv.KillRequest.h>
#include <cuauv.ThrusterStatus.h>
#include <cuauv.Depth.h>
#include <cuauv.HullStatus.h>
#include <cuauv.PowerStatus.h>
#include <cuauv.RailCurrents.h>
#include <cuauv.RailCommand.h>
#include <cuauv.BatteryDetail.h>
#include <cuauv.PingerBearing.h>
#include <cuauv.AcousticsStatus.h>
#include <cuauv.AcousticsCommand.h>

/* bytes -> frames: <=7 is one frame, else ceil((bytes + 2 CRC) / 7) */
#define FRAMES(b) ((b) <= 7 ? 1 : ((b) + 2 + 6) / 7)

_Static_assert(FRAMES(CUAUV_KILL_MAX_SIZE)             == 1, "Kill must stay single-frame");
_Static_assert(FRAMES(CUAUV_HOSTHEARTBEAT_MAX_SIZE)    == 1, "HostHeartbeat");
_Static_assert(FRAMES(CUAUV_KILLREQUEST_MAX_SIZE)      == 1, "KillRequest must stay single-frame");
_Static_assert(FRAMES(CUAUV_THRUSTERSTATUS_MAX_SIZE)   == 3, "ThrusterStatus (8 thrusters)");
_Static_assert(FRAMES(CUAUV_DEPTH_MAX_SIZE)            == 1, "Depth");
_Static_assert(FRAMES(CUAUV_HULLSTATUS_MAX_SIZE)       == 1, "HullStatus");
_Static_assert(FRAMES(CUAUV_POWERSTATUS_MAX_SIZE)      == 2, "PowerStatus");
_Static_assert(FRAMES(CUAUV_RAILCURRENTS_MAX_SIZE)     == 5, "RailCurrents");
_Static_assert(FRAMES(CUAUV_RAILCOMMAND_MAX_SIZE)      == 1, "RailCommand");
_Static_assert(FRAMES(CUAUV_BATTERYDETAIL_MAX_SIZE)    == 6, "BatteryDetail");
_Static_assert(FRAMES(CUAUV_PINGERBEARING_MAX_SIZE)    == 1, "PingerBearing");
_Static_assert(FRAMES(CUAUV_ACOUSTICSSTATUS_MAX_SIZE)  == 1, "AcousticsStatus");
_Static_assert(FRAMES(CUAUV_ACOUSTICSCOMMAND_MAX_SIZE) == 1, "AcousticsCommand");
